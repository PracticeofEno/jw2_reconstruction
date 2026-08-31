"""Head-to-head league runner: two policies play each other (-AIVS).

Each seed is played as a MIRRORED PAIR — game 1: A as owner 1 vs B as owner 2;
game 2 (same seed): B as owner 1 vs A as owner 2 — so start-position asymmetry
cancels and the aggregate win rate reflects policy strength, not spawn luck.
A game's winner is decided elimination-first (zero buildings = out), then army
value, then unit count; a frame-cap timeout with both sides alive is a DRAW by
default (--timeout-material restores the old material tiebreak).  Policies are
constructed PER MATCH from factories so parallel games never share stateful
policy objects (per-owner history, sampling stream).

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

import numpy as np

from ranker_ipc_server import serve_match, _load_policy


def paired_bootstrap_ci(pair_scores, n_boot: int = 10000, seed: int = 0):
    """95% bootstrap CI of the mean per-PAIR score (docs/1순위.md 8.2: the
    mirrored pair, not the single game, is the exchangeable unit — the two
    games of a pair share a map seed).  Scores: win=1, draw=0.5, loss=0."""
    arr = np.asarray(pair_scores, dtype=np.float64)
    if len(arr) == 0:
        return 0.5, 0.5
    rng = np.random.default_rng(seed)
    idx = rng.integers(0, len(arr), size=(n_boot, len(arr)))
    means = arr[idx].mean(axis=1)
    return float(np.quantile(means, 0.025)), float(np.quantile(means, 0.975))


def _one_game(install: Path, make_owner1, make_owner2, port: int, seed: int,
              max_frames: int, index: int):
    # Fresh policy instances per match (docs/1순위.md 5.1): TorchPolicy keeps
    # per-owner action history and a sampling stream, so sharing one object
    # across the thread pool mixes state between concurrent games and lets a
    # finished game's reset() wipe another game's history mid-flight.
    # Disjoint per-(game, side) sampling seeds: 4 slots per pair seed.
    salt = seed * 4 + (index % 2) * 2
    pol_owner1 = make_owner1(salt)
    pol_owner2 = make_owner2(salt + 1)
    out_dir = install / "leagueout" / f"w{index}"
    serve_match(install, pol_owner1, port, seed, max_frames,
                out_dir=out_dir, net_offset=index + 1, quiet=True,
                policy2=pol_owner2, versus=True)
    result_path = out_dir / "ai_selfplay_result.json"
    own1 = own2 = 0
    val1 = val2 = 0
    b1 = b2 = 0
    reason = "no_result"
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
        reason = data.get("reason", "?")
    return own1, own2, val1, val2, b1, b2, reason


def run_league(install_dir: Path, make_policy_a, make_policy_b, pairs: int,
               base_seed: int, max_frames: int, base_port: int,
               workers: int = 8, timeout_is_draw: bool = True):
    """Mirrored-pair head-to-head.

    ``make_policy_a`` / ``make_policy_b`` are FACTORIES (``f(seed) -> policy``)
    called once per game per side, so every match gets isolated policy state.

    ``timeout_is_draw`` (default): a game that hit the frame cap without an
    elimination is a DRAW — a material edge at timeout is not a win, and
    promoting on it selects hoarding policies (docs/1순위.md 5.6/5.11).  Pass
    False to restore the old elimination>value>count tiebreak at timeout."""
    install = Path(install_dir)
    # Each pair = two games (A first / B first) on the same seed.
    jobs = []
    for pair in range(pairs):
        seed = base_seed + pair
        jobs.append(("A1", seed, make_policy_a, make_policy_b))
        jobs.append(("B1", seed, make_policy_b, make_policy_a))

    def run(job_index):
        side, seed, f1, f2 = jobs[job_index]
        return (side, seed) + _one_game(install, f1, f2,
                                        base_port + job_index, seed,
                                        max_frames, job_index)

    with ThreadPoolExecutor(max_workers=min(workers, len(jobs))) as ex:
        rows = list(ex.map(run, range(len(jobs))))

    a_wins = b_wins = draws = timeouts = failures = 0
    a_units_total = b_units_total = 0
    a_game_scores = []
    for side, seed, own1, own2, val1, val2, b1, b2, reason in rows:
        a_units = own1 if side == "A1" else own2
        b_units = own2 if side == "A1" else own1
        a_value = val1 if side == "A1" else val2
        b_value = val2 if side == "A1" else val1
        a_bld = b1 if side == "A1" else b2
        b_bld = b2 if side == "A1" else b1
        a_units_total += a_units
        b_units_total += b_units
        if reason == "max_frames":
            timeouts += 1
        if reason == "no_result":
            failures += 1
        # Judgment order: building-elimination first (no buildings = out,
        # regardless of stray units), then army VALUE, then count.  A capped
        # game (or a failed one) is a draw under timeout_is_draw.
        a_score = (a_bld > 0, a_value, a_units)
        b_score = (b_bld > 0, b_value, b_units)
        undecided = reason == "no_result" or (
            timeout_is_draw and reason == "max_frames"
            and a_bld > 0 and b_bld > 0)
        if undecided or a_score == b_score:
            draws += 1
            verdict = "D"
            a_game_scores.append(0.5)
        elif a_score > b_score:
            a_wins += 1
            verdict = "A"
            a_game_scores.append(1.0)
        else:
            b_wins += 1
            verdict = "B"
            a_game_scores.append(0.0)
        print(f"  seed {seed} [{'A=o1' if side == 'A1' else 'B=o1'}] "
              f"A {a_units:3d}u/{a_value:5d}v/{a_bld:2d}b vs "
              f"B {b_units:3d}u/{b_value:5d}v/{b_bld:2d}b "
              f"-> {verdict} ({reason})", flush=True)
    games = len(rows)
    # rows follow jobs order (ex.map preserves it): games 2p and 2p+1 form
    # mirrored pair p.
    pair_scores = [(a_game_scores[i] + a_game_scores[i + 1]) / 2.0
                   for i in range(0, len(a_game_scores) - 1, 2)]
    ci_lo, ci_hi = paired_bootstrap_ci(pair_scores)
    return {
        "games": games, "a_wins": a_wins, "b_wins": b_wins, "draws": draws,
        "timeouts": timeouts, "failures": failures,
        "a_mean_units": a_units_total / games,
        "b_mean_units": b_units_total / games,
        "a_pair_score": (float(np.mean(pair_scores)) if pair_scores else 0.5),
        "a_score_ci95": (ci_lo, ci_hi),
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
    parser.add_argument("--stochastic", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="sample policies (deployment behavior; default). "
                             "--no-stochastic = deterministic argmax "
                             "(previously the flag was store_true with "
                             "default=True, so argmax was unselectable)")
    parser.add_argument("--timeout-material",
                        action="store_true",
                        help="score a frame-cap timeout by the old "
                             "elimination>value>count tiebreak instead of a "
                             "draw (default: timeout = draw)")
    args = parser.parse_args(argv)

    print(f"A: {args.policy_a or 'random'}   B: {args.policy_b or 'random'} "
          f"({'stochastic' if args.stochastic else 'argmax'})")

    def make_a(match_seed):
        return _load_policy(args.policy_a, match_seed,
                            stochastic=args.stochastic, verbose=False)

    def make_b(match_seed):
        return _load_policy(args.policy_b, match_seed,
                            stochastic=args.stochastic, verbose=False)

    summary = run_league(args.install_dir, make_a, make_b, args.pairs,
                         args.seed, args.max_frames, args.port, args.workers,
                         timeout_is_draw=not args.timeout_material)

    print("\n=== league summary ===")
    print(f"A: {args.policy_a or 'random'}   B: {args.policy_b or 'random'}")
    print(f"games {summary['games']} | A wins {summary['a_wins']} | "
          f"B wins {summary['b_wins']} | draws {summary['draws']} "
          f"(timeouts {summary['timeouts']}, failures {summary['failures']})")
    lo, hi = summary["a_score_ci95"]
    print(f"A pair score {summary['a_pair_score']:.3f} "
          f"(95% CI [{lo:.3f}, {hi:.3f}]; >0.5 = A stronger)")
    print(f"mean units A {summary['a_mean_units']:.1f} vs "
          f"B {summary['b_mean_units']:.1f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
