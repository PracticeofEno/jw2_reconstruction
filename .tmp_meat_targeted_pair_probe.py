"""Read-only exact-frame unit-state probe for the targeted meat P2P harness.

This intentionally reuses the proven readers/history join from
``.tmp_meat_live_probe.py`` without changing that shared fixture.  It waits
for one semantic condition on one runtime slot and reports both peers from a
finalized, same-simulation-frame pair.
"""

import argparse
import importlib.util
import json
import pathlib
import time


def load_meat_probe_module():
    path = pathlib.Path(__file__).with_name(".tmp_meat_live_probe.py")
    spec = importlib.util.spec_from_file_location(
        "ranker_targeted_meat_live_probe", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load shared meat probe: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


MEAT = load_meat_probe_module()


def state_projection(row):
    if row is None:
        return None
    return {
        "slot": row["slot"],
        "list": row["list"],
        "type": row["type"],
        "owner": row["owner"],
        "area_marker_flags": row["area_marker_flags"],
        "area_marker_high": bool(row["area_marker_flags"] & 0x80000000),
        "type_flags": row["type_flags"],
        "max_health": row["max_health"],
        "health": row["health"],
        "action_mode": row["action_mode"],
        "cargo": row["cargo"],
        "command_state": row["command_state"],
        "x": row["x"],
        "y": row["y"],
    }


def parity_projection(row):
    if row is None:
        return None
    return {
        key: row[key]
        for key in (
            "list", "type", "owner", "area_marker_high", "type_flags",
            "max_health", "health", "action_mode", "cargo",
            "command_state", "x", "y")
    }


def consume_transition(before, after):
    if before is None or after is None:
        return None
    action_delta = after["action_mode"] - before["action_mode"]
    health_delta = after["health"] - before["health"]
    cargo_delta = after["cargo"] - before["cargo"]
    return {
        "action_delta": action_delta,
        "health_delta": health_delta,
        "cargo_delta": cargo_delta,
        "health_before": before["health"],
        "health_after": after["health"],
        "max_health": after["max_health"],
        "matched": (
            before["health"] < before["max_health"] and
            action_delta < 0 and health_delta == -action_delta and
            cargo_delta == 0),
    }


def condition_matches(condition, previous, current):
    original = current["original"]
    rebuild = current["rebuild"]
    parity = parity_projection(original) == parity_projection(rebuild)
    detail = {"parity": parity}
    if condition == "snapshot":
        return parity, detail
    if condition == "marker":
        matched = (
            parity and original is not None and rebuild is not None and
            original["area_marker_high"] and rebuild["area_marker_high"])
        return matched, detail
    if condition == "damaged":
        matched = (
            parity and original is not None and rebuild is not None and
            original["health"] < original["max_health"] and
            rebuild["health"] < rebuild["max_health"])
        return matched, detail
    if condition == "consume":
        if previous is None:
            return False, detail
        original_transition = consume_transition(
            previous["original"], original)
        rebuild_transition = consume_transition(
            previous["rebuild"], rebuild)
        detail.update({
            "original_transition": original_transition,
            "rebuild_transition": rebuild_transition,
        })
        matched = (
            parity and original_transition is not None and
            rebuild_transition is not None and
            original_transition["matched"] and
            rebuild_transition["matched"] and
            original_transition == rebuild_transition)
        return matched, detail
    raise ValueError(f"unknown condition: {condition}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("original_pid", type=int)
    parser.add_argument("rebuild_pid", type=int)
    parser.add_argument("rebuild_base", type=lambda value: int(value, 0))
    parser.add_argument("layout_json")
    parser.add_argument("slot", type=int)
    parser.add_argument(
        "--condition",
        choices=(
            "snapshot", "marker", "damaged", "consume",
            "damage-consume"),
        default="snapshot",
    )
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--interval", type=float, default=0.01)
    args = parser.parse_args()

    with open(args.layout_json, "r", encoding="utf-8-sig") as stream:
        layout = json.load(stream)

    original_memory = MEAT.Memory(args.original_pid)
    rebuild_memory = MEAT.Memory(args.rebuild_pid)
    try:
        rebuild_path = rebuild_memory.image_path()
        actual_hash = MEAT.file_sha256(rebuild_path)
        expected_hash = str(layout.get("sha256", "")).upper()
        if expected_hash and actual_hash != expected_hash:
            raise RuntimeError(
                "rebuild/layout hash mismatch: "
                f"process={actual_hash}, layout={expected_hash}")
        if rebuild_memory.read(args.rebuild_base, 2) != b"MZ":
            raise RuntimeError(
                f"0x{args.rebuild_base:x} is not the rebuild image base")

        rebuild = MEAT.RebuildReader(
            rebuild_memory, args.rebuild_base, layout)
        histories = MEAT.new_stable_pair_histories()
        started = time.monotonic()
        previous = None
        exact_samples = 0
        parity_mismatch_samples = 0
        first_frame = None
        last_frame = None
        matched_pair = None
        matched_detail = None
        damage_observed = False

        while time.monotonic() - started < args.timeout:
            pair = MEAT.capture_stable_pair(
                original_memory, rebuild, histories)
            if pair is None:
                time.sleep(args.interval)
                continue
            frame = pair["frame"]
            if frame == last_frame:
                time.sleep(args.interval)
                continue
            first_frame = frame if first_frame is None else first_frame
            last_frame = frame
            exact_samples += 1
            current = {
                "frame": frame,
                "original": state_projection(
                    pair["original"]["units"].get(args.slot)),
                "rebuild": state_projection(
                    pair["rebuild"]["units"].get(args.slot)),
            }
            if (parity_projection(current["original"]) !=
                    parity_projection(current["rebuild"])):
                parity_mismatch_samples += 1
            damaged_now, _ = condition_matches("damaged", previous, current)
            damage_observed |= damaged_now
            evaluated_condition = (
                "consume" if args.condition == "damage-consume"
                else args.condition)
            matched, detail = condition_matches(
                evaluated_condition, previous, current)
            if args.condition == "damage-consume":
                detail["damage_observed"] = damage_observed
                matched = matched and damage_observed
            if matched:
                matched_pair = current
                matched_detail = detail
                break
            previous = current
            time.sleep(args.interval)

        result = {
            "probe": "targeted meat finalized exact-frame unit condition",
            "condition": args.condition,
            "slot": args.slot,
            "matched": matched_pair is not None,
            "exact_frame_samples": exact_samples,
            "first_frame": first_frame,
            "last_frame": last_frame,
            "parity_mismatch_samples": parity_mismatch_samples,
            "damage_observed": damage_observed,
            "pair": matched_pair,
            "detail": matched_detail,
            "rebuild": {
                "path": rebuild_path,
                "base": f"0x{args.rebuild_base:x}",
                "process_sha256": actual_hash,
                "layout_sha256": expected_hash,
                "hash_match": not expected_hash or actual_hash == expected_hash,
            },
        }
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if exact_samples else 2
    finally:
        original_memory.close()
        rebuild_memory.close()


if __name__ == "__main__":
    raise SystemExit(main())
