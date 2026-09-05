# -*- coding: utf-8 -*-
"""Assault-shadow collection: the v10b legacy stack (IPC macro + autopilot +
micro executor) plays a THROTTLED built-in opponent (-AIOPPSLOW) and the
micro executor's desired orders are tapped as entity KEEP/ISSUE labels
(-AISHADOW).  The point is base-assault labels: the current entity BC data
came from defensive games, so the policy never learned the cross-map march
that converts a won position into an elimination.

    python collect_assault_shadow.py --install-dir <deploy> --games 8 \
        --opp-slow 16 --max-frames 36000
"""
from __future__ import annotations

import argparse
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from ranker_ipc_server import serve_match, _load_policy
from ranker_rl_env import ACTION_NAMES

A = {name: i for i, name in enumerate(ACTION_NAMES)}


class AssaultTeacher:
    """v10b macro with forced aggression: after the opening, prefer the army
    attack objectives whenever they are legal.  v10b argmax alone sits vs a
    throttled opponent (it learned to attack REACTIVELY in the league), so
    plain collection yields almost no base-assault labels — the whole point
    of this dataset.  The autopilot keeps the economy/production breathing
    while the policy holds the attack objective."""

    def __init__(self, inner, attack_from_frame: int = 8000):
        self.inner = inner
        self.attack_from_frame = attack_from_frame
        self.frame = 0
        self.decisions = 0

    def act(self, feat, mask, tmask=None, **kwargs):
        base = (self.inner.act(feat, mask, tmask, **kwargs)
                if getattr(self.inner, "wants_target_mask", False)
                else self.inner.act(feat, mask))
        self.frame += 64  # decision cadence upper bound; only a rough clock
        self.decisions += 1
        if self.frame >= self.attack_from_frame:
            # Attack objectives may repeat every decision (same objective);
            # the EXPLORE objectives must not — re-issuing each decision
            # resets the walker and it never crosses the map, so they are
            # throttled to every 16th decision (~1000 frames).
            for name in ("attack_enemy_base", "search_enemy_base"):
                if mask[A[name]]:
                    return A[name]
            if self.decisions % 16 == 0:
                for name in ("explore_frontier", "roam_scout"):
                    if mask[A[name]]:
                        return A[name]
        return base

    def __getattr__(self, item):
        return getattr(self.inner, item)


def one_game(install: Path, policy_path: Path, port: int, seed: int,
             max_frames: int, index: int, out_root: Path, opp_slow: int):
    policy = AssaultTeacher(_load_policy(policy_path, seed, stochastic=False))
    # The legacy -AIOUT parser needs an absolute path (a relative one is
    # silently dropped and every output lands on shared install-root files).
    out_dir = (out_root / f"g{index}").resolve()
    info = serve_match(install, policy, port, seed, max_frames,
                       out_dir=out_dir, net_offset=200 + index, quiet=True,
                       extra_flags=["-AISHADOW", f"-AIOPPSLOW:{opp_slow}"],
                       timeout=1200.0)
    result = {}
    result_path = out_dir / "ai_selfplay_result.json"
    if result_path.exists():
        result = json.loads(result_path.read_text())
    owners = {o["owner"]: o for o in result.get("owners", [])}
    we, opp = owners.get(1, {}), owners.get(2, {})
    win = (not opp.get("alive", True)) and bool(we.get("alive"))
    shadow = sorted(out_dir.glob("ai_entity_shadow_*.bin"))
    return {
        "index": index, "seed": seed, "win": win,
        "end_frame": result.get("end_frame"),
        "reason": result.get("reason"),
        "we_units": we.get("units"), "opp_units": opp.get("units"),
        "opp_buildings": opp.get("buildings"),
        "opp_building_lost": opp.get("building_value_lost"),
        "shadow_bytes": sum(p.stat().st_size for p in shadow),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--policy", type=Path, default=None,
                        help="legacy macro checkpoint (default: v10b champion)")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--games", type=int, default=8)
    parser.add_argument("--seed0", type=int, default=7200)
    parser.add_argument("--port", type=int, default=5800)
    parser.add_argument("--opp-slow", type=int, default=16)
    parser.add_argument("--max-frames", type=int, default=36000)
    args = parser.parse_args()

    policy_path = args.policy or (args.install_dir /
                                  "ppo_policy.selfplay.v10b.pt")
    args.out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=args.games) as pool:
        futures = [pool.submit(one_game, args.install_dir, policy_path,
                               args.port + i, args.seed0 + i,
                               args.max_frames, i, args.out, args.opp_slow)
                   for i in range(args.games)]
        results = [f.result() for f in futures]
    wins = sum(r["win"] for r in results)
    for r in sorted(results, key=lambda r: r["index"]):
        print("g%d seed %d f%s %-10s WE u%s | OPP u%s bld %s blost %s | "
              "shadow %dB -> %s" % (
                  r["index"], r["seed"], r["end_frame"],
                  str(r["reason"])[:10], r["we_units"], r["opp_units"],
                  r["opp_buildings"], r["opp_building_lost"],
                  r["shadow_bytes"], "WIN" if r["win"] else "no"))
    print("wins %d/%d" % (wins, len(results)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
