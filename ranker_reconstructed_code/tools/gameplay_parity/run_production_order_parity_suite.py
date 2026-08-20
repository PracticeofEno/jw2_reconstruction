#!/usr/bin/env python3
"""Run every UI-bound production order through enqueue and completion."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from parity_trace_support import verify_missing_exact_frames
from parity_result_store import atomic_json, record_key


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
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "production_order_batch_manifest.json")
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
                     f"production_order_parity_{stamp}")
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
            "-StabilizeViewport", "-AlignPresentationRng",
        ]
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"orders={len(batch['cases'])}", flush=True)
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
        gap_fallback = verify_missing_exact_frames(
            root, batch["replay"], artifact, trace,
            args.start_frame, args.end_frame, args.timeout_seconds,
            stabilize_viewport=True, align_presentation_rng=True)
        gaps_verified = bool(gap_fallback and gap_fallback.get("pass"))
        continuous_exact = bool(
            trace.get("pass") and trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and
            ((trace.get("first_exact_frame") <= args.start_frame and
              trace.get("last_exact_frame") >= args.end_frame - 1 and
              not trace.get("pair_gaps")) or gaps_verified))
        terminal = trace.get("terminal_exact_state") or {}
        production = terminal.get("production", {})
        variants = {tuple(row) for row in production.get("variants", [])}
        observed_effects = production.get("completion_effects", [])
        effects_match = (observed_effects ==
                         batch["expected_completion_effects"])
        order_2b_match = (production.get("order_2b_bonus") ==
                          batch["expected_order_2b_bonus"])
        batch_failures = 0
        for case in batch["cases"]:
            completed_order = (0, case["order_id"], 1) in variants
            if not continuous_exact:
                verdict = "divergent"
            elif not completed_order or not effects_match or not order_2b_match:
                verdict = "not_exercised"
            else:
                verdict = "exact"
            row = {
                "kind": "production_order", **case,
                "result": verdict,
                "completed_variant_seen": completed_order,
                "batch_completion_effects_match_catalog": effects_match,
                "batch_order_2b_bonus_matches_catalog": order_2b_match,
                "terminal_production": production,
                "replay": batch["replay"],
                "replay_sha256": batch["replay_sha256"],
                "rebuild_sha256": trace.get("sha256"),
                "trace_artifact": artifact.relative_to(root).as_posix(),
                "frame_range": [args.start_frame, args.end_frame - 1],
                "exact_pair_count": trace.get("exact_pair_count", 0),
                "first_exact_frame": trace.get("first_exact_frame"),
                "last_exact_frame": trace.get("last_exact_frame"),
                "pair_gaps": trace.get("pair_gaps", []),
                "gap_fallback": gap_fallback,
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
              f"effects={len(observed_effects)}", flush=True)

    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"trace timed out: {error}", file=sys.stderr)
        raise SystemExit(2)
