"""Compare game-worker throughput on this host using identical commander jobs.

This is a performance measurement, never a policy promotion or victory gate.
Keep other game collectors and training jobs stopped during the comparison.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import time

from ranker_commander_eval import run_games


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--io", type=Path, required=True)
    parser.add_argument("--workers", type=int, nargs="+", default=[2, 3, 4])
    parser.add_argument("--games", type=int, default=12)
    parser.add_argument("--max-frames", type=int, default=20000)
    parser.add_argument("--teacher", action="store_true")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args(argv)
    if min(*args.workers, args.games, args.max_frames, args.seed) < 1:
        parser.error("counts, frames and seed must be positive")
    if args.games % 4 or any(args.games % count for count in args.workers):
        parser.error("--games must be divisible by 4 and every candidate worker count for equal full waves")
    args.io.mkdir(parents=True, exist_ok=False)
    jobs = [{"seed": args.seed + game, "tribe": game % 4,
             "max_frames": args.max_frames, "curriculum": 2}
            for game in range(args.games)]
    summary = {
        "purpose": "host_throughput_only", "pid": os.getpid(),
        "platform": platform.platform(), "processor": platform.processor(),
        "logical_processors": os.cpu_count(), "teacher": args.teacher,
        "weights_sha256": hashlib.sha256(args.weights.read_bytes()).hexdigest(),
        "exe_sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        "jobs": jobs, "measurements": [], "complete": False,
    }

    def save():
        temporary = args.io / "summary.json.tmp"
        temporary.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        temporary.replace(args.io / "summary.json")

    save()
    reference = None
    for workers in args.workers:
        print(json.dumps({"starting_workers": workers, "games": len(jobs)}), flush=True)
        started = time.monotonic()
        reports = run_games(args.install_dir, args.weights, args.io / f"workers_{workers}", jobs,
                            workers=workers, teacher=args.teacher, deterministic=True,
                            executable=args.exe)
        elapsed = time.monotonic() - started
        valid = [report for report in reports if report.get("valid")]
        identity = [(r.get("start_pair"), r.get("status"), r.get("end_frame"),
                     r.get("decisions")) for r in reports]
        consistent = reference is None or reference == identity
        if reference is None and len(valid) == len(jobs):
            reference = identity
        measurement = {
            "workers": workers, "games": len(reports), "valid_games": len(valid),
            "wall_seconds": elapsed,
            "frames_per_second": sum(r["end_frame"] for r in valid) / elapsed,
            "games_per_hour": len(valid) * 3600 / elapsed,
            "median_game_seconds": statistics.median(r["wall_seconds"] for r in reports),
            "same_case_outcomes": consistent,
            "startup_relaunches": sum(r.get("startup_relaunches", 0) for r in reports),
            "invalid_reasons": [r["reason"] for r in reports if not r.get("valid")],
        }
        summary["measurements"].append(measurement)
        save()
        print(json.dumps(measurement), flush=True)
        if len(valid) != len(jobs) or not consistent:
            raise RuntimeError("worker comparison failed validity/consistency; inspect job receipts before continuing")
    fastest = max(row["frames_per_second"] for row in summary["measurements"])
    # A small throughput tradeoff leaves CPU headroom on a shared laptop.
    summary["selected_workers"] = min(row["workers"] for row in summary["measurements"]
        if row["frames_per_second"] >= fastest * 0.95)
    summary["selection_rule"] = "fewest workers within 5% of highest measured throughput"
    summary["complete"] = True
    save()
    print(json.dumps({"selected_workers": summary["selected_workers"]}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
