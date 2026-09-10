"""Synthetic integrity/launch-contract tests; no native games are launched."""
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
import zlib
from unittest.mock import patch

import ranker_commander_strategy_router as router
from ranker_commander_strategy import write_strategy, sha


class StrategyRouterTest(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[3]
        self.temporary = tempfile.TemporaryDirectory(dir=root / "debug_artifacts/commander")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.exe = self.directory / "ranker_rebuild.exe"
        self.exe.write_bytes(b"synthetic executor identity; never launched")
        self.children = [write_strategy(self.directory / f"profile{i}", [0, 0, 0, 0, i, 0],
                                       self.exe, purpose="baseline" if i == 0 else "search") for i in range(4)]
        self.pairs = [(a, b) for a in range(4) for b in range(4) if a != b]
        self.scenarios = {"jobs48": [dict(seed=i + 1, tribe=t, start_pair=list(pair),
            curriculum=2, opp_slow=0, max_frames=60000, dagger=True, teacher_variant=0)
            for t in range(4) for i, pair in enumerate(self.pairs)]}
        train = {str(t): list(range(t * 12 + 3, t * 12 + 12)) for t in range(4)}
        contract = dict(public_routing_input="public_opponent_tribe", executable_sha256=sha(self.exe),
                        train_by_opponent=train, screen_excluded=[t * 12 + j for t in range(4) for j in range(3)])
        self.contract = self.write("contract.json", contract)
        self.plan = self.write("selection_plan.json", {"synthetic": True})
        routes, evaluations, comparisons, all_rows = {}, {}, {}, []
        for t, child in enumerate(self.children):
            key = str(t)
            routes[key] = dict(controller_manifest=child["manifest"],
                controller_manifest_sha256=child["manifest_sha256"],
                profile_sha256=child["definition"]["profile_sha256"])
            rows = [dict(job=i, tribe=t, start_pair=self.scenarios["jobs48"][i]["start_pair"])
                    for i in train[key]]
            ev = dict(indices=train[key], rows=rows)
            evaluations[key] = ev
            comparisons[key] = dict(selected_profile=routes[key], selected_zero=t == 0,
                                    zero_evaluation=ev, candidate_evaluation=ev)
            all_rows.extend(rows)
        self.evidence = dict(kind="contextual_router_selected_by_train36", selected_before_screen=True,
            screen_used_for_selection=False, contract=self.source(self.contract), plan=self.source(self.plan),
            comparisons=comparisons, evaluations=evaluations,
            train=dict(rows=all_rows, unique_actual_games=36, new_games_from_aggregation=0))
        self.evidence_path = self.write("selection.json", self.evidence)
        self.definition = dict(kind=router.KIND, method="contextual_reward_selection", own_tribe=1,
            schema_crc=2129581458, executable=str(self.exe), executable_sha256=sha(self.exe),
            input="public_opponent_tribe", routes=routes, selection_evidence=self.source(self.evidence_path),
            low_level_teacher=True, neural_actor=False, ppo_enabled=False)
        self.manifest = self.write("router.json", self.definition)
        self.binding = router.load_router(self.manifest)

    def write(self, name, value):
        path = self.directory / name
        path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        return path

    @staticmethod
    def source(path):
        return dict(path=str(path), sha256=sha(path))

    def reload(self, definition=None):
        self.write("router.json", definition or self.definition)
        return router.load_router(self.manifest)

    def job(self, tribe=0):
        return dict(seed=1, tribe=tribe, own_tribe=1, start_pair=[0, 1], curriculum=2,
                    max_frames=60000, coordinated_transfers=False, teacher_variant=router.TEACHER_VARIANT,
                    dagger=False)

    def rule_ablation_fixture(self):
        parent_exe = self.directory / "parent" / "ranker_rebuild.exe"
        parent_exe.parent.mkdir()
        parent_exe.write_bytes(b"synthetic parent executor; never launched")
        original = write_strategy(self.directory / "ablation_parent", self.children[1]["definition"]["parameters"],
                                  parent_exe, purpose="search")
        patch = {"before": "normal", "after": "aggressive under bounded predicate"}
        candidate = dict(kind="single_rule_ablation", own_tribe=1, schema_crc=2129581458,
            rule_teacher=True, neural_actor=False, ppo_enabled=False, exact_patch=patch, predicate="public bounded state",
            executable=str(self.exe), executable_sha256=sha(self.exe),
            parent_executable=str(parent_exe), parent_executable_sha256=sha(parent_exe),
            profiles={"verified": dict(source_manifest_sha256=original["manifest_sha256"],
                profile_sha256=original["definition"]["profile_sha256"], parameters=original["definition"]["parameters"])})
        candidate_path = self.write("ablation_build.json", candidate)
        definition = copy.deepcopy(self.children[1]["definition"])
        definition.update(method="single_rule_ablation", provenance=dict(
            unchanged_numeric_profile=True, numeric_parameters_retrained=False,
            candidate_build_manifest=self.source(candidate_path), parent_controller=original,
            rule_change=patch, rule_predicate=candidate["predicate"]))
        return definition, candidate_path, original

    def load_ablation_child(self, definition):
        path = Path(self.children[1]["manifest"])
        path.write_text(json.dumps(definition), encoding="utf-8")
        changed = copy.deepcopy(self.definition)
        changed["routes"]["1"]["controller_manifest_sha256"] = sha(path)
        changed["routes"]["1"]["profile_sha256"] = definition["profile_sha256"]
        evidence = copy.deepcopy(self.evidence)
        evidence["comparisons"]["1"]["selected_profile"] = changed["routes"]["1"]
        self.write("selection.json", evidence)
        changed["selection_evidence"] = self.source(self.evidence_path)
        return self.reload(changed)

    def test_single_rule_ablation_preserves_truthful_method_and_numeric_parent(self):
        definition, _, original = self.rule_ablation_fixture()
        binding = self.load_ablation_child(definition)
        chosen = router.bind_router_job(binding, self.job(1))["elf_strategy_profile"]["definition"]
        self.assertEqual(chosen["method"], "single_rule_ablation")
        self.assertEqual(chosen["parameters"], original["definition"]["parameters"])
        self.assertEqual(chosen["profile_sha256"], original["definition"]["profile_sha256"])
        self.assertFalse(chosen["neural_actor"])
        self.assertEqual(router.select_strategy(binding, 0), self.children[0])

    def nested_executor_fixture(self):
        intermediate, old_build, _ = self.rule_ablation_fixture()
        middle_exe = self.directory / "middle" / "ranker_rebuild.exe"
        middle_exe.parent.mkdir()
        middle_exe.write_bytes(b"synthetic intermediate executor; never launched")
        intermediate.update(executable=str(middle_exe), executable_sha256=sha(middle_exe))
        old = json.loads(old_build.read_text(encoding="utf-8"))
        old.update(executable=str(middle_exe), executable_sha256=sha(middle_exe))
        self.write(old_build.name, old)
        intermediate["provenance"]["candidate_build_manifest"] = self.source(old_build)
        middle_path = self.write("middle_controller.json", intermediate)
        middle = dict(manifest=str(middle_path), manifest_sha256=sha(middle_path), definition=intermediate)
        change = {"before": "stationary combat interrupted", "after": "preserve legal target"}
        build = dict(kind="executor_rule_experiment", own_tribe=1, schema_crc=2129581458,
            rule_teacher=True, neural_actor=False, ppo_enabled=False, exact_patch=change,
            predicate="own Elf and public Primitive opponent",
            executable=str(self.exe), executable_sha256=sha(self.exe),
            parent_executable=str(middle_exe), parent_executable_sha256=sha(middle_exe),
            profiles={"verified": dict(source_manifest_sha256=middle["manifest_sha256"],
                profile_sha256=intermediate["profile_sha256"], parameters=intermediate["parameters"])})
        build_path = self.write("executor_build.json", build)
        definition = copy.deepcopy(self.children[1]["definition"])
        definition.update(method="executor_rule_experiment", provenance=dict(
            unchanged_numeric_profile=True, numeric_parameters_retrained=False,
            parent_controller=middle, candidate_build_manifest=self.source(build_path),
            rule_change=change, rule_predicate=build["predicate"]))
        return definition, build_path, old_build

    def test_executor_experiment_validates_nested_ablation_without_relabeling_as_cem(self):
        definition, _, _ = self.nested_executor_fixture()
        chosen = router.bind_router_job(self.load_ablation_child(definition), self.job(1))
        d = chosen["elf_strategy_profile"]["definition"]
        self.assertEqual(d["method"], "executor_rule_experiment")
        self.assertEqual(d["provenance"]["parent_controller"]["definition"]["method"], "single_rule_ablation")
        self.assertFalse(d["neural_actor"])
        self.assertFalse(d["ppo_eligible"])

    def test_executor_experiment_rechecks_deep_parent_build(self):
        definition, _, old_build = self.nested_executor_fixture()
        self.load_ablation_child(definition)
        old_build.write_bytes(old_build.read_bytes() + b" ")
        with self.assertRaisesRegex(ValueError, "dependency changed"):
            self.load_ablation_child(definition)

    def test_executor_experiment_rejects_false_build_identity_and_numeric_ancestry(self):
        definition, build_path, _ = self.nested_executor_fixture()
        build = json.loads(build_path.read_text(encoding="utf-8"))
        for field, value in (("kind", "single_rule_ablation"), ("neural_actor", True),
                             ("parent_executable_sha256", sha(self.exe)), ("profiles", {})):
            edited = copy.deepcopy(definition)
            self.write(build_path.name, {**build, field: value})
            edited["provenance"]["candidate_build_manifest"] = self.source(build_path)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.load_ablation_child(edited)
        self.write(build_path.name, build)
        definition["provenance"]["numeric_parameters_retrained"] = True
        with self.assertRaisesRegex(ValueError, "unchanged-profile provenance"):
            self.load_ablation_child(definition)

    def test_executor_experiment_bounds_recursive_ancestry(self):
        definition, _, _ = self.nested_executor_fixture()
        with self.assertRaisesRegex(ValueError, "exceeds 16"):
            router._rule_ablation(definition, depth=16)

    def migration_receipt_fixture(self):
        """Synthetic metadata only; RLO parsing remains in the separate audit."""
        contract = self.write("native_contract.json", dict(executable_sha256=sha(self.exe),
            weights=str(self.exe), weights_sha256=sha(self.exe)))
        rows, audits = [], []
        for job in range(9):
            output = self.directory / f"native{job}" / "output"
            output.mkdir(parents=True)
            row = dict(job=job, tribe=0, seed=job + 1, start_pair=list(self.pairs[job]), status=1, end_frame=1000)
            metrics = dict(owner=1, status=1, end_frame=1000)
            result = dict(end_frame=1000, reason="synthetic elimination")
            for name, value in (("commander_metrics_1.json", metrics), ("ai_selfplay_result.json", result)):
                (output / name).write_text(json.dumps(value), encoding="utf-8")
            (output / "Jw2.log").write_text("Synthetic log; never a native match\n", encoding="utf-8")
            rollout = output / "commander.rlo"
            rollout.write_bytes(b"synthetic non-RLO metadata fixture")
            receipt = dict(row, own_tribe=1, valid=True, evaluation_valid=True, teacher=True, weight_version=0,
                curriculum=2, max_frames=60000, dagger=False, coordinated_transfers=False,
                teacher_variant=router.TEACHER_VARIANT, win=True, policy_seed=202609091000 + job,
                command=[str(self.exe), "-AISELF", "-AICOMMANDER", "-AITEACHER"],
                elf_strategy_profile=self.children[0], commander_metrics=metrics, result=result, rollout=str(rollout))
            path = output / "job.json"
            path.write_text(json.dumps(receipt), encoding="utf-8")
            audits.append(dict(job=job, receipt=self.source(path),
                source_controller=dict(path=self.children[0]["manifest"], sha256=self.children[0]["manifest_sha256"]),
                source_contract=self.source(contract), weights_placeholder_sha256=sha(self.exe),
                log_sha256=sha(output / "Jw2.log"), metrics_sha256=sha(output / "commander_metrics_1.json"),
                native_result=self.source(output / "ai_selfplay_result.json"), rollout=self.source(rollout),
                rollout_sha256=sha(rollout), runtime_sha256=sha(self.exe), status=1))
            rows.append(row)
        return dict(rows=rows, audits=audits)

    def test_migration_rechecks_all_pinned_receipts_and_native_outcomes(self):
        evaluation = self.migration_receipt_fixture()
        router._migration_receipts(evaluation, sha(self.exe), self.children[0]["definition"]["profile_sha256"], set())
        path = Path(evaluation["audits"][4]["receipt"]["path"])
        changed = json.loads(path.read_text(encoding="utf-8"))
        changed["status"] = 2
        path.write_text(json.dumps(changed), encoding="utf-8")
        evaluation["audits"][4]["receipt"] = self.source(path)
        with self.assertRaisesRegex(ValueError, "native outcome"):
            router._migration_receipts(evaluation, sha(self.exe), self.children[0]["definition"]["profile_sha256"], set())

    def test_migration_rejects_modified_native_metrics_even_with_updated_pin(self):
        evaluation = self.migration_receipt_fixture()
        audit = evaluation["audits"][8]
        path = Path(audit["receipt"]["path"]).with_name("commander_metrics_1.json")
        path.write_text(json.dumps(dict(owner=1, status=2, end_frame=1000)), encoding="utf-8")
        audit["metrics_sha256"] = sha(path)
        with self.assertRaisesRegex(ValueError, "native result/metrics"):
            router._migration_receipts(evaluation, sha(self.exe), self.children[0]["definition"]["profile_sha256"], set())

    def test_migration_rejects_nonbuiltin_native_control_and_profile_swap(self):
        evaluation = self.migration_receipt_fixture()
        audit = evaluation["audits"][0]
        path = Path(audit["receipt"]["path"])
        original = json.loads(path.read_text(encoding="utf-8"))
        for edit in (dict(curriculum=1), dict(command=original["command"] + ["-AIVS"]),
                     dict(elf_strategy_profile=self.children[1])):
            changed = {**original, **edit}
            path.write_text(json.dumps(changed), encoding="utf-8")
            audit["receipt"] = self.source(path)
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                router._migration_receipts(evaluation, sha(self.exe), self.children[0]["definition"]["profile_sha256"], set())

    def test_single_rule_ablation_rejects_valid_but_changed_numeric_profile(self):
        definition, _, _ = self.rule_ablation_fixture()
        definition["parameters"][4] += 1
        raw = ("JWELFSTRAT1\n" + " ".join(format(v, ".17g") for v in definition["parameters"]) + "\n").encode("ascii")
        Path(definition["profile_path"]).write_bytes(raw)
        definition["profile_sha256"] = sha(definition["profile_path"])
        definition["profile_crc32"] = zlib.crc32(raw)
        with self.assertRaisesRegex(ValueError, "changed numeric profile"):
            self.load_ablation_child(definition)

    def test_single_rule_ablation_rejects_unbound_build_or_parent_identity(self):
        definition, candidate_path, original = self.rule_ablation_fixture()
        candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
        bad_candidate = {**candidate, "executable_sha256": original["definition"]["executable_sha256"]}
        self.write(candidate_path.name, bad_candidate)
        definition["provenance"]["candidate_build_manifest"] = self.source(candidate_path)
        with self.assertRaisesRegex(ValueError, "executable differs"):
            self.load_ablation_child(definition)
        self.write(candidate_path.name, candidate)
        definition["provenance"]["candidate_build_manifest"] = self.source(candidate_path)
        parent_exe = Path(original["definition"]["executable"])
        parent_exe.write_bytes(parent_exe.read_bytes() + b"tamper")
        with self.assertRaisesRegex(ValueError, "dependency changed"):
            self.load_ablation_child(definition)

    def test_single_rule_ablation_requires_explicit_verified_unchanged_ancestry(self):
        definition, candidate_path, _ = self.rule_ablation_fixture()
        for field, bad in (("unchanged_numeric_profile", False), ("numeric_parameters_retrained", True),
                           ("parent_controller", None), ("rule_predicate", "different predicate")):
            edited = copy.deepcopy(definition)
            edited["provenance"][field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.load_ablation_child(edited)
        candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
        candidate["profiles"] = {}
        self.write(candidate_path.name, candidate)
        definition["provenance"]["candidate_build_manifest"] = self.source(candidate_path)
        with self.assertRaisesRegex(ValueError, "not verified by its build"):
            self.load_ablation_child(definition)

    def test_public_race_only_and_baseline_child_is_allowed(self):
        for tribe in range(4):
            self.assertEqual(router.select_strategy(self.binding, tribe), self.children[tribe])
            job = router.bind_router_job(self.binding, self.job(tribe))
            alternate = {**self.job(tribe), "seed": 987, "start_pair": [3, 2]}
            self.assertEqual(job["elf_strategy_profile"], router.bind_router_job(self.binding, alternate)["elf_strategy_profile"])
            self.assertEqual(job["elf_strategy_router"], self.binding)
        self.assertEqual(router.select_strategy(self.binding, 0)["definition"]["purpose"], "baseline")
        for tribe in (-1, 4, True, "0", 1.0):
            with self.subTest(tribe=tribe), self.assertRaises(ValueError):
                router.select_strategy(self.binding, tribe)

    def test_schema_cannot_add_seed_or_nn_routing(self):
        edits = [dict(input="seed"), dict(own_tribe=2), dict(own_tribe=True),
                 dict(neural_actor=True), dict(ppo_enabled=True), dict(schema_crc=0), dict(seed_routes={})]
        for edit in edits:
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                self.reload({**self.definition, **edit})
        changed = copy.deepcopy(self.definition)
        changed["routes"]["0"]["seed"] = 9
        with self.assertRaises(ValueError):
            self.reload(changed)
        changed = copy.deepcopy(self.definition)
        del changed["routes"]["3"]
        with self.assertRaises(ValueError):
            self.reload(changed)
        self.manifest.write_text('{"kind":"x","kind":"y"}', encoding="utf-8")
        with self.assertRaises(ValueError):
            router.load_router(self.manifest)

    def test_all_files_and_cached_binding_are_revalidated(self):
        for path in (self.exe, Path(self.children[3]["definition"]["profile_path"]),
                     Path(self.children[2]["manifest"]), self.evidence_path, self.contract, self.plan, self.manifest):
            original = path.read_bytes()
            path.write_bytes(original + b" ")
            with self.subTest(path=path), self.assertRaises(ValueError):
                router.validate_router(self.binding)
            path.write_bytes(original)
        changed = copy.deepcopy(self.binding)
        changed["definition"]["routes"]["0"] = changed["definition"]["routes"]["1"]
        with self.assertRaises(ValueError):
            router.validate_router(changed)

    def test_train_selection_identity_and_scope_are_required(self):
        for edit in (dict(screen_used_for_selection=True), dict(selected_before_screen=False)):
            self.write("selection.json", {**self.evidence, **edit})
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                self.reload({**self.definition, "selection_evidence": self.source(self.evidence_path)})
        changed = copy.deepcopy(self.evidence)
        changed["comparisons"]["0"]["selected_profile"] = changed["comparisons"]["1"]["selected_profile"]
        self.write("selection.json", changed)
        with self.assertRaises(ValueError):
            self.reload({**self.definition, "selection_evidence": self.source(self.evidence_path)})
        changed = copy.deepcopy(self.evidence)
        changed["evaluations"]["0"]["indices"] = [0] + changed["evaluations"]["0"]["indices"][1:]
        self.write("selection.json", changed)
        with self.assertRaises(ValueError):
            self.reload({**self.definition, "selection_evidence": self.source(self.evidence_path)})

    def test_jobs_cannot_override_selected_actor(self):
        for edit in (dict(own_tribe=2), dict(opp_slow=2), dict(curriculum=0), dict(dagger=True),
                     dict(teacher_variant=0), dict(teacher2=True), dict(opponent_weights="other.bin"),
                     dict(primary_weights="other.bin"), dict(coordinated_transfers=True),
                     dict(elf_strategy_profile=self.children[1])):
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                router.bind_router_job(self.binding, {**self.job(), **edit})

    def test_optional_strict_gate_rejects_equal_win_nonzero(self):
        contract = json.loads(self.contract.read_text(encoding="utf-8"))
        contract["require_win_improvement"] = True
        self.write("contract.json", contract)
        evidence = copy.deepcopy(self.evidence)
        evidence["contract"] = self.source(self.contract)
        evidence["require_win_improvement"] = True
        for key, comparison in evidence["comparisons"].items():
            comparison["candidate_evaluation"] = copy.deepcopy(comparison["candidate_evaluation"])
            comparison["zero_evaluation"] = copy.deepcopy(comparison["zero_evaluation"])
            comparison["candidate_evaluation"]["fitness"] = {"wins": 4}
            comparison["zero_evaluation"]["fitness"] = {"wins": 3}
            evidence["evaluations"][key] = comparison["zero_evaluation" if comparison["selected_zero"] else "candidate_evaluation"]
        self.write("selection.json", evidence)
        router.load_router(self.write("router.json", {**self.definition, "selection_evidence": self.source(self.evidence_path)}))
        evidence["comparisons"]["1"]["candidate_evaluation"]["fitness"]["wins"] = 3
        self.write("selection.json", evidence)
        with self.assertRaises(ValueError):
            self.reload({**self.definition, "selection_evidence": self.source(self.evidence_path)})

    def test_measured_48_and_dry_run_do_not_launch_or_write_outputs(self):
        jobs = router.measured_jobs(self.binding, self.scenarios)
        self.assertEqual(len(jobs), 48)
        self.assertTrue(all(j["teacher_variant"] == router.TEACHER_VARIANT and not j["dagger"] for j in jobs))
        changed = copy.deepcopy(self.scenarios)
        changed["jobs48"][1] = changed["jobs48"][0]
        with self.assertRaises(ValueError):
            router.measured_jobs(self.binding, changed)
        scenarios_path = self.write("scenarios.json", self.scenarios)
        output = self.directory / "not_created"
        with patch.object(router, "run_router", side_effect=AssertionError("dry run launched")), contextlib.redirect_stdout(io.StringIO()) as stream:
            self.assertEqual(router.main(["--router", str(self.manifest), "--scenarios", str(scenarios_path),
                "--install-dir", str(self.directory), "--weights-placeholder", str(self.exe),
                "--output", str(output), "--dry-run"]), 0)
        self.assertFalse(output.exists())
        self.assertEqual(json.loads(stream.getvalue())["native_games_executed"], 0)

    def test_metadata_audit_checks_actual_opponent_without_rollout(self):
        child = self.children[0]["definition"]
        output = self.directory / "receipt" / "output"
        output.mkdir(parents=True)
        log = (f"[ElfStrategyProfile] crc={child['profile_crc32']:08x} values="
               + ",".join(format(v, ".17g") for v in child["parameters"]) + f" path={child['profile_path']}\n"
               + "start-slots: owner=1 state=1 map_slot=0 tribe=1\n"
               + "start-slots: owner=2 state=1 map_slot=1 tribe=0\n"
               + "ai-play: Computer(AI) owner=1 armed\n"
               + "ai-commander: coordinated-transfers owner=1 enabled=0\n")
        (output / "Jw2.log").write_text(log, encoding="utf-8")
        result = {"end_frame": 20000, "reason": "elimination"}
        (output / "ai_selfplay_result.json").write_text(json.dumps(result), encoding="utf-8")
        (output / "commander_metrics_1.json").write_text(json.dumps(dict(owner=1, status=1, end_frame=20000)), encoding="utf-8")
        row = {**router.bind_router_job(self.binding, self.job()), "directory": str(output.parent),
               "valid": True, "evaluation_valid": True, "teacher": True, "deterministic": True,
               "strategy_profile_verified": True, "weight_version": 0,
               "status": 1, "win": True, "end_frame": 20000, "result": result,
               "command": [str(self.exe), "-AITEACHER", "-AITEACHERVAR:268435456"]}
        audit = router.audit_router_receipt(row)
        self.assertFalse(audit["rollout_checked"])
        self.assertTrue(audit["native_public_opponent_verified"])
        for bad_log in (log.replace("map_slot=1 tribe=0", "map_slot=1 tribe=3"),
                        log + "ai-play: Computer(AI) owner=2 armed\n",
                        log.replace("values=0,", "values=1,")):
            (output / "Jw2.log").write_text(bad_log, encoding="utf-8")
            with self.assertRaises(ValueError):
                router.audit_router_receipt(row)

    def test_native_driver_dispatches_per_job_without_mutating_dependencies(self):
        import ranker_commander_eval as evaluation
        install = self.directory / "install"
        install.mkdir()
        output = self.directory / "fresh"
        calls = []

        def fake_game(install_dir, weights, job_root, index, job, slot, **kwargs):
            calls.append((index, job, slot, kwargs))
            self.assertTrue(kwargs["teacher"])
            self.assertTrue(kwargs["deterministic"])
            self.assertEqual(kwargs["executable"], str(self.exe))
            self.assertEqual(job["elf_strategy_profile"], self.children[job["tribe"]])
            game = Path(job_root) / f"game_{index:05d}"
            (game / "output").mkdir(parents=True)
            return {**job, "job": index, "directory": str(game), "valid": True,
                    "evaluation_valid": True, "win": True, "status": 1, "end_frame": 20000,
                    "deterministic": True}

        with patch.object(evaluation, "_run_game", side_effect=fake_game), \
                patch.object(router, "audit_router_receipt", return_value={"synthetic_probe": True}) as audit, \
                contextlib.redirect_stdout(io.StringIO()):
            result = router.run_router(self.binding, [self.job(i) for i in range(4)],
                install_dir=install, weights_placeholder=self.exe, output=output, workers=2, slot_base=900)
        self.assertEqual(len(calls), 4)
        self.assertEqual(audit.call_count, 4)
        self.assertTrue(all(c.kwargs == {"check_rollout": True} for c in audit.call_args_list))
        self.assertTrue(all(900 <= c[2] < 902 for c in calls))
        self.assertEqual(result["new_native_games"], 4)
        self.assertEqual(result["valid_games"], 4)
        self.assertFalse(result["policy_adopted"])
        self.assertEqual(router.load_router(self.manifest), self.binding)
        with self.assertRaises(FileExistsError):
            router.run_router(self.binding, [self.job()], install_dir=install,
                weights_placeholder=self.exe, output=output)


class MigrationLayoutTest(unittest.TestCase):
    def fixture(self):
        pairs = [(a, b) for a in range(4) for b in range(4) if a != b]
        train_by = {str(t): list(range(t * 12 + 3, t * 12 + 12)) for t in range(4)}
        contract = dict(train_by_opponent=train_by, screen_excluded=[t * 12 + i for t in range(4) for i in range(3)])
        evaluations, all_rows = {}, []
        for key, indices in train_by.items():
            rows = [dict(job=j, tribe=int(key), seed=j + 1, start_pair=list(pairs[j % 12]),
                         status=1 if j % 3 else 2, end_frame=1000) for j in indices]
            evaluations[key] = dict(indices=indices, rows=rows, audits=[], fitness=router._migration_fitness(rows),
                source_runtime_sha256="b" * 64 if key == "0" else "a" * 64, selected_runtime_sha256="c" * 64,
                actual_source_games=9, actual_games_on_selected_runtime=0, reuse_basis="synthetic scope proof",
                numeric_profile_unchanged=True, new_games_from_aggregation=0)
            all_rows.extend(rows)
        evidence = dict(evaluations=evaluations, train=dict(rows=all_rows, fitness=router._migration_fitness(all_rows),
            unique_actual_games=36, new_games_from_aggregation=0, source_runtime_game_counts={"a" * 64: 27, "b" * 64: 9},
            selected_runtime_game_count=0, all_histories_reused=True))
        return evidence, contract

    def check(self, evidence, contract):
        return router._migration_train_layout(evidence, contract, "a" * 64, "b" * 64, "c" * 64)

    def test_actual_source_runtimes_remain_distinct_from_selected_runtime(self):
        evidence, contract = self.fixture()
        self.assertEqual(len(self.check(evidence, contract)), 36)

    def test_false_fresh_or_same_runtime_accounting_is_rejected(self):
        for edit in (dict(actual_games_on_selected_runtime=9), dict(source_runtime_sha256="c" * 64),
                     dict(new_games_from_aggregation=9)):
            evidence, contract = self.fixture()
            evidence["evaluations"]["0"].update(edit)
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                self.check(evidence, contract)
        evidence, contract = self.fixture()
        evidence["train"]["fully_fresh_router_cohort"] = True
        with self.assertRaisesRegex(ValueError, "unexpected migration aggregate"):
            self.check(evidence, contract)

    def test_incomplete_duplicate_or_screen_conditions_are_rejected(self):
        for mode in ("missing", "duplicate", "screen", "wrong_opponent"):
            evidence, contract = self.fixture()
            if mode == "missing":
                evidence["evaluations"]["1"]["rows"].pop()
            elif mode == "duplicate":
                evidence["evaluations"]["2"]["rows"][1] = evidence["evaluations"]["2"]["rows"][0]
            elif mode == "screen":
                contract["screen_excluded"].append(evidence["evaluations"]["0"]["indices"][0])
            else:
                evidence["evaluations"]["0"]["rows"][0]["tribe"] = 3
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.check(evidence, contract)

    def test_declared_wins_must_equal_actual_row_statuses(self):
        evidence, contract = self.fixture()
        evidence["evaluations"]["0"]["fitness"]["wins"] += 1
        with self.assertRaisesRegex(ValueError, "fitness"):
            self.check(evidence, contract)


if __name__ == "__main__":
    unittest.main()
