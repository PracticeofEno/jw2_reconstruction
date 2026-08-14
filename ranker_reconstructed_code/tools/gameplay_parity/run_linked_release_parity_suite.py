#!/usr/bin/env python3
"""Compare all linked pair/triad releases with the original."""

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
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "linked_release_manifest.json")
                          .read_text(encoding="utf-8"))
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact = (root / "ranker_reconstructed_code" / "tools" /
                "replay_debug" / "artifacts" / "runs" /
                f"linked_release_parity_{stamp}")
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
    result_path = artifact / "result.json"
    trace = (json.loads(result_path.read_text(encoding="utf-8"))
             if completed.returncode == 0 and result_path.exists()
             else {"pass": False, "reason": (
                 completed.stderr.strip() or completed.stdout.strip() or
                 f"trace command exited {completed.returncode}")})
    continuous = bool(
        trace.get("pass") and trace.get("first_exact_frame") is not None and
        trace.get("last_exact_frame") is not None and not trace.get("pair_gaps"))
    terminal_units = (trace.get("terminal_exact_state") or {}).get(
        "player_units", {})
    failures = 0
    for case in manifest["cases"]:
        group = [terminal_units.get(str(slot), {}) for slot in case["source_slots"]]
        result_seen = any(row.get("type") == case["result_unit_id"] for row in group)
        hidden_children = sum(
            bool(row.get("runtime_flags", 0) & 0x80) for row in group)
        # During the linked-release animation the non-parent members are hidden.
        # After the animation finishes the original game frees those member slots,
        # so an empty terminal row is the completed form of the same consumption.
        consumed_children = sum(
            not row or bool(row.get("runtime_flags", 0) & 0x80)
            for row in group)
        exercised = result_seen and consumed_children >= len(group) - 1
        verdict = ("exact" if continuous and exercised else
                   ("not_exercised" if continuous else "divergent"))
        row = {
            "kind": "linked_release", **case,
            "result": verdict, "result_type_seen": result_seen,
            "hidden_child_count": hidden_children,
            "consumed_child_count": consumed_children,
            "terminal_group": group,
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
