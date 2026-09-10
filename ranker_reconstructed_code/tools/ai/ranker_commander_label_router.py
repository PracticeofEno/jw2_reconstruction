"""A public-opponent teacher router used only for native DAgger labels.

Ordinary same-runtime routers retain their original validation. A separately
named model-only runtime migration retains the original teacher profile
bindings and records the current executable independently. It makes no new
reward-selection, gameplay, or adoption claim and is rejected by the ordinary
strategy router.
"""
from pathlib import Path
import shlex

import ranker_commander_strategy_router as router
from ranker_commander_strategy import validate_strategy

KIND = "commander_dagger_label_runtime_router"
METHOD = "native_model_runtime_migration"
TOP_KEYS = {"kind", "method", "purpose", "own_tribe", "schema_crc", "input",
            "executable", "executable_sha256", "source_router", "routes",
            "model_only_migration", "new_performance_games", "policy_adopted"}
PROOF_KEYS = {"kind", "label_only", "source_router", "source_runtime", "target_runtime",
              "source_response", "target_response", "source_model_object", "target_model_object",
              "common_link_inputs", "source_model", "target_model", "source_replacements",
              "model_header", "build_manifest", "native_model_parity", "new_games",
              "teacher_code_changed", "engine_changed", "numeric_profiles_changed"}


def _require(condition, message):
    if not condition:
        raise ValueError("label runtime migration: " + message)


def _binding(path):
    path = Path(path).resolve()
    return dict(manifest=str(path), manifest_sha256=router.sha(path), definition=router._read(path))


def _response(path):
    # Frozen GCC response files use individually quoted absolute forward-slash
    # paths. Parse the argument list, never execute arbitrary response contents.
    return shlex.split(Path(path).read_text(encoding="utf-8"), posix=True)


def _validate_model_only_proof(definition, parent):
    p = router._read(router._source(definition["model_only_migration"]))
    _require(set(p) == PROOF_KEYS and p["kind"] == "commander_model_only_label_runtime_migration"
             and p["label_only"] is True and p["new_games"] == 0
             and p["teacher_code_changed"] is False and p["engine_changed"] is False
             and p["numeric_profiles_changed"] is False, "invalid model-only proof contract")
    _require(p["source_router"] == definition["source_router"], "source router proof differs")
    old_exe = router._source(p["source_runtime"])
    new_exe = router._source(p["target_runtime"])
    _require(p["source_runtime"]["sha256"] == parent["executable_sha256"]
             and old_exe.resolve() == Path(parent["executable"]).resolve()
             and p["target_runtime"]["sha256"] == definition["executable_sha256"]
             and new_exe.resolve() == Path(definition["executable"]).resolve()
             and p["source_runtime"]["sha256"] != p["target_runtime"]["sha256"], "runtime identities differ")
    old_response = _response(router._source(p["source_response"]))
    new_response = _response(router._source(p["target_response"]))
    old_obj = router._source(p["source_model_object"])
    new_obj = router._source(p["target_model_object"])
    replacements = {str(old_obj).replace("\\", "/"): str(new_obj).replace("\\", "/"),
                    str(old_exe).replace("\\", "/"): str(new_exe).replace("\\", "/")}
    _require(all(old_response.count(key) == 1 for key in replacements)
             and [replacements.get(arg, arg) for arg in old_response] == new_response,
             "link changes are not limited to model object and output")
    # Every other explicit file linked into both executables must be identical,
    # including the CommanderTeacherAction/executor object and engine objects.
    common = [str(Path(arg).resolve()) for arg in old_response
              if arg not in replacements and Path(arg).is_absolute() and Path(arg).is_file()]
    pins = p["common_link_inputs"]
    _require(len(common) == len(set(common)) and {str(Path(x["path"]).resolve()) for x in pins} == set(common)
             and len(pins) == len(common) and any(Path(x).name == "ranker_ai_commander.obj" for x in common),
             "incomplete or duplicate common native input proof")
    for pin in pins:
        router._source(pin)
    build = router._read(router._source(p["build_manifest"]))
    _require(build["parent_executable_sha256"] == p["source_runtime"]["sha256"]
             and build["executable_sha256"] == p["target_runtime"]["sha256"]
             and build["parent_model_object_sha256"] == p["source_model_object"]["sha256"]
             and build["native_model_object_sha256"] == p["target_model_object"]["sha256"]
             and build["games_launched"] == 0 and build["deployed"] is False,
             "model build provenance differs")
    original = router._source(p["source_model"]).read_text(encoding="utf-8")
    changed = router._source(p["target_model"]).read_text(encoding="utf-8")
    edits = router._read(router._source(p["source_replacements"]))
    _require(p["source_model"]["sha256"] == build["base_native_model_source_sha256"]
             and p["target_model"]["sha256"] == build["native_model_source_sha256"]
             and p["source_replacements"]["sha256"] == build["exact_source_replacements_sha256"],
             "model source pins differ")
    for edit in reversed(edits):
        _require(set(edit) == {"before", "after"} and changed.count(edit["after"]) == 1,
                 "ambiguous model source restoration")
        changed = changed.replace(edit["after"], edit["before"])
    _require(changed == original, "model source does not restore its parent")
    header = router._source(p["model_header"])
    _require(header.name == "ranker_ai_commander_model.h", "unexpected model ABI header")
    parity = router._read(router._source(p["native_model_parity"]))
    _require(parity["passed"] is True and parity["executable_sha256"] == p["target_runtime"]["sha256"]
             and parity["native_model_sha256"] == p["target_model"]["sha256"]
             and parity["format2_and_zero3_parent_native_bytes_exact"] is True
             and parity["nonelf_native_bytes_exact"] is True
             and parity["skill_logits_and_critic_exact"] is True and parity["games_launched"] == 0,
             "native model compatibility proof differs")
    return p


def validate_label_router(binding):
    """Validate a label-only migration or an unchanged ordinary router."""
    if binding.get("definition", {}).get("kind") != KIND:
        return router.validate_router(binding)
    _require(set(binding) == {"manifest", "manifest_sha256", "definition"}, "unexpected binding fields")
    d = router._read(router._file(binding["manifest"], binding["manifest_sha256"]))
    _require(d == binding["definition"] and set(d) == TOP_KEYS, "manifest changed or extra routing fields")
    _require(d["method"] == METHOD and d["purpose"] == "dagger_labels_only"
             and type(d["own_tribe"]) is int and d["own_tribe"] == 1
             and d["schema_crc"] == 2129581458 and d["input"] == "public_opponent_tribe"
             and d["new_performance_games"] == 0 and d["policy_adopted"] is False,
             "invalid label-only contract")
    exe = router._file(d["executable"], d["executable_sha256"])
    _require(exe.name.lower() == "ranker_rebuild.exe", "wrong native executable filename")
    source_path = router._source(d["source_router"])
    source = router._read(source_path)
    # A single ordinary reward-selected source is permitted. No recursive
    # label migration or executor migration chain can disguise changed rules.
    _require(source.get("kind") == router.KIND and source.get("method") == "contextual_reward_selection"
             and source_path.resolve() != Path(binding["manifest"]).resolve(), "source is not a finite ordinary teacher router")
    parent = router.load_router(source_path)["definition"]
    _require(d["routes"] == parent["routes"] and set(d["routes"]) == router.ROUTES,
             "numeric public-opponent routes changed")
    _validate_model_only_proof(d, parent)
    return d


def load_label_router(path):
    binding = _binding(path)
    validate_label_router(binding)
    return binding


def select_label_strategy(binding, public_opponent_tribe):
    tribe = router._integer(public_opponent_tribe, 0, 3, "public opponent tribe")
    d = validate_label_router(binding)
    if d["kind"] != KIND:
        return router.select_strategy(binding, tribe)
    parent = router.load_router(router._source(d["source_router"]))
    return router.select_strategy(parent, tribe)


def validate_label_profile(binding, public_opponent_tribe, selected, *, executable_sha, log=None):
    """Bind actual runtime separately from the unchanged source profile runtime."""
    d = validate_label_router(binding)
    if d["executable_sha256"] != executable_sha:
        raise ValueError("DAgger label router runtime differs from the actor runtime")
    expected = select_label_strategy(binding, public_opponent_tribe)
    if selected != expected:
        raise ValueError("DAgger label profile differs from its public-opponent route")
    source_runtime = executable_sha
    if d["kind"] == KIND:
        source_runtime = router._read(router._source(d["source_router"]))["executable_sha256"]
    # The actual process still reads the exact original profile path/CRC/values.
    # Its executable difference was established independently above.
    return validate_strategy(selected, executable_sha=source_runtime, log=log)
