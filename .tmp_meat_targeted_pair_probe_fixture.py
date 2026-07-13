"""Pure fixture matrix for .tmp_meat_targeted_pair_probe.py."""

import importlib.util
import pathlib


def load_target():
    path = pathlib.Path(__file__).with_name(
        ".tmp_meat_targeted_pair_probe.py")
    spec = importlib.util.spec_from_file_location(
        "ranker_targeted_meat_pair_fixture_target", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load target: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PROBE = load_target()


def state(**overrides):
    row = {
        "slot": 63,
        "list": "active",
        "type": 32,
        "owner": 1,
        "area_marker_flags": 0,
        "area_marker_high": False,
        "type_flags": 0x22FB,
        "max_health": 120,
        "health": 120,
        "action_mode": 0,
        "cargo": 0,
        "command_state": 1,
        "x": 256,
        "y": 2016,
    }
    row.update(overrides)
    return row


def pair(original=None, rebuild=None):
    original = state() if original is None else original
    rebuild = dict(original) if rebuild is None else rebuild
    return {"frame": 100, "original": original, "rebuild": rebuild}


def main():
    matched, _ = PROBE.condition_matches("snapshot", None, pair())
    assert matched

    marked = state(
        area_marker_flags=0x80000001, area_marker_high=True)
    matched, _ = PROBE.condition_matches("marker", None, pair(marked))
    assert matched
    unmarked = state(type_flags=0x800022FB)
    matched, _ = PROBE.condition_matches("marker", None, pair(unmarked))
    assert not matched, "type_flags bit 31 must not substitute for marker bit"

    damaged = state(health=111, action_mode=124)
    matched, _ = PROBE.condition_matches("damaged", None, pair(damaged))
    assert matched

    before = state(health=111, action_mode=124, cargo=0)
    after = state(health=112, action_mode=123, cargo=0)
    matched, detail = PROBE.condition_matches(
        "consume", pair(before), pair(after))
    assert matched
    assert detail["original_transition"] == {
        "action_delta": -1,
        "health_delta": 1,
        "cargo_delta": 0,
        "health_before": 111,
        "health_after": 112,
        "max_health": 120,
        "matched": True,
    }

    contaminated = state(health=112, action_mode=123, cargo=1)
    matched, _ = PROBE.condition_matches(
        "consume", pair(before), pair(contaminated))
    assert not matched, "cargo changes must invalidate consumption"

    full_before = state(health=120, action_mode=124)
    full_after = state(health=121, action_mode=123)
    matched, _ = PROBE.condition_matches(
        "consume", pair(full_before), pair(full_after))
    assert not matched, "consumption requires a genuinely damaged unit"

    divergent = state(health=110)
    matched, _ = PROBE.condition_matches(
        "damaged", None, pair(damaged, divergent))
    assert not matched, "peer state mismatch cannot satisfy a condition"

    print(
        "MEAT_TARGETED_PAIR_FIXTURE_PASS "
        "snapshot marker/raw0c damaged consume=-1/+1 cargo=0 parity=yes")


if __name__ == "__main__":
    main()
