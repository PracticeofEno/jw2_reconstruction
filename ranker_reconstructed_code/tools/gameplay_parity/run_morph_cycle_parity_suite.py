#!/usr/bin/env python3
"""Compare every player-operable morph enter/exit cycle with the original."""

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
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=90)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "morph_cycle_manifest.json")
                          .read_text(encoding="utf-8"))
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact = (root / "ranker_reconstructed_code" / "tools" /
                "replay_debug" / "artifacts" / "runs" /
                f"morph_cycle_parity_{stamp}")
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    command = [
        "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", str(trace_script), "-ReplayPath", manifest["replay"],
        "-StartFrame", str(args.start_frame), "-EndFrame", str(args.end_frame),
        "-OutputDirectory", str(artifact),
        "-TimeoutSeconds", str(args.timeout_seconds),
        "-TraceIntervalMs", str(args.trace_interval_ms),
    ]
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
        trace.get("last_exact_frame") is not None and not trace.get("pair_gaps"))
    observed_types = set(trace.get("semantic_coverage", {}).get("unit_types", []))
    terminal_types = {
        row.get("type") for row in
        (trace.get("terminal_exact_state") or {}).get("player_units", {}).values()
    }
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    failures = 0
    for case in manifest["cases"]:
        entered = case["morph_unit_id"] in observed_types
        exited = case["source_unit_id"] in terminal_types
        verdict = "exact" if continuous_exact and entered and exited else (
            "not_exercised" if continuous_exact else "divergent")
        row = {
            "kind": "morph_cycle", **case,
            "result": verdict,
            "morph_type_seen": entered,
            "source_type_restored": exited,
            "replay": manifest["replay"],
            "replay_sha256": manifest["replay_sha256"],
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
        failures += verdict != "exact"
    recorded["cases"] = sorted(records.values(), key=lambda row: tuple(
        str(value) for value in record_key(row)))
    atomic_json(results_path, recorded)
    print(f"{'PASS' if not failures else f'FAIL({failures})'}: "
          f"bindings={len(manifest['cases'])} "
          f"exact_frames={trace.get('exact_pair_count', 0)} "
          f"artifacts={artifact}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
