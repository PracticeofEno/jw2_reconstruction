"""Evaluate a Computer(AI) high-level policy vs the built-in Owner AI.

Runs N deterministic (seeded) games in which the policy drives owner 1 over the
IPC socket and the built-in AI plays owner 2, then reports how owner 1 fares:
mean final unit count, the opponent's, and the "win-by-units" rate (owner 1 ends
with strictly more units).  This is the yardstick for "is the policy getting
stronger" across the pipeline (random -> imitation -> PPO).

  python ranker_eval.py --install-dir <deploy> --policy ppo_policy.pt --games 8
  python ranker_eval.py --install-dir <deploy>                 # random-legal
  python ranker_eval.py --install-dir <deploy> --policy imitation_policy.npz

Games are run sequentially (they share the deploy dir's result files), so do not
run this while a trainer is producing games in the same directory.
"""

from __future__ import annotations

import argparse
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from ranker_ipc_server import serve_match, _load_policy


def _one_game(install: Path, policy_factory, port: int, seed: int,
              max_frames: int, index: int, opp_tribe: int | None = None):
    # Fresh policy per match (docs/1순위.md 5.1): stateful policies (per-owner
    # action history, sampling stream) must never be shared across the thread
    # pool — one game's history writes/reset would corrupt another's.
    policy = policy_factory(seed)
    out_dir = install / "evalout" / f"w{index}"
    info = serve_match(install, policy, port, seed, max_frames,
                       out_dir=out_dir, net_offset=index + 1, quiet=True,
                       opp_tribe=opp_tribe)
    own = opp = own_value = opp_value = 0
    own_bld = opp_bld = 0
    result_path = out_dir / "ai_selfplay_result.json"
    data = None
    if result_path.exists():
        data = json.loads(result_path.read_text())
        owners = {o["owner"]: o for o in data.get("owners", [])}
        own = owners.get(1, {}).get("units", 0)
        opp = owners.get(2, {}).get("units", 0)
        own_value = owners.get(1, {}).get("unit_value", own)
        opp_value = owners.get(2, {}).get("unit_value", opp)
        own_bld = owners.get(1, {}).get("buildings", 1 if own else 0)
        opp_bld = owners.get(2, {}).get("buildings", 1 if opp else 0)
    reason = info.get("end", {}).get("reason", "?")
    if (not reason or reason == "?") and data is not None:
        # The IPC end message can be lost on a crash; the result JSON keeps
        # the authoritative end reason.
        reason = data.get("reason", "?")
    print(f"  seed {seed}: own {own:3d}u/{own_value:5d}v/{own_bld:2d}b vs "
          f"opp {opp:3d}u/{opp_value:5d}v/{opp_bld:2d}b  ({reason})",
          flush=True)
    return {"seed": seed, "own": own, "opp": opp, "own_value": own_value,
            "opp_value": opp_value, "own_bld": own_bld, "opp_bld": opp_bld,
            "reason": reason, "decisions": info["steps"],
            "no_result": data is None}


def evaluate(install_dir: Path, policy_factory, base_port: int, seeds,
             max_frames: int, workers: int = 8,
             opp_tribe: int | None = None):
    """Run one eval game per seed.  ``policy_factory(seed)`` must return a
    FRESH policy instance for that match (never a shared object)."""
    install = Path(install_dir)
    with ThreadPoolExecutor(max_workers=min(workers, len(seeds))) as ex:
        rows = list(ex.map(
            lambda a: _one_game(install, policy_factory, base_port + a[0],
                                a[1], max_frames, a[0], opp_tribe),
            list(enumerate(seeds))))
    return rows


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--policy", type=Path, default=None,
                        help="imitation_policy.npz or ppo_policy.pt; "
                             "omit for random-legal")
    parser.add_argument("--games", type=int, default=8)
    parser.add_argument("--seed", type=int, default=9000,
                        help="base seed; game i uses seed+i")
    parser.add_argument("--max-frames", type=int, default=100000)
    parser.add_argument("--port", type=int, default=5700)
    parser.add_argument("--workers", type=int, default=8,
                        help="parallel eval games")
    parser.add_argument("--stochastic", action="store_true",
                        help="sample from the policy distribution (deployment "
                             "behavior, fixed torch seed) instead of argmax")
    parser.add_argument("--opp-tribe", type=int, default=None,
                        help="-AITRIBE for the built-in opponent: 0=Primitive "
                             "1=Elf 2=Tyrano 3=Demon, 4=rotate by seed; "
                             "omit for the game default (Tyrano)")
    args = parser.parse_args(argv)

    # Announce the policy once, then hand evaluate() a FACTORY: each match
    # builds its own policy instance (per-owner history + private sampling
    # generator seeded by the match seed), so parallel matches are isolated
    # and workers=1 vs workers=N produce the same per-seed results.
    _load_policy(args.policy, args.seed, stochastic=args.stochastic)

    def policy_factory(match_seed):
        return _load_policy(args.policy, match_seed,
                            stochastic=args.stochastic, verbose=False)

    seeds = [args.seed + i for i in range(args.games)]
    rows = evaluate(args.install_dir, policy_factory, args.port, seeds,
                    args.max_frames, workers=args.workers,
                    opp_tribe=args.opp_tribe)

    n = len(rows)
    own = sum(r["own"] for r in rows) / n
    opp = sum(r["opp"] for r in rows) / n
    own_v = sum(r["own_value"] for r in rows) / n
    opp_v = sum(r["opp_value"] for r in rows) / n
    # Win = building-elimination first, then army value, then count.  Report
    # elimination wins and timeouts separately: a material edge at the frame
    # cap is NOT the same evidence as razing the enemy (docs/1순위.md 5.6).
    wins = sum(1 for r in rows
               if (r["own_bld"] > 0, r["own_value"], r["own"]) >
                  (r["opp_bld"] > 0, r["opp_value"], r["opp"]))
    elim_wins = sum(1 for r in rows if r["opp_bld"] == 0 and r["own_bld"] > 0)
    timeouts = sum(1 for r in rows if r["reason"] == "max_frames")
    failed = sum(1 for r in rows if r["no_result"])
    print("\n=== eval summary ===")
    print(f"games {n} | mean own {own:.1f}u/{own_v:.0f}v vs "
          f"opp {opp:.1f}u/{opp_v:.0f}v | win {wins}/{n} ({100*wins/n:.0f}%)"
          f" [elim {elim_wins}, timeout {timeouts}"
          + (f", NO-RESULT {failed}" if failed else "") + "]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
