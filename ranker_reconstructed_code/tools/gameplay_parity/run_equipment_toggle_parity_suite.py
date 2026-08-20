#!/usr/bin/env python3
"""Run all UI-reachable equipment slot transitions in original and rebuild."""

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


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root_default = tool_dir.parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=root_default)
    parser.add_argument("--batch", type=int, action="append", default=[])
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=80)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--reuse-artifact-root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "equipment_toggle_batch_manifest.json")
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
                         f"equipment_toggle_parity_{stamp}")
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
            "-File", str(trace_script), "-ReplayPath", batch["replay"],
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(args.end_frame),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(args.timeout_seconds),
            "-TraceIntervalMs", str(args.trace_interval_ms),
            "-TemporaryReplayName",
            f"DebugReplay_EquipmentToggle_{stamp}_{label}.ply",
            "-StabilizeViewport",
            "-AlignPresentationRng",
        ]
        completed = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            timeout=args.timeout_seconds + 60)
        result_path = artifact / "result.json"
        if completed.returncode == 0 and result_path.exists():
            trace = json.loads(result_path.read_text(encoding="utf-8"))
        else:
            return {"pass": False, "reason": (
                completed.stderr.strip() or completed.stdout.strip() or
                f"trace command exited {completed.returncode}")}
        return trace

    for number, batch in enumerate(batches, 1):
        trace = run_trace(batch)
        label = f"B{batch['batch_index']:02d}"
        artifact = artifact_root / label
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"cases={len(batch['cases'])}", flush=True)
        behavior_gaps = [gap for gap in trace.get("pair_gaps", [])
                         if gap[1] > manifest["command_frame"]]
        sampler_complete = bool(
            trace.get("first_exact_frame") is not None and
            trace.get("first_exact_frame") <= args.start_frame and
            trace.get("last_exact_frame") is not None and
            trace.get("last_exact_frame") >= args.end_frame - 1 and
            not trace.get("pair_gaps"))
        gap_fallback = (verify_missing_exact_frames(
            root, batch["replay"], artifact, trace, args.start_frame,
            args.end_frame, args.timeout_seconds,
            stabilize_viewport=True,
            align_presentation_rng=True)
            if trace.get("pass") and not sampler_complete else None)
        continuous_exact = bool(
            trace.get("pass") and
            (sampler_complete or
             (gap_fallback is not None and gap_fallback.get("pass"))))
        terminal_units = ((trace.get("terminal_exact_state") or
                           trace.get("terminal_state") or {})
                          .get("player_units", {}))
        batch_failures = 0
        for case in batch["cases"]:
            terminal = terminal_units.get(str(case["source_slot"]), {})
            observed_equipment = terminal.get("equipment")
            transition_seen = observed_equipment == case["expected_equipment"]
            expected_no_change = (case["expected_equipment"] ==
                                  case["initial_equipment"])
            if not continuous_exact:
                verdict = "divergent"
            elif not transition_seen:
                verdict = "not_exercised"
            else:
                verdict = "exact"
            row = {
                "kind": "equipment_toggle", **case,
                "result": verdict,
                "expected_no_state_change": expected_no_change,
                "transition_seen": transition_seen,
                "observed_equipment": observed_equipment,
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
        print(f"  {'PASS' if not batch_failures else f'FAIL({batch_failures})'}: "
              f"exact_frames={trace.get('exact_pair_count', 0)}", flush=True)

    recorded["cases"] = sorted(records.values(), key=lambda row: tuple(
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
