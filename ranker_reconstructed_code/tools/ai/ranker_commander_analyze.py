"""Summarize completed commander RLOs and inspect economy/squad decisions.

Examples:
  python ranker_commander_analyze.py summary <evaluation-directory> --output analysis.json
  python ranker_commander_analyze.py timeline <game-directory> --step 1000
  python ranker_commander_analyze.py timeline <game-directory> --transfers --start 8000 --end 10000

Counts and positions reconstructed from the fp16 observation are approximate;
frames, actions and terminal results are exact. No privileged fields are used.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import statistics

import numpy as np

from ranker_commander_rollout import read_rollout


def rollout_path(path):
    path = Path(path)
    return path if path.suffix == ".rlo" else path / "output" / "commander.rlo"


def summarize_game(path):
    episode = read_rollout(rollout_path(path), teacher=True)
    try:
        records = episode.decisions
        actions = records["action"]
        frames = records["frame"]
        macros = Counter(map(int, actions[:, 0]))
        reversed_guard = reversed_raid = 0
        trips = []
        launched = None
        for i, (frame, action) in enumerate(zip(frames, actions)):
            macro = int(action[0])
            if i:
                pair = (int(actions[i - 1, 0]), macro)
                reversed_guard += pair in ((38, 39), (39, 38))
                reversed_raid += pair in ((40, 41), (41, 40))
            if macro == 40:
                launched = int(frame)
            elif macro == 41 and launched is not None:
                trips.append(int(frame) - launched)
                launched = None
        attack = (actions[:, 2] == 1) & np.isin(actions[:, 3], [2, 3])
        # Piecewise-constant estimate between recorded decisions, not exact
        # frame telemetry. The actor's idle-worker feature saturates at ten.
        durations = np.diff(np.append(frames.astype(np.int64), int(episode.terminal["frame"])))
        observed_frames = int(durations.sum())
        idle = records["vector"][:, 12].astype(np.float64) * 10
        mean_idle = float(np.average(idle, weights=durations)) if observed_frames else None
        return {
            "game": rollout_path(path).parent.parent.name,
            "seed": episode.seed,
            "status": int(episode.terminal["status"]),
            "end_frame": int(episode.terminal["frame"]),
            "decisions": len(records),
            "macro_counts": dict(sorted(macros.items())),
            "event_counts": dict(sorted(Counter(map(int, records["event"])).items())),
            "consecutive_guard_reversals": reversed_guard,
            "consecutive_raid_reversals": reversed_raid,
            "scout_trip_frames": trips,
            "scout_recalls_within_32_frames": sum(t <= 32 for t in trips),
            "first_main_attack_frame": int(frames[attack][0]) if attack.any() else None,
            "observed_frames": observed_frames,
            "mean_idle_workers_estimate": mean_idle,
            "idle_feature_saturated_frame_fraction_estimate":
                float(np.average(idle >= 10, weights=durations)) if observed_frames else None,
        }
    finally:
        episode.close()


def summarize(path):
    path = Path(path)
    # A job receipt is written only after the engine and RLO validation finish.
    games = [summarize_game(p.parent.parent) for p in sorted(path.glob("game_*/output/job.json"))
             if json.loads(p.read_text(encoding="utf-8")).get("valid")]
    macros = Counter()
    for game in games:
        macros.update(game["macro_counts"])
    trips = [t for game in games for t in game["scout_trip_frames"]]
    idle_means = [g["mean_idle_workers_estimate"] for g in games if g["mean_idle_workers_estimate"] is not None]
    return {
        "directory": str(path.resolve()),
        "completed_valid_games": len(games),
        "wins": sum(g["status"] == 1 for g in games),
        "macro_counts": dict(sorted(macros.items())),
        "consecutive_guard_reversals": sum(g["consecutive_guard_reversals"] for g in games),
        "consecutive_raid_reversals": sum(g["consecutive_raid_reversals"] for g in games),
        "scout_recalls_within_32_frames": sum(g["scout_recalls_within_32_frames"] for g in games),
        "median_scout_trip_frames": statistics.median(trips) if trips else None,
        "mean_of_game_idle_worker_estimates": statistics.mean(idle_means) if idle_means else None,
        "idle_estimate_method": "decision samples held to next decision; idle count clipped at 10; equal game weights",
        "games": games,
    }


def timeline(args):
    episode = read_rollout(rollout_path(args.path), teacher=True)
    try:
        print("frame action[8] res workers army Wown Wvisible Wmemory near | MAIN(count,W,x,y,intent,anchor) GUARD RAID")
        next_frame = args.start
        for record in episode.decisions:
            frame = int(record["frame"])
            if frame < args.start or frame > args.end:
                continue
            if args.transfers:
                if record["action"][0] < 38:
                    continue
            elif frame < next_frame:
                continue
            next_frame = frame + args.step
            z = record["vector"].astype(np.float32)
            squads = []
            for q in range(3):
                off = 166 + q * 21
                squads.append(tuple(round(float(value)) for value in (
                    z[off] * 20, z[off + 1] * 8000, z[off + 3] * 4096,
                    z[off + 4] * 4096, z[off + 12] * 8, z[off + 13] * 16)))
            print(frame, record["action"].tolist(),
                  round(float(np.expm1(z[8] * np.log1p(20000)))),
                  *(round(float(z[i] * scale)) for i, scale in
                    [(11, 40), (26, 40), (27, 20000), (35, 20000), (36, 20000), (46, 10000)]),
                  "|", *squads)
    finally:
        episode.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    summary = commands.add_parser("summary")
    summary.add_argument("path", type=Path)
    summary.add_argument("--output", type=Path)
    view = commands.add_parser("timeline")
    view.add_argument("path", type=Path)
    view.add_argument("--step", type=int, default=1000)
    view.add_argument("--start", type=int, default=0)
    view.add_argument("--end", type=int, default=60000)
    view.add_argument("--transfers", action="store_true")
    args = parser.parse_args()
    if args.command == "timeline":
        if args.step <= 0 or args.start < 0 or args.end < args.start:
            parser.error("step must be positive and 0 <= start <= end")
        timeline(args)
    else:
        result = summarize(args.path)
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in result.items() if k != "games"}, indent=2))


if __name__ == "__main__":
    main()
