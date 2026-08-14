#!/usr/bin/env python3
"""Compare factorized equipment, return, balance, hold, and death commands."""

from __future__ import annotations

import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from run_unit_production_parity_suite import atomic_json, record_key


INTERACTION_STATES = {0x0D, 0x0E}
VALUE_TRANSFER_STATES = {0x73, 0x74, 0x75}


def changed_range(value: dict, initial: int) -> bool:
    return value.get("min", initial) != initial or value.get("max", initial) != initial


def merge_checkpoint_row(ranges: dict, slot: str, row: dict) -> None:
    """Merge one exact one-shot state into the compact trace ranges."""
    target = ranges.setdefault(slot, {})
    for key, value in (
            ("states", row.get("state")),
            ("command_flag_values", row.get("command_flags"))):
        if value is None:
            continue
        values = target.setdefault(key, [])
        if value not in values:
            values.append(value)
            values.sort()
    for key in ("cargo", "action_mode"):
        value = row.get(key)
        if value is None:
            continue
        bounds = target.setdefault(key, {"min": value, "max": value})
        bounds["min"] = min(bounds.get("min", value), value)
        bounds["max"] = max(bounds.get("max", value), value)


def finish_return_cargo_with_one_shot_probes(
        root: Path, replay: str, artifact: Path, trace: dict,
        start_frame: int, end_frame: int, timeout_seconds: int) -> dict:
    """Cover ReturnCargo when original fast continuous playback stalls.

    The original executable can stop advancing immediately after the deposit
    callback when the replay is allowed to run continuously.  The suspended
    one-shot probe does not have that limitation.  Preserve every exact frame
    already captured by the compact trace and probe each missing frame through
    frame 40, which includes approach, deposit, and post-deposit command state.
    """
    journal_path = artifact / "journal.jsonl"
    observed = set()
    if journal_path.exists():
        for line in journal_path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("exact"):
                observed.add(int(row["frame"]))
            elif row.get("frame") is not None:
                return {"pass": False, "frames": [],
                        "reason": f"non-exact trace frame {row['frame']}"}
    coverage_end = min(end_frame - 1, 40)
    required = set(range(start_frame, coverage_end + 1))
    missing = sorted(required - observed)
    if not observed or trace.get("first_difference") is not None:
        return {"pass": False, "frames": [],
                "reason": "compact trace had no exact prefix"}

    probe_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "probe_replay.ps1")
    ranges = trace.setdefault("semantic_coverage", {}).setdefault(
        "player_unit_state_ranges", {})
    terminal_units = trace.setdefault("terminal_exact_state", {}).setdefault(
        "player_units", {})
    exact_probes = []
    for frame in missing:
        output = artifact / f"one_shot_{frame:03d}"
        completed = subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(probe_script), "-ReplayPath", replay,
            "-TargetFrame", str(frame), "-OutputDirectory", str(output),
            "-TimeoutSeconds", str(timeout_seconds),
        ], cwd=root, capture_output=True, text=True,
            timeout=timeout_seconds + 60)
        result_path = output / "result.json"
        detail_path = output / "result-detail.json"
        if (completed.returncode != 0 or not result_path.exists() or
                not detail_path.exists()):
            reason = (completed.stderr.strip() or completed.stdout.strip() or
                      f"one-shot frame {frame} exited {completed.returncode}")
            return {"pass": False, "frames": exact_probes,
                    "reason": reason}
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if not result.get("pass"):
            return {"pass": False, "frames": exact_probes,
                    "reason": result.get("reason", f"frame {frame} diverged")}
        detail = json.loads(detail_path.read_text(encoding="utf-8"))
        original = detail["divergence_pair"]["original"]["state"]
        for slot, row in original.get("rows", {}).items():
            merge_checkpoint_row(ranges, slot, row)
            if frame == coverage_end:
                terminal_units[slot] = row
        exact_probes.append(frame)

    trace["pass"] = True
    trace["reason"] = (
        "exact compact prefix plus one-shot ReturnCargo frames through 40")
    trace["exact_pair_count"] = len(required)
    trace["first_exact_frame"] = start_frame
    trace["last_exact_frame"] = coverage_end
    trace["pair_gaps"] = []
    return {"pass": True, "frames": exact_probes, "reason": trace["reason"]}


def fill_behavior_trace_gaps(root: Path, replay: str, artifact: Path,
                             trace: dict, command_frame: int,
                             timeout_seconds: int) -> dict | None:
    """One-shot any compact-trace gaps after player commands begin."""
    behavior_gaps = [gap for gap in trace.get("pair_gaps", [])
                     if gap[1] > command_frame]
    if not trace.get("pass") or not behavior_gaps:
        return None
    frames = sorted({frame
                     for low, high in behavior_gaps
                     for frame in range(max(low + 1, command_frame), high)})
    probe_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "probe_replay.ps1")
    exact_frames = []
    for frame in frames:
        output = artifact / f"gap_one_shot_{frame:03d}"
        completed = subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(probe_script), "-ReplayPath", replay,
            "-TargetFrame", str(frame), "-OutputDirectory", str(output),
            "-TimeoutSeconds", str(timeout_seconds),
        ], cwd=root, capture_output=True, text=True,
            timeout=timeout_seconds + 60)
        result_path = output / "result.json"
        if completed.returncode != 0 or not result_path.exists():
            return {"pass": False, "frames": exact_frames,
                    "reason": (completed.stderr.strip() or
                               completed.stdout.strip() or
                               f"gap frame {frame} probe failed")}
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if not result.get("pass"):
            return {"pass": False, "frames": exact_frames,
                    "reason": result.get("reason", f"frame {frame} diverged")}
        exact_frames.append(frame)
    trace["exact_pair_count"] = trace.get("exact_pair_count", 0) + len(exact_frames)
    return {"pass": True, "frames": exact_frames,
            "reason": "behavior trace gaps verified by one-shot probes"}


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=tool_dir.parents[2])
    parser.add_argument("--batch", type=int, action="append", default=[])
    parser.add_argument("--label", action="append", default=[])
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=140)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "direct_command_batch_manifest.json")
                          .read_text(encoding="utf-8"))
    batches = [row for row in manifest["batches"]
               if (not args.batch or row["batch_index"] in args.batch) and
               (not args.label or row["label"] in args.label)]
    if not batches:
        raise ValueError("no manifest batches matched the filters")

    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact_root = (root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts" / "runs" /
                     f"direct_command_parity_{stamp}")
    artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0

    for number, batch in enumerate(batches, 1):
        label = f"B{batch['batch_index']:02d}_{batch['label']}"
        artifact = artifact_root / label
        trace_timeout = (min(args.timeout_seconds, 45)
                         if batch["label"] == "ReturnCargo"
                         else args.timeout_seconds)
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"cases={len(batch['cases'])}", flush=True)
        completed = subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script), "-ReplayPath", batch["replay"],
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(args.end_frame),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(trace_timeout),
            "-TraceIntervalMs", str(args.trace_interval_ms),
        ], cwd=root, capture_output=True, text=True,
            timeout=trace_timeout + 60)
        result_path = artifact / "result.json"
        trace = (json.loads(result_path.read_text(encoding="utf-8"))
                 if completed.returncode == 0 and result_path.exists()
                 else {"pass": False, "reason": (
                     completed.stderr.strip() or completed.stdout.strip() or
                     f"trace command exited {completed.returncode}")})
        checkpoint_fallback = None
        if (batch["label"] == "ReturnCargo" and not trace.get("pass") and
                "timed out" in trace.get("reason", "")):
            checkpoint_fallback = finish_return_cargo_with_one_shot_probes(
                root, batch["replay"], artifact, trace, args.start_frame,
                args.end_frame, args.timeout_seconds)
        gap_fallback = fill_behavior_trace_gaps(
            root, batch["replay"], artifact, trace, 31,
            args.timeout_seconds)
        behavior_gaps = [gap for gap in trace.get("pair_gaps", [])
                         if gap[1] > 31]
        behavior_gaps_verified = bool(
            gap_fallback and gap_fallback.get("pass"))
        return_cargo_checkpoint_complete = bool(
            batch["label"] == "ReturnCargo" and
            checkpoint_fallback and checkpoint_fallback.get("pass") and
            trace.get("last_exact_frame", -1) >= 40)
        continuous = bool(
            trace.get("pass") and trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and
            trace.get("first_exact_frame") <= 31 and
            (trace.get("last_exact_frame") >= args.end_frame - 1 or
             return_cargo_checkpoint_complete) and
            (not behavior_gaps or behavior_gaps_verified))
        ranges = trace.get("semantic_coverage", {}).get(
            "player_unit_state_ranges", {})
        terminal_units = (trace.get("terminal_exact_state") or {}).get(
            "player_units", {})
        batch_failures = 0

        for case in batch["cases"]:
            operation = case["operation"]
            source_slot = case.get("source_slot", case.get("unit_slot"))
            source_range = ranges.get(str(source_slot), {})
            source_terminal = terminal_units.get(str(source_slot), {})
            states = set(source_range.get("states", []))
            exercised = False
            evidence = {}
            if operation == "equipment_transfer":
                target_range = ranges.get(str(case["target_slot"]), {})
                target_terminal = terminal_units.get(str(case["target_slot"]), {})
                source_equipment = source_terminal.get("equipment", [])
                target_equipment = target_terminal.get("equipment", [])
                source_cleared = case["effect_id"] not in source_equipment
                target_received = (
                    case["effect_id"] in target_equipment or
                    target_terminal.get("type") != case["target_unit_id"] or
                    bool(target_range.get("production_variants")) and
                    max(target_range["production_variants"]) > 0 or
                    changed_range(target_range.get("runtime_stat_20", {}),
                                  target_terminal.get("runtime_stat_20", 0)))
                interaction_seen = bool(states & INTERACTION_STATES)
                transfer_completed = (
                    source_cleared and target_received and interaction_seen)
                immobile_approach_seen = (
                    case.get("expected_immobile_approach", False) and
                    0x0E in states and not source_cleared and
                    not target_received)
                exercised = transfer_completed or immobile_approach_seen
                evidence = {
                    "interaction_seen": interaction_seen,
                    "transfer_completed": transfer_completed,
                    "expected_immobile_approach":
                        case.get("expected_immobile_approach", False),
                    "immobile_approach_seen": immobile_approach_seen,
                    "source_slot_cleared": source_cleared,
                    "target_received": target_received,
                    "terminal_target": target_terminal,
                }
            elif operation == "food_transfer":
                target_range = ranges.get(str(case["target_slot"]), {})
                source_action = source_range.get("action_mode", {})
                target_action = target_range.get("action_mode", {})
                exercised = (bool(states & INTERACTION_STATES) and
                             source_action.get("min", 100) <= 50 and
                             target_action.get("max", 0) >= 50)
                evidence = {"source_action_mode": source_action,
                            "target_action_mode": target_action}
            elif operation == "balance_secondary":
                donor_range = ranges.get(str(case["target_slot"]), {})
                recipient_action = source_range.get("action_mode", {})
                donor_action = donor_range.get("action_mode", {})
                exercised = (bool(states & VALUE_TRANSFER_STATES) and
                             recipient_action.get("max", 0) >= 50 and
                             donor_action.get("min", 100) <= 50)
                evidence = {"recipient_action_mode": recipient_action,
                            "donor_action_mode": donor_action}
            elif operation == "return_food":
                exercised = bool(states & INTERACTION_STATES)
                evidence = {"interaction_seen": exercised,
                            "action_mode": source_range.get("action_mode", {})}
            elif operation == "return_cargo":
                cargo = source_range.get("cargo", {})
                low_states = {state & 0x00FFFFFF for state in states}
                command_flag_values = source_range.get("command_flag_values", [])
                deposit_seen = 0x2B in low_states
                carry_flag_cleared = (
                    any((value & 4) != 0 for value in command_flag_values) and
                    any((value & 4) == 0 for value in command_flag_values))
                # Original state 0x2b credits raw +0x4c but deliberately
                # retains its cargo word; completion is the deposit state plus
                # clearing command flag 4, not cargo becoming zero.
                exercised = deposit_seen and carry_flag_cleared
                evidence = {"deposit_state_seen": deposit_seen,
                            "carry_flag_cleared": carry_flag_cleared,
                            "cargo_retained": cargo}
            elif operation == "reserved_work":
                command_flag_values = source_range.get("command_flag_values", [])
                exercised = (1 in states and
                             any((value & 1) != 0
                                 for value in command_flag_values))
                evidence = {"idle_acquire_seen": 1 in states,
                            "reservation_flag_seen": any(
                                (value & 1) != 0
                                for value in command_flag_values)}
            elif operation == "hold_position":
                exercised = (source_terminal.get("runtime_flags", 0) & 8) != 0
                evidence = {"terminal_runtime_flags":
                            source_terminal.get("runtime_flags")}
            elif operation == "nested_death_mark":
                dead_state_seen = any((state & 0x10000000) != 0 for state in states)
                lifecycle_seen = "lifecycle" in source_range.get("lists", [])
                exercised = dead_state_seen or lifecycle_seen
                evidence = {"dead_state_seen": dead_state_seen,
                            "lifecycle_seen": lifecycle_seen}
            else:
                raise ValueError(f"unknown operation {operation}")

            verdict = ("exact" if continuous and exercised else
                       ("not_exercised" if continuous else "divergent"))
            row = {
                "kind": "direct_command",
                **case,
                "result": verdict,
                "exercised": exercised,
                "evidence": evidence,
                "observed_states": sorted(states),
                "source_state_range": source_range,
                "terminal_source": source_terminal,
                "replay": batch["replay"],
                "replay_sha256": batch["replay_sha256"],
                "rebuild_sha256": trace.get("sha256"),
                "trace_artifact": artifact.relative_to(root).as_posix(),
                "frame_range": [args.start_frame, args.end_frame - 1],
                "exact_pair_count": trace.get("exact_pair_count", 0),
                "first_exact_frame": trace.get("first_exact_frame"),
                "last_exact_frame": trace.get("last_exact_frame"),
                "pair_gaps": trace.get("pair_gaps", []),
                "behavior_pair_gaps": behavior_gaps,
                "gap_fallback": gap_fallback,
                "trace_reason": trace.get("reason"),
                "checkpoint_fallback": checkpoint_fallback,
                "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
            }
            records[record_key(row)] = row
            batch_failures += verdict != "exact"

        recorded["cases"] = sorted(records.values(), key=lambda row: tuple(
            str(value) for value in record_key(row)))
        atomic_json(results_path, recorded)
        failures += batch_failures
        print(f"  {'PASS' if not batch_failures else f'FAIL({batch_failures})'}: "
              f"exact_frames={trace.get('exact_pair_count', 0)}", flush=True)

    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
