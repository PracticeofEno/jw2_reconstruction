#!/usr/bin/env python3
"""Compare all player-operable load/unload cycles with the original."""

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
    parser.add_argument("--end-frame", type=int, default=220)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((tool_dir / "transport_cycle_manifest.json")
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
                     f"transport_cycle_parity_{stamp}")
    artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0
    for number, batch in enumerate(batches, 1):
        label = f"B{batch['batch_index']:02d}"
        artifact = artifact_root / label
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"pairs={len(batch['cases'])}", flush=True)
        completed = subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script), "-ReplayPath", batch["replay"],
            "-StartFrame", str(args.start_frame), "-EndFrame", str(args.end_frame),
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
        ranges = trace.get("semantic_coverage", {}).get(
            "player_unit_state_ranges", {})
        terminal_units = (trace.get("terminal_exact_state") or {}).get(
            "player_units", {})
        batch_failures = 0
        for case in batch["cases"]:
            carrier_range = ranges.get(str(case["carrier_slot"]), {})
            passenger_range = ranges.get(str(case["passenger_slot"]), {})
            terminal_carrier = terminal_units.get(str(case["carrier_slot"]), {})
            terminal_passenger = terminal_units.get(str(case["passenger_slot"]), {})
            cargo = carrier_range.get("cargo", {})
            loaded = (cargo.get("max", 0) >= case["passenger_size"] and
                      0x45 in passenger_range.get("states", []))
            unloaded_visible = (
                terminal_carrier.get("cargo") == 0 and
                terminal_passenger.get("type") == case["passenger_unit_id"] and
                (terminal_passenger.get("runtime_flags", 0) & 0x80) == 0 and
                (terminal_passenger.get("runtime_flags", 0) & 1) != 0 and
                terminal_passenger.get("state") != 0x45)
            verdict = ("exact" if continuous and loaded and unloaded_visible else
                       ("not_exercised" if continuous else "divergent"))
            row = {
                "kind": "transport_cycle", **case,
                "result": verdict,
                "load_observed": loaded,
                "unload_visible_observed": unloaded_visible,
                "carrier_cargo_range": cargo,
                "passenger_states": passenger_range.get("states", []),
                "terminal_carrier": terminal_carrier,
                "terminal_passenger": terminal_passenger,
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
              f"exact_frames={trace.get('exact_pair_count', 0)}", flush=True)
    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
