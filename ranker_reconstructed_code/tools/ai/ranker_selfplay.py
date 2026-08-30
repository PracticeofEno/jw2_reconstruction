"""Self-play league trainer (AlphaStar-lite): the "AIs fight each other and get
progressively stronger" loop proper.

Each GENERATION fine-tunes a challenger from the current champion with PPO over
-AIVS games where BOTH owners are driven by the challenger network — every game
yields two trajectories (winner's and loser's), so the policy simultaneously
learns to beat and to not-lose-to itself.  The generation then faces a LEAGUE
GATE: mirrored-pair head-to-head vs the champion; only a challenger that wins
the gate becomes the new champion (a rejected challenger is discarded and the
next generation restarts from the champion, so strength never regresses).

  python ranker_selfplay.py --install-dir <deploy> \
      --init ppo_policy.bc.pt --generations 5 --iters-per-gen 8 \
      --games-per-iter 12 --max-frames 8000 --gate-pairs 4

Checkpoints: <out>.gen000.pt is the initial champion; every accepted challenger
is saved as <out>.genNNN.pt and also copied to <out> (the current champion).
"""

from __future__ import annotations

import argparse
import json
import random
import shutil
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import torch

from ranker_ppo import (ActorCritic, DEFAULT_DISCOUNT, N_ACTIONS, N_FEATURES,
                        _army_value, augment_rewards, compute_gae, ppo_update,
                        rollout_versus, _units)
from ranker_league import run_league
from ranker_ipc_server import _load_policy


def save_gate_replays(install: Path, gen: int, promoted: bool,
                      base_seed: int, pairs: int):
    """Preserve gate-match replays so promotions can be WATCHED.

    League job order (run_league): pair p -> index 2p is A1 (challenger =
    owner 1), index 2p+1 is B1 (champion = owner 1), both on seed base+p.
    PROMOTED generations keep every gate game; rejected generations keep only
    the challenger's best win (evidence of what almost worked) to bound disk.
    """
    league_dir = install / "leagueout"
    replays_dir = install / "Replays"

    def copy_game(index, name):
        src = league_dir / f"w{index}" / "ai_selfplay_replay.ply"
        if not src.exists():
            return False
        replays_dir.mkdir(exist_ok=True)
        shutil.copy2(src, replays_dir / f"{name}.ply")
        vpo = src.with_suffix(".vpo")
        if vpo.exists():
            shutil.copy2(vpo, replays_dir / f"{name}.vpo")
        result = league_dir / f"w{index}" / "ai_selfplay_result.json"
        if result.exists():
            shutil.copy2(result, replays_dir / f"{name}.result.json")
        return True

    saved = 0
    if promoted:
        for index in range(pairs * 2):
            side = "A1" if index % 2 == 0 else "B1"
            seed = base_seed + index // 2
            if copy_game(index,
                         f"AI_gate_gen{gen:03d}_PROMOTED_seed{seed}_{side}"):
                saved += 1
    else:
        # Rejected gate: keep 3 random games (user directive — a random
        # sample shows typical play; the single best-win cherry-picked).
        # Seeded by generation so a re-run of the same gen samples the same
        # games.
        picks = random.Random(base_seed + gen).sample(
            range(pairs * 2), min(3, pairs * 2))
        for index in sorted(picks):
            seed = base_seed + index // 2
            side = "A1" if index % 2 == 0 else "B1"
            if copy_game(index,
                         f"AI_gate_gen{gen:03d}_rejected_seed{seed}_{side}"):
                saved += 1
    if saved:
        print(f"  gate replays saved: {saved} -> Replays/AI_gate_gen"
              f"{gen:03d}_*", flush=True)


def update_record_replays(install: Path, io_base: Path, games, seeds,
                          gen: int, record_state: dict):
    """Whenever a game's strongest side beats the all-time unit record, copy
    that game's replay into Replays/ (with .vpo sidecar) so the user can watch
    it in-game.  The record persists across runs via Replays/ai_records.json."""
    replays_dir = install / "Replays"
    registry = replays_dir / "ai_records.json"
    if "best" not in record_state:
        best = 0
        if registry.exists():
            try:
                best = int(json.loads(registry.read_text()).get(
                    "best_units", 0))
            except (json.JSONDecodeError, ValueError):
                best = 0
        record_state["best"] = best
    for index, rolls in enumerate(games):
        if not rolls:
            continue
        result = rolls[0].get("result") or {}
        # A game that burned through the whole safety frame cap is a
        # STALEMATE, not an achievement — record replays are for decisive
        # (elimination) games only (user directive).
        if result.get("reason") == "max_frames":
            continue
        owners = {o.get("owner"): o for o in result.get("owners", [])}
        # Record metric = surviving-army VALUE (production-cost sum) when the
        # exe reports it; a hoard of cheap units is not a record.  Old exes
        # fall back to unit count.
        score = max(
            owners.get(1, {}).get("unit_value",
                                  owners.get(1, {}).get("units", 0)),
            owners.get(2, {}).get("unit_value",
                                  owners.get(2, {}).get("units", 0)))
        if score <= record_state["best"]:
            continue
        src = io_base / f"w{index}" / "ai_selfplay_replay.ply"
        if not src.exists():
            continue
        replays_dir.mkdir(exist_ok=True)
        name = f"AI_record_{score}v_gen{gen}_seed{seeds[index]}"
        shutil.copy2(src, replays_dir / f"{name}.ply")
        vpo = src.with_suffix(".vpo")
        if vpo.exists():
            shutil.copy2(vpo, replays_dir / f"{name}.vpo")
        # Keep the recorded outcome next to the replay so -AIREPLAY headless
        # playback can be machine-compared against it (desync detection).
        result_src = io_base / f"w{index}" / "ai_selfplay_result.json"
        if result_src.exists():
            shutil.copy2(result_src, replays_dir / f"{name}.result.json")
        record_state["best"] = score
        registry.write_text(json.dumps(
            {"best_units": score, "file": f"{name}.ply"}))
        print(f"  NEW RECORD value {score} -> Replays/{name}.ply", flush=True)


def save_net(net, path: Path):
    from ranker_ppo import checkpoint_payload
    torch.save(checkpoint_payload(net), path)


def load_net(path: Path) -> ActorCritic:
    from ranker_ppo import _check_checkpoint_shape, _hidden_of
    ckpt = torch.load(path, map_location="cpu", weights_only=False)
    _check_checkpoint_shape(ckpt, path)
    state = ckpt["state_dict"]
    net = ActorCritic(hidden=ckpt.get("hidden", _hidden_of(state)))
    net.load_state_dict(state)
    return net


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--init", type=Path, required=True,
                        help="starting champion checkpoint (.pt), e.g. the BC "
                             "policy or a PPO run's output")
    parser.add_argument("--generations", type=int, default=5)
    parser.add_argument("--iters-per-gen", type=int, default=8)
    parser.add_argument("--games-per-iter", type=int, default=12,
                        help="-AIVS games per iter (each yields TWO rollouts)")
    parser.add_argument("--max-frames", type=int, default=100000)
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--port", type=int, default=6800)
    parser.add_argument("--seed", type=int, default=5000)
    parser.add_argument("--lr", type=float, default=1e-4,
                        help="lower than scratch PPO: fine-tuning a champion")
    parser.add_argument("--gamma", type=float, default=DEFAULT_DISCOUNT)
    parser.add_argument("--lam", type=float, default=0.95)
    parser.add_argument("--reward-scale", type=float, default=5.0)
    parser.add_argument("--terminal-weight", type=float, default=3.0)
    parser.add_argument("--ent-coef", type=float, default=0.02)
    parser.add_argument("--no-idle-gens", type=int, default=0,
                        help="curriculum: for generations 1..N the training "
                             "rollouts forbid no_op while another action is "
                             "legal (gates and later generations use the "
                             "game's own mask)")
    parser.add_argument("--value-warmup", type=int, default=2,
                        help="value-only iters at the start of generation 1")
    parser.add_argument("--gate-pairs", type=int, default=12,
                        help="mirrored seed pairs for the champion gate "
                             "(2 games per pair).  4 pairs promoted on a "
                             "1-win edge over 8 games — coin-flip territory; "
                             "12 pairs (24 games) separates skill from luck "
                             "and runs only once per generation")
    parser.add_argument("--gate-margin", type=int, default=1,
                        help="challenger must win at least this many MORE "
                             "games than the champion to be promoted")
    parser.add_argument("--out", type=Path,
                        default=Path("ppo_policy.selfplay.pt"))
    args = parser.parse_args(argv)

    torch.manual_seed(0)
    torch.set_num_threads(1)
    install = Path(args.install_dir)
    io_base = install / "spout"

    champion_path = args.out.with_suffix(".gen000.pt")
    net = load_net(args.init)
    save_net(net, champion_path)
    save_net(net, args.out)
    print(f"gen 0 champion <- {args.init} (saved {champion_path})", flush=True)

    net_lock = threading.Lock()

    class Locked:
        def act(self, feat, mask, deterministic=False, tmask=None, aux=None):
            with net_lock:
                return net.act(feat, mask, deterministic, tmask, aux)

    seed_counter = args.seed
    warmup_left = args.value_warmup
    accepted = 0
    record_state: dict = {}
    for gen in range(1, args.generations + 1):
        # Challenger starts from the champion each generation.
        net.load_state_dict(load_net(champion_path).state_dict())
        opt = torch.optim.Adam(net.parameters(), lr=args.lr)

        for it in range(args.iters_per_gen):
            seeds = [seed_counter + g for g in range(args.games_per_iter)]
            seed_counter += args.games_per_iter

            def run_game(index):
                try:
                    return rollout_versus(
                        Locked(), install, args.port + index, seeds[index],
                        args.max_frames, out_dir=io_base / f"w{index}",
                        net_offset=40 + index,
                        no_idle=gen <= args.no_idle_gens)
                except Exception as err:  # noqa: BLE001
                    print(f"  selfplay game seed={seeds[index]} failed "
                          f"({type(err).__name__}: {err}); dropped", flush=True)
                    return []

            workers = max(1, min(args.workers, args.games_per_iter))
            with ThreadPoolExecutor(max_workers=workers) as ex:
                games = list(ex.map(run_game, range(args.games_per_iter)))
            update_record_replays(install, io_base, games, seeds, gen,
                                  record_state)

            batches, units_all = [], []
            for rolls in games:
                # Final combat army value per owner of this game, so each
                # side's terminal relative term compares armies, not workers.
                army_by_owner = {}
                for roll in rolls:
                    if len(roll["action"]) and roll["feat"].shape[1] > 66:
                        army_by_owner[roll["owner"]] = _army_value(roll["feat"][-1])
                for roll in rolls:
                    if len(roll["action"]) == 0:
                        continue
                    opp = 3 - roll["owner"] if roll["owner"] in (1, 2) else None
                    shaped = augment_rewards(roll, args.reward_scale,
                                             args.terminal_weight,
                                             opp_army_value=army_by_owner.get(opp))
                    adv, ret = compute_gae(shaped, roll["value"], roll["done"],
                                           args.gamma, args.lam)
                    batches.append((roll, adv, ret))
                    units_all.append(_units(roll["result"], roll["owner"]))
            if not batches:
                print(f"gen {gen} iter {it}: no data", flush=True)
                continue
            merged = {k: np.concatenate([b[0][k] for b in batches])
                      for k in ("feat", "mask", "tmask", "aux", "action",
                                "target", "logp", "value")}
            adv = np.concatenate([b[1] for b in batches])
            ret = np.concatenate([b[2] for b in batches])
            value_only = warmup_left > 0
            if value_only:
                warmup_left -= 1
            stats = ppo_update(net, opt, merged, adv, ret,
                               ent_coef=args.ent_coef, value_only=value_only)
            tag = "warmup" if value_only else "train "
            print(f"gen {gen} iter {it:2d} [{tag}] | steps {len(adv):5d} | "
                  f"mean units {np.mean(units_all):.1f} | "
                  f"pol {stats['pol_loss']:+.3f} val {stats['val_loss']:.3f} "
                  f"ent {stats['entropy']:.3f}", flush=True)

        # League gate: challenger (A) vs champion (B), mirrored pairs.
        challenger_path = args.out.with_suffix(f".gen{gen:03d}.challenger.pt")
        save_net(net, challenger_path)
        pol_a = _load_policy(challenger_path, args.seed + gen, stochastic=True)
        pol_b = _load_policy(champion_path, args.seed + gen + 1,
                             stochastic=True)
        gate_base_seed = seed_counter
        summary = run_league(install, pol_a, pol_b, args.gate_pairs,
                             seed_counter, args.max_frames,
                             args.port + 100, workers=args.workers)
        seed_counter += args.gate_pairs
        promoted = summary["a_wins"] >= summary["b_wins"] + args.gate_margin
        print(f"gen {gen} GATE: challenger {summary['a_wins']}W "
              f"vs champion {summary['b_wins']}W ({summary['draws']}D) | "
              f"units {summary['a_mean_units']:.1f} vs "
              f"{summary['b_mean_units']:.1f} -> "
              f"{'PROMOTED' if promoted else 'rejected'}", flush=True)
        save_gate_replays(install, gen, promoted, gate_base_seed,
                          args.gate_pairs)
        if promoted:
            accepted += 1
            champion_path = args.out.with_suffix(f".gen{gen:03d}.pt")
            save_net(net, champion_path)
            save_net(net, args.out)

    print(f"done: {accepted}/{args.generations} generations promoted; "
          f"champion -> {args.out}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
