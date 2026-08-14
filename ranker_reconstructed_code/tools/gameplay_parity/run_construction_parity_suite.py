#!/usr/bin/env python3
"""Run every player worker/building pair through completed construction."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from run_unit_production_parity_suite import atomic_json, record_key


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=tool_dir.parents[2])
    parser.add_argument("--batch", type=int, action="append", default=[])
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=120)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "construction_batch_manifest.json")
                          .read_text(encoding="utf-8"))
    batches = [row for row in manifest["batches"]
               if not args.batch or row["batch_index"] in args.batch]
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact_root = (root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts" / "runs" /
                     f"construction_parity_{stamp}")
    artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0

    for number, batch in enumerate(batches, 1):
        label = f"B{batch['batch_index']:02d}"
        artifact = artifact_root / label
        command = [
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script), "-ReplayPath", batch["replay"],
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(args.end_frame),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(args.timeout_seconds),
            "-TraceIntervalMs", str(args.trace_interval_ms),
        ]
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"bindings={len(batch['cases'])}", flush=True)
        completed = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            timeout=args.timeout_seconds + 60)
        result_path = artifact / "result.json"
        if completed.returncode == 0 and result_path.exists():
            trace = json.loads(result_path.read_text(encoding="utf-8"))
        else:
            trace = {"pass": False, "reason": (
                completed.stderr.strip() or completed.stdout.strip() or
                f"trace command exited {completed.returncode}")}
        continuous_exact = bool(
            trace.get("pass") and trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and
            not trace.get("pair_gaps"))
        terminal = trace.get("terminal_exact_state") or {}
        player_units = terminal.get("player_units", {})
        observed_types = [row.get("type") for row in player_units.values()]
        completed_counts = {
            tuple(row) for row in
            terminal.get("production", {}).get("completed_types", [])
        }
        batch_failures = 0
        for case in batch["cases"]:
            building_id = case["building_unit_id"]
            expected_count = int(
                batch["expected_completed_type_counts"][str(building_id)])
            spawned = building_id in observed_types
            completed_count = (0, building_id, expected_count) in completed_counts
            if not continuous_exact:
                verdict = "divergent"
            elif not spawned or not completed_count:
                verdict = "not_exercised"
            else:
                verdict = "exact"
            row = {
                "kind": "construction", **case,
                "result": verdict,
                "spawned_type_seen": spawned,
                "completed_count_reached": completed_count,
                "expected_completed_count": expected_count,
                "replay": batch["replay"],
                "replay_sha256": batch["replay_sha256"],
                "rebuild_sha256": trace.get("sha256"),
                "trace_artifact": artifact.relative_to(root).as_posix(),
                "frame_range": [args.start_frame, args.end_frame - 1],
                "exact_pair_count": trace.get("exact_pair_count", 0),
                "first_exact_frame": trace.get("first_exact_frame"),
                "last_exact_frame": trace.get("last_exact_frame"),
                "pair_gaps": trace.get("pair_gaps", []),
                "trace_reason": trace.get("reason"),
                "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
            }
            records[record_key(row)] = row
            batch_failures += verdict != "exact"
        recorded["cases"] = sorted(records.values(), key=lambda row: tuple(
            str(value) for value in record_key(row)))
        atomic_json(results_path, recorded)
        failures += batch_failures
        print(f"  {'PASS' if not batch_failures else f'FAIL({batch_failures})'}: "
              f"exact_frames={trace.get('exact_pair_count', 0)} "
              f"player_units={len(player_units)}", flush=True)

    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"trace timed out: {error}", file=sys.stderr)
        raise SystemExit(2)
