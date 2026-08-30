"""Head-to-head league runner: two policies play each other (-AIVS).

Each seed is played as a MIRRORED PAIR — game 1: A as owner 1 vs B as owner 2;
game 2 (same seed): B as owner 1 vs A as owner 2 — so start-position asymmetry
cancels and the aggregate win rate reflects policy strength, not spawn luck.
A game's winner is the side with strictly more final units (elimination counts
via units too); equal units = draw.

  python ranker_league.py --install-dir <deploy> \
      --policy-a ppo_policy.bc.pt --policy-b ppo_policy.it030.pt \
      --pairs 6 --max-frames 8000

This is the measurement half of a self-play league; the training half feeds
-AIVS episode data (both owners logged) back into PPO.
"""

from __future__ import annotations

import argparse
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from ranker_ipc_server import serve_match, _load_policy


def _one_game(install: Path, pol_owner1, pol_owner2, port: int, seed: int,
              max_frames: int, index: int):
    out_dir = install / "leagueout" / f"w{index}"
    serve_match(install, pol_owner1, port, seed, max_frames,
                out_dir=out_dir, net_offset=index + 1, quiet=True,
                policy2=pol_owner2, versus=True)
    result_path = out_dir / "ai_selfplay_result.json"
    own1 = own2 = 0
    val1 = val2 = 0
    b1 = b2 = 0
    if result_path.exists():
        data = json.loads(result_path.read_text())
        owners = {o["owner"]: o for o in data.get("owners", [])}
        own1 = owners.get(1, {}).get("units", 0)
        own2 = owners.get(2, {}).get("units", 0)
        # unit_value (sum of production costs of surviving units) ranks armies
        # correctly — raw counts treat a 5000-cost ultimate like a 100-cost
        # grunt.  Older result JSONs lack the field; fall back to counts.
        val1 = owners.get(1, {}).get("unit_value", own1)
        val2 = owners.get(2, {}).get("unit_value", own2)
        # Buildings decide elimination (zero buildings = eliminated, whatever
        # mobile units remain).  Old JSONs: approximate with unit counts.
        b1 = owners.get(1, {}).get("buildings", 1 if own1 else 0)
        b2 = owners.get(2, {}).get("buildings", 1 if own2 else 0)
    return own1, own2, val1, val2, b1, b2


def run_league(install_dir: Path, policy_a, policy_b, pairs: int,
               base_seed: int, max_frames: int, base_port: int,
               workers: int = 8):
    install = Path(install_dir)
    # Each pair = two games (A first / B first) on the same seed.
    jobs = []
    for pair in range(pairs):
        seed = base_seed + pair
        jobs.append(("A1", seed, policy_a, policy_b))
        jobs.append(("B1", seed, policy_b, policy_a))

    def run(job_index):
        side, seed, p1, p2 = jobs[job_index]
        return (side, seed) + _one_game(install, p1, p2,
                                        base_port + job_index, seed,
                                        max_frames, job_index)

    with ThreadPoolExecutor(max_workers=min(workers, len(jobs))) as ex:
        rows = list(ex.map(run, range(len(jobs))))

    a_wins = b_wins = draws = 0
    a_units_total = b_units_total = 0
    for side, seed, own1, own2, val1, val2, b1, b2 in rows:
        a_units = own1 if side == "A1" else own2
        b_units = own2 if side == "A1" else own1
        a_value = val1 if side == "A1" else val2
        b_value = val2 if side == "A1" else val1
        a_bld = b1 if side == "A1" else b2
        b_bld = b2 if side == "A1" else b1
        a_units_total += a_units
        b_units_total += b_units
        # Judgment order: building-elimination first (no buildings = out,
        # regardless of stray units), then army VALUE, then count.
        a_score = (a_bld > 0, a_value, a_units)
        b_score = (b_bld > 0, b_value, b_units)
        if a_score > b_score:
            a_wins += 1
        elif b_score > a_score:
            b_wins += 1
        else:
            draws += 1
        print(f"  seed {seed} [{'A=o1' if side == 'A1' else 'B=o1'}] "
              f"A {a_units:3d}u/{a_value:5d}v/{a_bld:2d}b vs "
              f"B {b_units:3d}u/{b_value:5d}v/{b_bld:2d}b", flush=True)
    games = len(rows)
    return {
        "games": games, "a_wins": a_wins, "b_wins": b_wins, "draws": draws,
        "a_mean_units": a_units_total / games,
        "b_mean_units": b_units_total / games,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--policy-a", type=Path, default=None,
                        help=".pt/.npz for side A; omit for random-legal")
    parser.add_argument("--policy-b", type=Path, default=None,
                        help=".pt/.npz for side B; omit for random-legal")
    parser.add_argument("--pairs", type=int, default=6,
                        help="mirrored seed pairs (2 games each)")
    parser.add_argument("--seed", type=int, default=7000)
    parser.add_argument("--max-frames", type=int, default=100000)
    parser.add_argument("--port", type=int, default=6100)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--stochastic", action="store_true", default=True,
                        help="sample policies (deployment behavior; default)")
    args = parser.parse_args(argv)

    policy_a = _load_policy(args.policy_a, args.seed, stochastic=True)
    policy_b = _load_policy(args.policy_b, args.seed + 1, stochastic=True)
    summary = run_league(args.install_dir, policy_a, policy_b, args.pairs,
                         args.seed, args.max_frames, args.port, args.workers)

    print("\n=== league summary ===")
    print(f"A: {args.policy_a or 'random'}   B: {args.policy_b or 'random'}")
    print(f"games {summary['games']} | A wins {summary['a_wins']} | "
          f"B wins {summary['b_wins']} | draws {summary['draws']}")
    print(f"mean units A {summary['a_mean_units']:.1f} vs "
          f"B {summary['b_mean_units']:.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
