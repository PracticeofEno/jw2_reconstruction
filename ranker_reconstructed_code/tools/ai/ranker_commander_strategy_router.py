"""Evaluate a frozen Elf strategy selected once by the public opponent race.

This controller uses reward-selected numeric profiles and the native rule
executor. Its rollouts remain teacher data, never neural/PPO trajectories.
Loading or evaluating a router does not adopt or deploy it.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import copy
import json
from pathlib import Path
import re
import threading

from ranker_commander_strategy import KEYS, load_strategy, sha, validate_strategy

KIND = "reward_optimized_elf_contextual_strategy"
ROUTE_KEYS = {"controller_manifest", "controller_manifest_sha256", "profile_sha256"}
ROUTES = {str(i) for i in range(4)}
TOP_KEYS = {"kind", "method", "own_tribe", "schema_crc", "executable",
            "executable_sha256", "input", "routes", "selection_evidence",
            "low_level_teacher", "neural_actor", "ppo_enabled"}
TEACHER_VARIANT = 268435456


def _object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON field: {key}")
        value[key] = item
    return value


def _read(path):
    def reject(value):
        raise ValueError(f"nonfinite JSON constant: {value}")
    return json.loads(Path(path).read_text(encoding="utf-8"),
                      object_pairs_hook=_object, parse_constant=reject)


def _integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"invalid {name}")
    return value


def _file(path, checksum):
    path = Path(path)
    if not path.is_absolute() or not re.fullmatch(r"[0-9a-f]{64}", checksum):
        raise ValueError("router file requires an absolute path and SHA256")
    if sha(path) != checksum:
        raise ValueError(f"router dependency changed: {path}")
    return path


def _source(source):
    if set(source) != {"path", "sha256"}:
        raise ValueError("unexpected router source fields")
    return _file(source["path"], source["sha256"])


def _rule_ablation(definition, depth=0):
    """Accept an executor change only with verified unchanged numeric ancestry.

    This is not a CEM/NN update. The router's selection validator separately
    checks either numeric-strategy evidence or explicit executor-migration reuse.
    """
    if depth >= 16:
        raise ValueError("rule experiment ancestry exceeds 16 changes")
    method = definition.get("method")
    if method not in ("single_rule_ablation", "executor_rule_experiment"):
        raise ValueError("unsupported rule experiment ancestry")
    provenance = definition.get("provenance", {})
    if (provenance.get("unchanged_numeric_profile") is not True
            or provenance.get("numeric_parameters_retrained") is not False
            or not isinstance(provenance.get("candidate_build_manifest"), dict)
            or not isinstance(provenance.get("parent_controller"), dict)):
        raise ValueError("rule ablation requires explicit unchanged-profile provenance")
    candidate = _read(_source(provenance["candidate_build_manifest"]))
    if (candidate.get("kind") != method or candidate.get("own_tribe") != 1
            or candidate.get("schema_crc") != definition["schema_crc"]
            or candidate.get("rule_teacher") is not True or candidate.get("neural_actor") is not False
            or candidate.get("ppo_enabled") is not False
            or not candidate.get("exact_patch") or not candidate.get("predicate")
            or provenance.get("rule_change") != candidate["exact_patch"]
            or provenance.get("rule_predicate") != candidate["predicate"]):
        raise ValueError("rule ablation manifest or declared change differs")
    if (candidate.get("executable_sha256") != definition["executable_sha256"]
            or Path(candidate["executable"]).resolve() != Path(definition["executable"]).resolve()):
        raise ValueError("rule ablation executable differs from its frozen build")
    _file(candidate["executable"], candidate["executable_sha256"])
    parent = provenance["parent_controller"]
    if set(parent) != {"manifest", "manifest_sha256", "definition"}:
        raise ValueError("rule ablation parent binding differs")
    parent_path = _file(parent["manifest"], parent["manifest_sha256"])
    if _read(parent_path) != parent["definition"]:
        raise ValueError("rule ablation parent definition changed")
    original = validate_strategy(parent, executable_sha=candidate["parent_executable_sha256"])
    parent_methods = ("CEM", "coordinate_reward_search")
    if method == "executor_rule_experiment":
        parent_methods += ("single_rule_ablation", "executor_rule_experiment")
    if (original.get("method") not in parent_methods
            or original.get("low_level_teacher") is not True or original.get("neural_actor") is not False
            or original.get("ppo_eligible") is not False
            or Path(original["executable"]).resolve() != Path(candidate["parent_executable"]).resolve()):
        raise ValueError("rule ablation parent is not the declared reward-selected executor")
    _file(candidate["parent_executable"], candidate["parent_executable_sha256"])
    if original.get("method") in ("single_rule_ablation", "executor_rule_experiment"):
        _rule_ablation(original, depth + 1)
    for field in ("keys", "parameters", "profile_sha256", "profile_crc32"):
        if definition[field] != original[field]:
            raise ValueError("rule ablation changed numeric profile: " + field)
    if Path(definition["profile_path"]).read_bytes() != Path(original["profile_path"]).read_bytes():
        raise ValueError("rule ablation numeric profile bytes differ")
    verified_profiles = candidate.get("profiles", {})
    if not isinstance(verified_profiles, dict) or not any(
            row.get("source_manifest_sha256") == parent["manifest_sha256"]
            and row.get("profile_sha256") == definition["profile_sha256"]
            and row.get("parameters") == definition["parameters"]
            for row in verified_profiles.values() if isinstance(row, dict)):
        raise ValueError("rule ablation parent profile was not verified by its build")


def _child(definition, tribe):
    route = definition["routes"][str(tribe)]
    path = _file(route["controller_manifest"], route["controller_manifest_sha256"])
    # Parse strictly before the existing numeric-profile integrity validator.
    d = _read(path)
    if (d.get("neural_actor") is not False or d.get("ppo_eligible") is not False
            or d.get("low_level_teacher") is not True or type(d.get("own_tribe")) is not int
            or d.get("method") not in ("CEM", "coordinate_reward_search", "single_rule_ablation", "executor_rule_experiment")
            or d.get("keys") != list(KEYS)):
        raise ValueError("router child is not a reward-selected rule strategy")
    if Path(d["executable"]).resolve() != Path(definition["executable"]).resolve():
        raise ValueError("router child executable path differs")
    _file(d["profile_path"], route["profile_sha256"])
    binding = load_strategy(path, executable_sha=definition["executable_sha256"])
    if binding["definition"]["profile_sha256"] != route["profile_sha256"]:
        raise ValueError("router route profile identity differs")
    if d["method"] in ("single_rule_ablation", "executor_rule_experiment"):
        _rule_ablation(d)
    return binding


def _migration_require(condition, message):
    if not condition:
        raise ValueError("executor migration: " + message)


def _migration_fitness(rows):
    return dict(games=len(rows), wins=sum(r["status"] == 1 for r in rows),
                losses=sum(r["status"] == 2 for r in rows),
                truncated=sum(r["status"] == 3 for r in rows))


def _migration_train_layout(evidence, contract, parent_sha, broad_sha, selected_sha):
    """Check actual source-runtime accounting without inventing fresh games."""
    require = _migration_require
    evaluations, train = evidence["evaluations"], evidence["train"]
    require(set(train) == {"rows", "fitness", "unique_actual_games", "new_games_from_aggregation",
            "source_runtime_game_counts", "selected_runtime_game_count", "all_histories_reused"},
            "unexpected migration aggregate fields")
    require(set(evaluations) == ROUTES and set(contract["train_by_opponent"]) == ROUTES,
            "four TRAIN groups are required")
    require(len({parent_sha, broad_sha, selected_sha}) == 3, "runtime identities must remain distinct")
    seen, rows = set(), []
    for key in sorted(ROUTES):
        e = evaluations[key]
        require(set(e) == {"indices", "rows", "audits", "fitness", "source_runtime_sha256",
                "selected_runtime_sha256", "actual_source_games", "actual_games_on_selected_runtime",
                "reuse_basis", "numeric_profile_unchanged", "new_games_from_aggregation"},
                "unexpected source evaluation fields")
        indices = e["indices"]
        require(indices == contract["train_by_opponent"][key] and len(indices) == len(set(indices)) == 9
                and not (set(indices) & set(contract["screen_excluded"])) and not (seen & set(indices)),
                "incomplete, duplicate or SCREEN TRAIN indices")
        require(len(e["rows"]) == 9 and {r["job"] for r in e["rows"]} == set(indices)
                and len({tuple(r["start_pair"]) for r in e["rows"]}) == 9,
                "TRAIN rows must contain nine actual conditions")
        for row in e["rows"]:
            require(set(row) == {"job", "tribe", "seed", "start_pair", "status", "end_frame"}
                    and type(row["tribe"]) is int and row["tribe"] == int(key), "unexpected TRAIN row fields")
            _integer(row["status"], 1, 3, "native status")
            _integer(row["end_frame"], 1, 60000, "native end frame")
        require(e["source_runtime_sha256"] == (broad_sha if key == "0" else parent_sha)
                and e["selected_runtime_sha256"] == selected_sha
                and e["actual_source_games"] == 9 and e["actual_games_on_selected_runtime"] == 0
                and e["new_games_from_aggregation"] == 0 and e["numeric_profile_unchanged"] is True,
                "false fresh or same-runtime evaluation claim")
        require(e["fitness"] == _migration_fitness(e["rows"]), "TRAIN fitness differs from actual rows")
        seen.update(indices)
        rows.extend(e["rows"])
    rows.sort(key=lambda r: r["job"])
    require(train["rows"] == rows and train["fitness"] == _migration_fitness(rows)
            and train["unique_actual_games"] == 36 and train["new_games_from_aggregation"] == 0
            and train["selected_runtime_game_count"] == 0 and train["all_histories_reused"] is True
            and train["source_runtime_game_counts"] == {broad_sha: 9, parent_sha: 27},
            "migration aggregate must disclose 36 reused source games")
    return rows


def _migration_receipts(evaluation, runtime_sha, profile_sha, checked_files):
    """Recheck pinned receipt/native terminal identity; RLO audit is separate."""
    require = _migration_require
    audits = evaluation["audits"]
    rows = {r["job"]: r for r in evaluation["rows"]}
    require(len(audits) == len(rows) == 9 and {a["job"] for a in audits} == set(rows),
            "native receipt coverage differs")
    def once(path, checksum):
        key = (str(Path(path).resolve()), checksum)
        if key not in checked_files:
            _file(path, checksum)
            checked_files.add(key)
    for audit in audits:
        path = _source(audit["receipt"])
        receipt = _read(path)
        row = rows[audit["job"]]
        require({k: receipt[k] for k in row} == row, "pinned native outcome or scenario differs")
        require(receipt.get("valid") is True and receipt.get("evaluation_valid") is True
                and receipt.get("teacher") is True and receipt.get("weight_version") == 0
                and receipt.get("own_tribe") == 1 and receipt.get("curriculum") == 2
                and receipt.get("max_frames") == 60000 and receipt.get("dagger") is False
                and receipt.get("coordinated_transfers") is False
                and receipt.get("teacher_variant") == TEACHER_VARIANT
                and receipt.get("win") is (row["status"] == 1), "receipt is not a normal builtin teacher game")
        require(receipt["policy_seed"] == 202609091000 + row["job"], "actual policy seed differs")
        command = receipt["command"]
        require("-AISELF" in command and "-AICOMMANDER" in command and "-AITEACHER" in command
                and not any(str(p).upper().startswith(("-AIVS", "-AIDAGGER", "-AITEACHER2", "-AIOPPSLOW")) for p in command),
                "native opponent or control flags differ")
        once(command[0], runtime_sha)
        binding = receipt["elf_strategy_profile"]
        require(audit["source_controller"] == {"path": binding["manifest"], "sha256": binding["manifest_sha256"]},
                "source strategy binding differs")
        d = validate_strategy(binding, executable_sha=runtime_sha)
        require(d["profile_sha256"] == profile_sha and Path(d["executable"]).resolve() == Path(command[0]).resolve(),
                "actual source profile/runtime differs")
        contract = _read(_source(audit["source_contract"]))
        require(contract["executable_sha256"] == runtime_sha
                and contract["weights_sha256"] == audit["weights_placeholder_sha256"], "source contract differs")
        once(contract["weights"], contract["weights_sha256"])
        output = path.parent
        once(output / "Jw2.log", audit["log_sha256"])
        metrics = _read(_file(output / "commander_metrics_1.json", audit["metrics_sha256"]))
        result = _read(_source(audit["native_result"]))
        require(metrics == receipt["commander_metrics"] and metrics["owner"] == 1
                and metrics["status"] == row["status"] and metrics["end_frame"] == row["end_frame"]
                and result == receipt["result"] and result["end_frame"] == row["end_frame"],
                "actual native result/metrics disagree")
        require(Path(receipt["rollout"]).resolve() == Path(audit["rollout"]["path"]).resolve()
                and audit["rollout"]["sha256"] == audit["rollout_sha256"]
                and audit["runtime_sha256"] == runtime_sha and audit["status"] == row["status"],
                "pinned source RLO audit identity differs")


def _migration_selection(definition):
    """Validate the separately named public-Primitive executor migration."""
    require = _migration_require
    e = _read(_source(definition["selection_evidence"]))
    require(set(e) == {"kind", "contract", "plan", "selected_before_screen", "screen_used_for_selection",
            "selected_at_unix_ns", "selected_at_utc", "parent_router", "candidate_build_manifest",
            "source_scope_proof", "selected_profiles", "numeric_routes_unchanged", "changed_public_opponent",
            "rule_comparison", "evaluations", "train", "outside_scope_route_policy", "completed_parity_validation",
            "actual_parity_validation_pending", "actual_screen_validation_pending", "policy_adopted"},
            "unexpected migration selection fields")
    require(e.get("kind") == "contextual_executor_scope_migration_selected_by_train36"
            and e.get("selected_before_screen") is True and e.get("screen_used_for_selection") is False
            and e.get("numeric_routes_unchanged") is True and e.get("changed_public_opponent") == 0
            and e.get("actual_parity_validation_pending") is False and e.get("actual_screen_validation_pending") is True
            and e.get("policy_adopted") is False, "selection is not a TRAIN-frozen scope migration")
    contract = _read(_source(e["contract"]))
    plan = _read(_source(e["plan"]))
    require(set(contract) == {"kind", "public_routing_input", "own_tribe", "executable", "executable_sha256",
            "parent_router", "parent_train_selection", "parent_full48", "candidate_build_manifest", "source_scope_proof",
            "train_by_opponent", "screen_excluded", "changed_public_opponent", "selection_rule",
            "fully_fresh_selected_runtime_train", "source_runtime_games_allowed_only_with_declared_scope",
            "native_parity_before_screen", "primitive_screen_indices", "primitive_screen_min_wins",
            "policy_adopted", "neural_actor", "ppo_enabled"}, "unexpected migration contract fields")
    require(plan["contract"] == e["contract"] and plan["candidate_build_manifest"] == e["candidate_build_manifest"],
            "pre-adoption plan provenance differs")
    require(contract["kind"] == "public_opponent_executor_migration_contract"
            and contract["public_routing_input"] == "public_opponent_tribe" and contract["own_tribe"] == 1
            and contract["executable_sha256"] == definition["executable_sha256"]
            and contract["changed_public_opponent"] == 0
            and contract["fully_fresh_selected_runtime_train"] is False
            and contract["source_runtime_games_allowed_only_with_declared_scope"] is True
            and contract["neural_actor"] is False and contract["ppo_enabled"] is False and contract["policy_adopted"] is False,
            "migration contract differs")
    require(e["parent_router"] == contract["parent_router"], "incumbent router pin differs")
    parent_path = _source(e["parent_router"])
    require(_read(parent_path)["method"] == "contextual_reward_selection", "unsupported migration parent")
    parent = load_router(parent_path)["definition"]
    parent_selection = _read(_source(contract["parent_train_selection"]))
    require(parent["selection_evidence"] == contract["parent_train_selection"], "incumbent TRAIN selection differs")
    parent_contract = _read(_source(parent_selection["contract"]))
    require(contract["train_by_opponent"] == parent_contract["train_by_opponent"]
            and contract["screen_excluded"] == parent_contract["screen_excluded"], "TRAIN/SCREEN split changed")
    parent_full = _read(_source(contract["parent_full48"]))
    require(e["candidate_build_manifest"] == contract["candidate_build_manifest"]
            and e["source_scope_proof"] == contract["source_scope_proof"], "scope provenance pins differ")
    build_path = _source(e["candidate_build_manifest"])
    build = _read(build_path)
    scope = _read(_source(e["source_scope_proof"]))
    require(build["kind"] == "executor_rule_experiment" and build["own_tribe"] == 1
            and build["public_opponent_tribes"] == [0]
            and build["executable_sha256"] == definition["executable_sha256"]
            and build["parent_executable_sha256"] == parent["executable_sha256"]
            and Path(build["parent_executable"]).resolve() == Path(parent["executable"]).resolve(), "executor scope differs")
    broad = _read(_source(build["broad_candidate_manifest"]))
    broad_exe = _source(build["broad_candidate"])
    require(broad["executable_sha256"] == build["broad_candidate"]["sha256"]
            and Path(broad["executable"]).resolve() == broad_exe.resolve(), "broad executor identity differs")
    require(scope["candidate_build_manifest"] == e["candidate_build_manifest"]
            and scope["own_tribe"] == 1 and scope["changed_public_opponents"] == [0]
            and scope["unchanged_public_opponents"] == [1, 2, 3] and scope["public_opponent_only"] is True
            and scope["inside_reference_runtime_sha256"] == broad["executable_sha256"]
            and scope["outside_reference_runtime_sha256"] == parent["executable_sha256"]
            and scope["actual_source_sha256"] == build["source_sha256"]
            and scope["exact_insert_removal_equals_parent"] is True
            and scope["native_fixture_scope_parity"] == build["scope_parity"], "scope proof differs")
    source = _file(build_path.with_name("ranker_ai_commander.cpp"), build["source_sha256"]).read_text(encoding="utf-8")
    baseline_source = Path(parent["executable"]).with_name("ranker_ai_commander.cpp").read_text(encoding="utf-8")
    broad_source = Path(broad["executable"]).with_name("ranker_ai_commander.cpp").read_text(encoding="utf-8")
    old_condition = "if(v.services.own_tribe==1&&ranged&&target&&u.target_id==target->id&&"
    new_condition = "if(v.services.own_tribe==1&&v.services.public_enemy_tribe==0&&ranged&&target&&u.target_id==target->id&&"
    require(broad["patch"].count(old_condition) == 1 and build["exact_patch"] == broad["patch"].replace(old_condition, new_condition)
            and source.count(build["exact_patch"]) == 1 and source.replace(build["exact_patch"], "") == baseline_source
            and broad_source.count(broad["patch"]) == 1 and broad_source.replace(broad["patch"], "") == baseline_source,
            "source is not the exact public-opponent-only scope change")
    for name, checksum in scope["includes_unchanged"].items():
        a = _file(build_path.with_name(name), checksum)
        b = Path(parent["executable"]).with_name(name)
        require(a.read_bytes() == b.read_bytes(), "teacher or numeric profile source changed")
    require(set(scope["includes_unchanged"]) == {"elf_cem_profile.inc", "ranker_ai_commander_elf_teacher.inc", "ranker_ai_commander_elf_primitive_teacher.inc"},
            "incomplete unchanged-source proof")
    require(e["selected_profiles"] == definition["routes"] == plan["selected_routes"], "selected public routes changed")
    for key in sorted(ROUTES):
        child = _read(_file(definition["routes"][key]["controller_manifest"], definition["routes"][key]["controller_manifest_sha256"]))
        old = parent["routes"][key]
        ancestry = child["provenance"]["parent_controller"]
        require(child["profile_sha256"] == old["profile_sha256"]
                and ancestry["manifest"] == old["controller_manifest"] and ancestry["manifest_sha256"] == old["controller_manifest_sha256"]
                and child["provenance"]["candidate_build_manifest"] == e["candidate_build_manifest"], "numeric route ancestry changed")
    _migration_train_layout(e, contract, parent["executable_sha256"], broad["executable_sha256"], definition["executable_sha256"])
    comparison = e["rule_comparison"]
    require(set(comparison) == {"incumbent", "candidate", "selection_metric", "strict_win_improvement",
            "wins_before_after", "gained_wins", "lost_wins", "individual_win_veto"}, "unexpected rule-selection fields")
    incumbent, candidate = comparison["incumbent"], comparison["candidate"]
    require(candidate == e["evaluations"]["0"] and incumbent["indices"] == contract["train_by_opponent"]["0"]
            and incumbent["rows"] == parent_selection["evaluations"]["0"]["rows"]
            and {a["job"]: a["receipt"]["sha256"] for a in incumbent["audits"]}
                == {a["job"]: a["receipt_sha256"] for a in parent_selection["evaluations"]["0"]["audits"]}
            and incumbent["source_runtime_sha256"] == parent["executable_sha256"]
            and incumbent["fitness"] == _migration_fitness(incumbent["rows"])
            and comparison["selection_metric"] == "TRAIN9 wins" and comparison["strict_win_improvement"] is True
            and comparison["individual_win_veto"] is False
            and comparison["wins_before_after"] == [incumbent["fitness"]["wins"], candidate["fitness"]["wins"]]
            and candidate["fitness"]["wins"] > incumbent["fitness"]["wins"], "rule failed strict TRAIN win selection")
    before = {r["job"]: r for r in incumbent["rows"]}
    require(all((r["tribe"], r["seed"], r["start_pair"]) == (before[r["job"]]["tribe"], before[r["job"]]["seed"], before[r["job"]]["start_pair"]) for r in candidate["rows"]), "paired TRAIN conditions changed")
    require(comparison["gained_wins"] == [r["job"] for r in candidate["rows"] if before[r["job"]]["status"] != 1 and r["status"] == 1]
            and comparison["lost_wins"] == [r["job"] for r in candidate["rows"] if before[r["job"]]["status"] == 1 and r["status"] != 1], "paired win changes differ")
    checked_files = set()
    _migration_receipts(incumbent, parent["executable_sha256"], definition["routes"]["0"]["profile_sha256"], checked_files)
    for key in sorted(ROUTES):
        evaluation = e["evaluations"][key]
        if key != "0":
            old = parent_selection["evaluations"][key]
            require(evaluation["rows"] == old["rows"], "outside-scope TRAIN history changed")
            require({a["job"]: a["receipt"]["sha256"] for a in evaluation["audits"]}
                    == {a["job"]: a["receipt_sha256"] for a in old["audits"]}, "outside-scope receipt identity changed")
        _migration_receipts(evaluation, evaluation["source_runtime_sha256"], definition["routes"][key]["profile_sha256"], checked_files)
    require(e["completed_parity_validation"] == plan["completed_parity_validation"], "parity validation pin differs")
    parity = _read(_source(e["completed_parity_validation"]))
    proof = _read(_source(parity["actual_audit"]))
    _source(parity["actual_summary"])
    _source(parity["original_validation_plan"])
    _source(parity["execution_sidecar"])
    require(parity["passed"] is True and proof["passed"] is True and proof["complete"] is True
            and parity["actual_summary"] == {"path": proof["probe_summary"], "sha256": proof["probe_summary_sha256"]}
            and parity["original_validation_plan"] == plan["original_validation_plan"]
            and parity["execution_sidecar"] == plan["execution_sidecar"]
            and proof["frozen_validation_plan_sha256"] == plan["original_validation_plan"]["sha256"]
            and parity["independent_performance_samples"] == proof["independent_performance_samples"] == 0
            and parity["source_runtime_sha256"] == proof["source_runtime_sha256"] == broad["executable_sha256"]
            and parity["scoped_runtime_sha256"] == proof["scoped_runtime_sha256"] == definition["executable_sha256"]
            and proof["build_manifest_sha256"] == e["candidate_build_manifest"]["sha256"]
            and {p["canonical_job"] for p in proof["pairs"]} == {1, 6, 11}
            and all(p["passed"] and p["full_rollout_file_byte_identical"] and p["deterministic_gameplay_metrics_equal"] and p["final_result_equal"] for p in proof["pairs"]), "native scope parity not established")
    selected_audits = {a["job"]: a for a in candidate["audits"]}
    for pair in proof["pairs"]:
        require(pair["source_receipt_sha256"] == selected_audits[pair["canonical_job"]]["receipt"]["sha256"]
                and pair["source_rollout_sha256"] == selected_audits[pair["canonical_job"]]["rollout"]["sha256"],
                "native parity is not tied to the selected TRAIN histories")
    require(plan["screen_policy_frozen"] is True and plan["screen_no_fitting"] is True
            and plan["required_screen_indices"] == contract["primitive_screen_indices"] == [0, 5, 10]
            and plan["screen_min_wins"] == contract["primitive_screen_min_wins"] == 2
            and contract["native_parity_before_screen"] is True,
            "frozen SCREEN validation rule changed")
    require(plan["future_aggregate_composition"] == {"broad_stationary_train9_reused": 9,
            "parent_outside_primitive_games_reused": 36, "new_scoped_primitive_screen_games": 3,
            "diagnostic_parity_independent_samples": 0}
            and plan["parent_full48_wins"] == parent_full["full"]["fitness"]["wins"]
            and plan["policy_adopted"] is False and plan["no_shared_best_or_campaign_state_writes"] is True,
            "false fresh48 or premature adoption claim")


def _selection(definition):
    """Check TRAIN-only route identity; native history audit is a separate artifact."""
    if definition["method"] == "contextual_executor_scope_migration":
        return _migration_selection(definition)
    evidence = _read(_source(definition["selection_evidence"]))
    if (evidence.get("kind") != "contextual_router_selected_by_train36"
            or evidence.get("selected_before_screen") is not True
            or evidence.get("screen_used_for_selection") is not False):
        raise ValueError("router requires selection frozen on TRAIN evidence")
    contract = _read(_source(evidence["contract"]))
    _source(evidence["plan"])
    strict_wins = "require_win_improvement" in contract or "require_win_improvement" in evidence
    if strict_wins and (contract.get("require_win_improvement") is not True
                        or evidence.get("require_win_improvement") is not True):
        raise ValueError("router strict win-improvement selection contract differs")
    if (contract.get("public_routing_input") != "public_opponent_tribe"
            or contract["executable_sha256"] != definition["executable_sha256"]
            or set(contract["train_by_opponent"]) != ROUTES
            or set(evidence["comparisons"]) != ROUTES
            or set(evidence["evaluations"]) != ROUTES):
        raise ValueError("router selection scope differs")
    seen = set()
    selected_rows = []
    for key in sorted(ROUTES):
        comparison = evidence["comparisons"][key]
        if type(comparison["selected_zero"]) is not bool:
            raise ValueError("router selection must identify a baseline choice explicitly")
        if (strict_wins and not comparison["selected_zero"]
                and comparison["candidate_evaluation"]["fitness"]["wins"]
                <= comparison["zero_evaluation"]["fitness"]["wins"]):
            raise ValueError("nonzero route did not improve TRAIN wins over zero")
        if {k: comparison["selected_profile"][k] for k in ROUTE_KEYS} != definition["routes"][key]:
            raise ValueError("router differs from the TRAIN-selected profile")
        evaluation = evidence["evaluations"][key]
        expected = contract["train_by_opponent"][key]
        indices = evaluation["indices"]
        if (len(indices) != 9 or len(set(indices)) != 9 or indices != expected
                or set(indices) & set(contract["screen_excluded"]) or seen & set(indices)):
            raise ValueError("router selection has incomplete or overlapping TRAIN scope")
        chosen = comparison["zero_evaluation" if comparison["selected_zero"] else "candidate_evaluation"]
        if evaluation != chosen:
            raise ValueError("router selected evaluation differs")
        rows = evaluation["rows"]
        if (len(rows) != 9 or {r["job"] for r in rows} != set(indices)
                or any(r["tribe"] != int(key) for r in rows)
                or len({tuple(r["start_pair"]) for r in rows}) != 9):
            raise ValueError("router TRAIN rows do not match their public opponent")
        seen.update(indices)
        selected_rows.extend(rows)
    train = evidence["train"]
    if (train["rows"] != sorted(selected_rows, key=lambda r: r["job"])
            or train["unique_actual_games"] != 36 or train["new_games_from_aggregation"] != 0):
        raise ValueError("router selection aggregate differs")


def validate_router(binding):
    """Recheck immutable router, executable, every child profile, and selection."""
    if set(binding) != {"manifest", "manifest_sha256", "definition"}:
        raise ValueError("unexpected router binding fields")
    d = _read(_file(binding["manifest"], binding["manifest_sha256"]))
    if d != binding["definition"] or set(d) != TOP_KEYS:
        raise ValueError("router definition changed or contains unsupported routing inputs")
    if (d["kind"] != KIND or d["method"] not in ("contextual_reward_selection", "contextual_executor_scope_migration")
            or d["input"] != "public_opponent_tribe" or type(d["own_tribe"]) is not int
            or d["own_tribe"] != 1 or d["schema_crc"] != 2129581458
            or d["low_level_teacher"] is not True or d["neural_actor"] is not False
            or d["ppo_enabled"] is not False or set(d["routes"]) != ROUTES):
        raise ValueError("router public-race/teacher/schema contract differs")
    exe = _file(d["executable"], d["executable_sha256"])
    if exe.name.lower() != "ranker_rebuild.exe":
        raise ValueError("router executable must be ranker_rebuild.exe")
    for key in sorted(ROUTES):
        if set(d["routes"][key]) != ROUTE_KEYS:
            raise ValueError("router route contains unsupported inputs")
        _child(d, int(key))
    _selection(d)
    return d


def load_router(path):
    path = Path(path).resolve()
    binding = dict(manifest=str(path), manifest_sha256=sha(path), definition=_read(path))
    validate_router(binding)
    return binding


def select_strategy(binding, public_opponent_tribe):
    """The only selection argument is the public opponent race, never a seed."""
    tribe = _integer(public_opponent_tribe, 0, 3, "public opponent tribe")
    return _child(validate_router(binding), tribe)


def bind_router_job(binding, job):
    if (job.get("own_tribe") != 1 or type(job.get("own_tribe")) is not int
            or job.get("curriculum") != 2 or job.get("opp_slow", 0) != 0
            or job.get("teacher_variant") != TEACHER_VARIANT
            or job.get("coordinated_transfers") is not False
            or any(job.get(k) for k in ("dagger", "teacher2", "teacher_variant2",
                                        "opponent_weights", "primary_weights", "controller_manifest"))):
        raise ValueError("router job must use own Elf and the normal builtin opponent")
    child = select_strategy(binding, job["tribe"])
    if job.get("elf_strategy_profile", child) != child:
        raise ValueError("job tries to override its public-opponent profile")
    if job.get("elf_strategy_router", binding) != binding:
        raise ValueError("job tries to override its router")
    return {**copy.deepcopy(job), "elf_strategy_router": copy.deepcopy(binding),
            "elf_strategy_profile": child}


def audit_router_receipt(row, *, check_rollout=False):
    """Verify a new receipt; default works after RLO archival using native logs."""
    import ranker_commander_eval as evaluation
    binding = row["elf_strategy_router"]
    bind_router_job(binding, row)
    d = binding["definition"]
    if (row.get("valid") is not True or row.get("evaluation_valid") is not True
            or row.get("teacher") is not True or row.get("deterministic") is not True
            or row.get("strategy_profile_verified") is not True or row.get("weight_version") != 0):
        raise ValueError("router receipt lacks a valid native teacher result")
    command = row["command"]
    if (sha(command[0]) != d["executable_sha256"] or "-AITEACHER" not in command
            or "-AITEACHERVAR:268435456" not in command
            or any(str(part).upper().startswith(("-AIVS", "-AITEACHER2", "-AIDAGGER")) for part in command)):
        raise ValueError("router receipt used a different actor or executor")
    log = (Path(row["directory"]) / "output" / "Jw2.log").read_text(encoding="utf-8", errors="replace")
    validate_strategy(row["elf_strategy_profile"], executable_sha=d["executable_sha256"], log=log)
    slots = evaluation.extract_start_slots(log)
    if (slots.get(1, {}).get("tribe") != 1 or slots.get(2, {}).get("tribe") != row["tribe"]
            or [slots[i]["map_slot"] for i in (1, 2)] != row["start_pair"]
            or re.findall(r"ai-play: Computer\(AI\) owner=(\d+) armed", log) != ["1"]):
        raise ValueError("actual public opponent or native AI ownership differs")
    evaluation.validate_transfer_mode_log(log, owner=1, enabled=False)
    result = _read(Path(row["directory"]) / "output" / "ai_selfplay_result.json")
    metrics = _read(Path(row["directory"]) / "output" / "commander_metrics_1.json")
    if (result != row.get("result") or result["end_frame"] != row["end_frame"]
            or metrics.get("owner") != 1 or metrics.get("status") != row["status"]
            or metrics.get("end_frame") != row["end_frame"]
            or row.get("win") is not (row["status"] == evaluation.WIN)):
        raise ValueError("router result/metrics do not bind the reported native outcome")
    if check_rollout:
        episode = evaluation.read_rollout(row["rollout"], teacher=True)
        try:
            vector = episode.records["vector"]
            if (episode.owner != 1 or episode.seed != row["seed"] or episode.weight_version != 0
                    or not (vector[:, 606:610] == [0, 1, 0, 0]).all()
                    or not (vector[:, 66:70] == [int(i == row["tribe"]) for i in range(4)]).all()
                    or int(episode.terminal["status"]) != row["status"]
                    or int(episode.terminal["frame"]) != row["end_frame"]):
                raise ValueError("router rollout public race or terminal differs")
        finally:
            if hasattr(episode, "close"):
                episode.close()
            elif hasattr(episode.records, "_mmap"):
                episode.records._mmap.close()
    return dict(router_manifest_sha256=binding["manifest_sha256"],
                public_opponent_tribe=row["tribe"],
                profile_sha256=row["elf_strategy_profile"]["definition"]["profile_sha256"],
                native_public_opponent_verified=True, rollout_checked=check_rollout,
                neural_actor=False, ppo_enabled=False)


def measured_jobs(binding, scenarios):
    """Use only measured environment fields from a campaign's jobs48 contract."""
    rows = scenarios["jobs48"]
    if len(rows) != 48:
        raise ValueError("router evaluation requires 48 measured scenarios")
    expected = {(t, a, b) for t in range(4) for a in range(4) for b in range(4) if a != b}
    seen, pair_seeds, jobs = set(), {}, []
    for index, row in enumerate(rows):
        tribe = _integer(row["tribe"], 0, 3, "scenario tribe")
        seed = _integer(row["seed"], 1, 0xFFFFFFFF, "scenario seed")
        pair = tuple(row["start_pair"])
        if len(pair) != 2:
            raise ValueError("scenario must have two measured start slots")
        for value in pair:
            _integer(value, 0, 3, "start slot")
        key = (tribe, *pair)
        if (key not in expected or key in seen or pair_seeds.get(pair, seed) != seed
                or row.get("curriculum", 2) != 2 or row.get("opp_slow", 0) != 0
                or row.get("max_frames", 60000) != 60000):
            raise ValueError("scenario repeats a condition or changes normal builtin settings")
        seen.add(key)
        pair_seeds[pair] = seed
        # Source actor fields are deliberately not imported. The router fixes
        # its own rule-executor identity, independent of the old campaign actor.
        jobs.append(bind_router_job(binding, dict(scenario_index=index, seed=seed, tribe=tribe, start_pair=list(pair),
            own_tribe=1, curriculum=2, max_frames=60000, opp_slow=0,
            coordinated_transfers=False, policy_seed=202609091000 + index,
            teacher_variant=TEACHER_VARIANT, dagger=False)))
    return jobs


def run_router(binding, jobs, *, install_dir, weights_placeholder, output,
               workers=4, slot_base=1000, timeout=1200.0):
    """Create fresh native evaluation receipts. Never overwrite an old cohort."""
    import ranker_commander_eval as evaluation
    _integer(workers, 1, 8, "worker count")
    _integer(slot_base, 0, 60000 - workers, "slot base")
    if not 0 < timeout < float("inf"):
        raise ValueError("invalid router timeout")
    d = validate_router(binding)
    jobs = [bind_router_job(binding, job) for job in jobs]
    if not jobs:
        raise ValueError("router evaluation needs at least one scenario")
    install_dir, output = Path(install_dir).resolve(), Path(output).resolve()
    if output == install_dir or install_dir in output.parents:
        raise ValueError("router output must be outside the installation")
    weights_placeholder = Path(weights_placeholder).resolve()
    if not weights_placeholder.is_file() or not install_dir.is_dir():
        raise ValueError("missing installation or unused weight placeholder")
    output.mkdir(parents=True, exist_ok=False)
    metadata = dict(kind=KIND, elf_strategy_router=binding, jobs=jobs,
        driver_source=dict(path=str(Path(__file__).resolve()), sha256=sha(__file__)),
        weights_placeholder=str(weights_placeholder), weights_placeholder_sha256=sha(weights_placeholder),
        weights_used_for_actions=False, low_level_teacher=True, neural_actor=False, ppo_enabled=False,
        policy_adopted=False, fully_fresh_router_cohort=True)
    (output / "plan.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    lock, pending = threading.Lock(), iter(enumerate(jobs))

    def worker(slot):
        reports = []
        while True:
            with lock:
                item = next(pending, None)
            if item is None:
                return reports
            index, job = item
            # Revalidate every dependency immediately before and after a match.
            bind_router_job(binding, job)
            row = evaluation._run_game(install_dir, weights_placeholder, output, index, job, slot,
                teacher=True, deterministic=True, timeout=timeout,
                executable=d["executable"], no_sleep=True)
            try:
                row["strategy_router_audit"] = audit_router_receipt(row, check_rollout=True)
                row["strategy_router_verified"] = True
            except (OSError, ValueError, KeyError, RuntimeError) as error:
                row.update(valid=False, evaluation_valid=False, win=False,
                           strategy_router_verified=False, reason=str(error))
            (Path(row["directory"]) / "output" / "job.json").write_text(
                json.dumps(row, indent=2) + "\n", encoding="utf-8")
            with lock:
                print(json.dumps(dict(job=index, tribe=row["tribe"], valid=row["valid"],
                    win=row.get("win", False), status=row.get("status"), end_frame=row.get("end_frame"))), flush=True)
            reports.append(row)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        reports = sum(list(pool.map(worker, range(slot_base, slot_base + min(workers, len(jobs))))), [])
    reports.sort(key=lambda row: row["job"])
    summary = evaluation.summarize_evaluation(reports, expected_games=len(jobs))
    summary.update(kind=KIND, elf_strategy_router=binding, results=reports,
                   neural_actor=False, ppo_enabled=False, policy_adopted=False,
                   fully_fresh_router_cohort=True, native_game_attempts=len(reports),
                   new_native_games=sum(row.get("strategy_router_verified") is True for row in reports))
    (output / "full.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--router", type=Path, required=True)
    parser.add_argument("--scenarios", type=Path, required=True, help="Measured campaign contract containing jobs48")
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--weights-placeholder", type=Path, required=True,
                        help="Existing file required by legacy launcher; teacher never loads it as an actor")
    parser.add_argument("--output", type=Path, required=True, help="New directory for all 48 fresh matches")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--slot-base", type=int, default=1000)
    parser.add_argument("--timeout", type=float, default=1200.0)
    parser.add_argument("--dry-run", action="store_true", help="Validate and print the complete launch plan without games")
    args = parser.parse_args(argv)
    binding = load_router(args.router)
    jobs = measured_jobs(binding, _read(args.scenarios))
    _integer(args.workers, 1, 8, "worker count")
    _integer(args.slot_base, 0, 60000 - args.workers, "slot base")
    if not 0 < args.timeout < float("inf"):
        raise ValueError("invalid router timeout")
    if not args.install_dir.is_dir() or not args.weights_placeholder.is_file():
        raise ValueError("missing installation or unused weight placeholder")
    if args.dry_run:
        print(json.dumps(dict(dry_run=True, native_games_executed=0, policy_adopted=False,
            elf_strategy_router=binding, scenario_source=dict(path=str(args.scenarios.resolve()), sha256=sha(args.scenarios)),
            jobs=jobs, weights_used_for_actions=False), indent=2))
        return 0
    summary = run_router(binding, jobs, install_dir=args.install_dir, weights_placeholder=args.weights_placeholder,
        output=args.output, workers=args.workers, slot_base=args.slot_base, timeout=args.timeout)
    return 0 if summary["valid_games"] == 48 and summary["coverage_complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
