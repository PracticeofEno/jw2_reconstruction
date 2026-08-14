#!/usr/bin/env python3
"""Compare actual resource harvesting for all selector-seven unit types."""

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
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--reuse-artifact", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "harvest_manifest.json")
                          .read_text(encoding="utf-8"))
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact = (args.reuse_artifact.resolve() if args.reuse_artifact else
                root / "ranker_reconstructed_code" / "tools" /
                "replay_debug" / "artifacts" / "runs" /
                f"harvest_parity_{stamp}")
    result_path = artifact / "result.json"
    if args.reuse_artifact:
        trace = (json.loads(result_path.read_text(encoding="utf-8"))
                 if result_path.exists() else
                 {"pass": False, "reason": f"missing reused trace: {result_path}"})
    else:
        completed = subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script), "-ReplayPath", manifest["replay"],
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(manifest["end_frame"]),
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
    failures = 0
    for case in manifest["cases"]:
        state_range = ranges.get(str(case["unit_slot"]), {})
        states = state_range.get("states", [])
        valid_states = (set(range(0x53, 0x59)) if case["unit_id"] == 16
                        else set(range(0x28, 0x2E)))
        state_seen = bool(set(states) & valid_states)
        cargo = state_range.get("cargo", {})
        harvested = cargo.get("max", 0) > 0
        verdict = ("exact" if continuous and state_seen and harvested else
                   ("not_exercised" if continuous else "divergent"))
        row = {
            "kind": "direct_action", "action_selector": 7,
            "unit_id": case["unit_id"], "unit_name": case["unit_name"],
            "unit_slot": case["unit_slot"], "result": verdict,
            "resource_tile": case["resource_tile"],
            "resource_initial_amount": case["resource_initial_amount"],
            "harvest_state_seen": state_seen, "observed_states": states,
            "cargo_range": cargo,
            "replay": manifest["replay"],
            "replay_sha256": manifest["replay_sha256"],
            "rebuild_sha256": trace.get("sha256"),
            "trace_artifact": artifact.relative_to(root).as_posix(),
            "frame_range": [args.start_frame, manifest["end_frame"] - 1],
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
          f"exact_frames={trace.get('exact_pair_count', 0)} "
          f"artifact={artifact}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
