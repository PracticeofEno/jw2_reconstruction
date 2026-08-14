#!/usr/bin/env python3
"""Run all player equipment subtype-03 effects in original and rebuild."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from parity_result_store import atomic_json, record_key
from parity_trace_support import verify_missing_exact_frames


def changed_fields(case: dict[str, Any], unit_range: dict[str, Any],
                   terminal: dict[str, Any]) -> list[str]:
    initial = case["initial"]
    changed = []
    for field in case["observable_fields"]:
        if field == "type":
            if terminal.get("type") != initial["type"]:
                changed.append(field)
        elif field == "owner_resource":
            # Not used by the shipped catalog, retained for schema completeness.
            changed.append(field)
        elif field == "command_value":
            if terminal.get("command_value") != initial.get(field, 0):
                changed.append(field)
        elif field == "production_variant":
            if terminal.get(field) != initial[field]:
                changed.append(field)
        elif field == "elite_progress":
            observed = unit_range.get(field, {})
            rank_variants = unit_range.get("production_variants", [])
            if (observed.get("min") != initial.get(field) or
                    observed.get("max") != initial.get(field) or
                    any(value != initial.get("production_variant", 0)
                        for value in rank_variants)):
                # A large experience grant can cross a rank threshold and
                # consume the entire stored progress value back to zero.  The
                # original still publishes the temporary/ranked production
                # variant (and possibly stat growth), so an unchanged terminal
                # progress word is not a rejected or unexercised effect.
                changed.append(field)
        else:
            observed = unit_range.get(field, {})
            if (observed.get("min") != initial.get(field) or
                    observed.get("max") != initial.get(field)):
                changed.append(field)
    return changed


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root_default = tool_dir.parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=root_default)
    parser.add_argument("--batch", type=int, action="append", default=[])
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=100)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--reuse-artifact-root", type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest = json.loads((tool_dir / "equipment_apply_batch_manifest.json")
                          .read_text(encoding="utf-8"))
    batches = [row for row in manifest["batches"]
               if not args.batch or row["batch_index"] in args.batch]
    if not batches:
        raise ValueError("no manifest batches matched the filters")
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if args.reuse_artifact_root:
        artifact_root = args.reuse_artifact_root.resolve()
        if not artifact_root.is_dir():
            raise FileNotFoundError(artifact_root)
    else:
        artifact_root = (root / "ranker_reconstructed_code" / "tools" /
                         "replay_debug" / "artifacts" / "runs" /
                         f"equipment_apply_parity_{stamp}")
        artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0

    def run_trace(batch: dict[str, Any]) -> dict[str, Any]:
        batch_index = batch["batch_index"]
        label = f"B{batch_index:02d}"
        artifact = artifact_root / label
        if args.reuse_artifact_root:
            result_path = artifact / "result.json"
            if not result_path.exists():
                return {"pass": False,
                        "reason": f"missing reused trace: {result_path}"}
            return json.loads(result_path.read_text(encoding="utf-8"))
        command = [
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script),
            "-ReplayPath", batch["replay"],
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(args.end_frame),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(args.timeout_seconds),
            "-TraceIntervalMs", str(args.trace_interval_ms),
            "-TemporaryReplayName",
            f"DebugReplay_EquipmentApply_{stamp}_{label}.ply",
        ]
        completed = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            timeout=args.timeout_seconds + 60)
        result_path = artifact / "result.json"
        if completed.returncode == 0 and result_path.exists():
            trace = json.loads(result_path.read_text(encoding="utf-8"))
        else:
            return {
                "pass": False,
                "reason": (completed.stderr.strip() or
                           completed.stdout.strip() or
                           f"trace command exited {completed.returncode}"),
            }
        return trace

    for number, batch in enumerate(batches, 1):
        trace = run_trace(batch)
        batch_index = batch["batch_index"]
        label = f"B{batch_index:02d}"
        artifact = artifact_root / label
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"effects={len(batch['cases'])}", flush=True)
        behavior_gaps = [gap for gap in trace.get("pair_gaps", [])
                         if gap[1] > manifest["command_frame"]]
        gap_fallback = (verify_missing_exact_frames(
            root, batch["replay"], artifact, trace, args.start_frame,
            args.end_frame, args.timeout_seconds) if behavior_gaps else None)
        continuous_exact = bool(
            trace.get("pass") and
            trace.get("first_exact_frame") is not None and
            trace.get("first_exact_frame") <= manifest["command_frame"] and
            trace.get("last_exact_frame") is not None and
            trace.get("last_exact_frame") >= args.end_frame - 1 and
            (not behavior_gaps or
             (gap_fallback is not None and gap_fallback.get("pass"))))
        coverage = trace.get("semantic_coverage") or {}
        ranges = coverage.get("player_unit_state_ranges", {})
        terminal_units = ((trace.get("terminal_exact_state") or
                           trace.get("terminal_state") or {})
                          .get("player_units", {}))
        batch_failures = 0

        for case in batch["cases"]:
            slot = str(case["source_slot"])
            unit_range = ranges.get(slot, {})
            terminal = terminal_units.get(slot, {})
            changed = changed_fields(case, unit_range, terminal)
            expected = case["observable_fields"]
            # Positive health/secondary additions clamp at the unit's current
            # maxima.  The fixture intentionally starts full, so such a field
            # alone can be an exact, exercised no-op.
            clamped_full_fields = {
                field for field in expected
                if field in ("health", "secondary") and
                case["initial"].get(field) == case["initial"].get(
                    "max_health" if field == "health" else "max_secondary")
            }
            effective_expected = [field for field in expected
                                  if field not in clamped_full_fields]
            immediate_mutation_expected = bool(effective_expected)
            mutation_seen = bool(changed)
            if not continuous_exact:
                verdict = "divergent"
            elif immediate_mutation_expected and not mutation_seen:
                verdict = "not_exercised"
            else:
                verdict = "exact"
            row = {
                "kind": "equipment_apply",
                **case,
                "result": verdict,
                "immediate_mutation_expected": immediate_mutation_expected,
                "mutation_seen": mutation_seen,
                "changed_fields": changed,
                "catalog_no_immediate_simulation_mutation": not expected,
                "clamped_full_fields": sorted(clamped_full_fields),
                "source_state_range": unit_range,
                "terminal_source": terminal,
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
                "gap_one_shot": gap_fallback,
                "trace_reason": trace.get("reason"),
                "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
            }
            records[record_key(row)] = row
            batch_failures += verdict != "exact"

        failures += batch_failures
        status = "PASS" if batch_failures == 0 else f"FAIL({batch_failures})"
        print(f"  {status}: exact_frames={trace.get('exact_pair_count', 0)}",
              flush=True)

    recorded["schema"] = 1
    recorded["cases"] = sorted(
        records.values(), key=lambda row: tuple(
            str(value) for value in record_key(row)))
    atomic_json(results_path, recorded)
    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"trace timed out: {error}", file=sys.stderr)
        raise SystemExit(2)
