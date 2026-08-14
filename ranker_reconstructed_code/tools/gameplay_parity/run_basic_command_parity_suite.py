#!/usr/bin/env python3
"""Compare guard/idle and primary point actions for every capable unit."""

from __future__ import annotations

import argparse
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

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
    parser.add_argument("--reuse-artifact-root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "basic_command_batch_manifest.json")
                          .read_text(encoding="utf-8"))
    batches = [row for row in manifest["batches"]
               if not args.batch or row["batch_index"] in args.batch]
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
                         f"basic_command_parity_{stamp}")
        artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0
    for number, batch in enumerate(batches, 1):
        label = f"B{batch['batch_index']:02d}"
        artifact = artifact_root / label
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"units={len(batch['cases'])}", flush=True)
        result_path = artifact / "result.json"
        if args.reuse_artifact_root:
            trace = (json.loads(result_path.read_text(encoding="utf-8"))
                     if result_path.exists() else
                     {"pass": False, "reason": f"missing reused trace: {result_path}"})
        else:
            completed = subprocess.run([
                "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(trace_script), "-ReplayPath", batch["replay"],
                "-StartFrame", str(args.start_frame),
                "-EndFrame", str(args.end_frame),
                "-OutputDirectory", str(artifact),
                "-TimeoutSeconds", str(args.timeout_seconds),
                "-TraceIntervalMs", str(args.trace_interval_ms),
            ], cwd=root, capture_output=True, text=True,
                timeout=args.timeout_seconds + 60)
            trace = (json.loads(result_path.read_text(encoding="utf-8"))
                     if completed.returncode == 0 and result_path.exists()
                     else {"pass": False, "reason": (
                         completed.stderr.strip() or completed.stdout.strip() or
                         f"trace command exited {completed.returncode}")})
        continuous = bool(
            trace.get("pass") and trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and not trace.get("pair_gaps"))
        ranges = trace.get("semantic_coverage", {}).get(
            "player_unit_state_ranges", {})
        terminal_units = (trace.get("terminal_exact_state") or {}).get(
            "player_units", {})
        batch_failures = 0
        for case in batch["cases"]:
            state_range = ranges.get(str(case["unit_slot"]), {})
            states = state_range.get("states", [])
            terminal = terminal_units.get(str(case["unit_slot"]), {})
            guard_seen = terminal.get("state") == 1
            action_rows = [(0, guard_seen, "returned_to_idle")]
            if case["primary_capable"]:
                primary_seen = any(value in states for value in (8, 9, 10))
                action_rows.append((1, primary_seen, "point_action_state_seen"))
            for selector, exercised, evidence_kind in action_rows:
                verdict = ("exact" if continuous and exercised else
                           ("not_exercised" if continuous else "divergent"))
                row = {
                    "kind": "direct_action",
                    "action_selector": selector,
                    "unit_id": case["unit_id"],
                    "unit_name": case["unit_name"],
                    "unit_slot": case["unit_slot"],
                    "result": verdict,
                    "evidence_kind": evidence_kind,
                    "observed_states": states,
                    "terminal_unit": terminal,
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
        failures += batch_failures
        print(f"  {'PASS' if not batch_failures else f'FAIL({batch_failures})'}: "
              f"exact_frames={trace.get('exact_pair_count', 0)}", flush=True)
    recorded["cases"] = sorted(records.values(), key=lambda row: tuple(
        str(value) for value in record_key(row)))
    atomic_json(results_path, recorded)
    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
