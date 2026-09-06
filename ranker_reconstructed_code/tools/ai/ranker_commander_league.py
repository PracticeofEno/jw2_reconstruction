"""Active curriculum and PFSP league controllers using isolated offline cohorts.

Nothing is promoted from training loss. Curriculum and champion transitions
require complete, separately collected evaluation manifests.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import random

import numpy as np
import torch

from ranker_commander_eval import (curriculum_promotion, curriculum_settings,
    discover_seed_mapping, evaluation_jobs, run_games, summarize_evaluation)
from ranker_commander_model import CommanderPolicy, load_weights
from ranker_commander_rollout import SCHEMA_CRC, WIN
from ranker_commander_train import (TrainConfig, build_batch, load_cohort, load_optimizer,
    close_episode, discard_accepted, ppo_admission, save_checkpoint, train_update)


def write_state(path, state):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps({**state, "schema_crc": SCHEMA_CRC}, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def curriculum_jobs(mapping, stage, *, sampling):
    if stage not in range(4):
        raise ValueError("curriculum evaluation stage must be C0..C3")
    settings = curriculum_settings(stage)
    if not sampling:
        jobs = evaluation_jobs(mapping)
        return [{**job, **settings} for job in jobs if stage != 0 or job["tribe"] == 2]
    if stage == 0:
        return [{"seed": seed, "tribe": 2, **settings} for seed in range(100, 148)]
    if stage == 1:
        return [{"seed": seed, "tribe": tribe, **settings} for tribe in range(4) for seed in range(100, 112)]
    return evaluation_jobs(mapping, sampling=True)


def collect_evaluation(install, weights, directory, jobs, *, workers=8,
                       deterministic=False, executable=None, no_sleep=True, retries=2,
                       teacher=False):
    jobs = [{**job, "case_index": index} for index, job in enumerate(jobs)]
    resolved = {}
    pending = jobs
    for attempt in range(retries + 1):
        if not pending:
            break
        reports = run_games(install, weights, Path(directory) / f"attempt_{attempt}", pending,
            workers=workers, deterministic=deterministic, executable=executable, no_sleep=no_sleep,
            teacher=teacher)
        for report in reports:
            resolved[report["case_index"]] = report
        pending = [jobs[index] for index, report in sorted(resolved.items())
                   if not report.get("valid") or not report.get("evaluation_valid")]
    result = [resolved[index] for index in range(len(jobs))]
    write_state(Path(directory) / "evaluation.json", {"reports": result, "attempts": attempt + 1})
    return result


def measured_seed_catalog(reports):
    catalog = defaultdict(set)
    for report in reports:
        if report.get("valid") and report.get("tribe") == 2 and "start_pair" in report:
            catalog[tuple(report["start_pair"])].add(int(report["seed"]))
    return {pair: sorted(seeds) for pair, seeds in catalog.items()}


def champion_challenge_jobs(catalog, challenger, champion):
    """100 distinct seeded games, 50 with challenger on each policy owner.

    Choose four seeds per ordered pair, plus two mutually reversed extra
    pairs. Repeated deterministic PRNG seeds cannot inflate a win count.
    """
    pairs = sorted((a, b) for a in range(4) for b in range(4) if a != b)
    extras = {(0, 1), (1, 0)}
    missing = {pair: 4 + int(pair in extras) - len(catalog.get(pair, [])) for pair in pairs
               if len(catalog.get(pair, [])) < 4 + int(pair in extras)}
    if missing:
        raise ValueError(f"champion challenge needs more measured distinct seeds per start pair: {missing}; increase discovery --max-seed")
    selected = [(pair, seed) for pair in pairs for seed in catalog[pair][:4 + int(pair in extras)]]
    jobs = []
    for owner in (1, 2):
        for pair, seed in selected:
            jobs.append({"seed": seed, "tribe": 2, "start_pair": list(pair), "evaluate_owner": owner,
                         "primary_weights": str(challenger if owner == 1 else champion),
                         "opponent_weights": str(champion if owner == 1 else challenger),
                         **curriculum_settings(4)})
    return jobs


def champion_gate(challenge_reports, builtin_reports):
    valid = [row for row in challenge_reports if row.get("valid") and row.get("evaluation_valid")]
    owners = Counter(row.get("evaluate_owner") for row in valid)
    identities = {(row.get("evaluate_owner"), row.get("seed")) for row in valid}
    pairs = Counter(tuple(row.get("start_pair", ())) for row in valid)
    owner_pairs = Counter((row.get("evaluate_owner"), tuple(row.get("start_pair", ()))) for row in valid)
    coverage = (owners == {1: 50, 2: 50} and len(identities) == 100 and len(pairs) == 12
                and all(8 <= count <= 10 for count in pairs.values()) and len(owner_pairs) == 24
                and all(4 <= count <= 5 for count in owner_pairs.values())
                and not any(row.get("deterministic") for row in valid))
    wins = sum((row.get("status") if row.get("evaluate_owner") == 1 else row.get("status2")) == WIN
               for row in valid)
    builtin = summarize_evaluation(builtin_reports, expected_games=48)
    passed = len(valid) == 100 and coverage and wins >= 55 and builtin["passed_100_percent"]
    return {"promote": passed, "games": len(valid), "wins": wins, "required_wins": 55,
            "coverage_complete": coverage, "builtin": builtin}


def league_jobs(pool, scores, *, seed, start_game, games, primary, champion_only=None):
    """Cumulative quota gives exactly 20% built-in games over every five jobs."""
    rng = random.Random(seed + start_game)
    jobs = []
    names = sorted(pool)
    for index in range(games):
        global_game = start_game + index
        job = {"seed": seed + global_game, "tribe": 2, **curriculum_settings(4)}
        if champion_only is not None:
            job.update(opponent_weights=str(champion_only), opponent_key="champion")
        elif (global_game + 1) // 5 != global_game // 5:
            job.update(tribe=(global_game // 5) % 4, opponent_key="builtin")
        elif names:
            weights = []
            for name in names:
                score = scores.get(name, {"games": 0, "wins": 0})
                probability = score["wins"] / score["games"] if score["games"] else 0.5
                weights.append(max(1e-6, (1.0 - probability) ** 2))
            choice = rng.choices(names, weights=weights, k=1)[0]
            job.update(opponent_weights=pool[choice], opponent_key=choice)
        else:
            raise ValueError("PFSP league requires a nonempty checkpoint pool")
        jobs.append(job)
    return jobs


def _update(policy, optimizer, reports, output, iteration, shaping_scale, teacher=None,
             *, keep_rollouts=False):
    paths = [row["rollout"] for row in reports if row.get("valid")]
    episodes, rejected = load_cohort(paths, version=policy.weight_version)
    if len(episodes) * 12 < len(reports) * 10:
        raise RuntimeError(f"cohort has fewer than 10/12 valid episodes: {rejected}")
    config = TrainConfig(iteration=iteration, shaping_scale=shaping_scale)
    batch, reward_metrics = build_batch(episodes, config)
    optimizer, optimizer_metrics = train_update(policy, batch, config, optimizer=optimizer,
                                                teacher_policy=teacher)
    metadata = {"mode": "ppo", "iteration": iteration, "rejected": rejected,
                **reward_metrics, **optimizer_metrics}
    save_checkpoint(policy, output, version=policy.weight_version + 1, metadata=metadata, optimizer=optimizer)
    if keep_rollouts:
        for episode in episodes:
            close_episode(episode)
    else:
        discard_accepted(episodes)
        for report in reports:
            if report.get("valid") and report.get("rollout2"):
                Path(report["rollout2"]).unlink(missing_ok=True)
    print(json.dumps({"checkpoint": str(output), "weight_version": policy.weight_version, **metadata}), flush=True)
    return optimizer, config.shaping_scale


def run_curriculum(args, state, mapping):
    from ranker_commander_train import main as train_main
    if state.get("stage", 0) >= 4:
        return state
    for _ in range(args.max_updates):
        stage = state["stage"]
        iteration = state["updates"]
        current = Path(state["policy"])
        command = ["ppo", "--install-dir", str(args.install_dir), "--policy", str(current),
                   "--out", str(args.work_dir / "current.bin"), "--io", str(args.work_dir / "training"),
                   "--iterations", "1", "--iteration", str(iteration), "--workers", str(args.workers),
                   "--games-per-cohort", str(args.games), "--curriculum", str(stage), "--seed", str(args.seed),
                   # Design 5.5 mixes rule-commander variants from C1 (40%) and
                   # C2 (20%). At C1 the policy still loses ~80% of variant games,
                   # so the mix is held at 25% there (with 16-game cohorts: 12
                   # built-in games as designed plus 4 variant games) and raised
                   # to the designed share from C2.
                   "--variant-opponents", str(0.25 if stage == 1 else 0.2 if stage >= 2 else 0.0)]
        if args.exe:
            command.extend(["--exe", str(args.exe)])
        if args.keep_sleep:
            command.append("--keep-sleep")
        if args.teacher_policy:
            command.extend(["--teacher-policy", str(args.teacher_policy)])
        if args.no_bc_control:
            command.append("--no-bc-control")
        if args.bc_warm_start:
            command.append("--bc-warm-start")
        if args.keep_rollouts:
            command.append("--keep-rollouts")
        command.extend(["--teacher-kl-initial", str(args.teacher_kl_initial),
                        "--teacher-kl-floor", str(args.teacher_kl_floor),
                        "--teacher-kl-decay", str(args.teacher_kl_decay),
                        "--critic-warmup", str(args.critic_warmup)])
        train_main(command)
        state.update(policy=str((args.work_dir / "current.bin").resolve()), updates=iteration + 1)
        write_state(args.state, state)
        if state["updates"] % args.evaluate_every:
            continue
        directory = args.work_dir / "evaluation" / f"update_{state['updates']:05d}_C{stage}"
        argmax = collect_evaluation(args.install_dir, state["policy"], directory / "argmax",
            curriculum_jobs(mapping, stage, sampling=False), workers=args.workers,
            deterministic=True, executable=args.exe, no_sleep=not args.keep_sleep)
        sampled = collect_evaluation(args.install_dir, state["policy"], directory / "sample",
            curriculum_jobs(mapping, stage, sampling=True), workers=args.workers,
            executable=args.exe, no_sleep=not args.keep_sleep)
        gate = curriculum_promotion(stage, argmax, sampled)
        state["last_gate"] = gate
        if gate["promote"]:
            if stage == 3:
                # Repeat the full suite with the same frozen policy before any update.
                repeated_argmax = collect_evaluation(args.install_dir, state["policy"], directory / "repeat_argmax",
                    curriculum_jobs(mapping, stage, sampling=False), workers=args.workers,
                    deterministic=True, executable=args.exe, no_sleep=not args.keep_sleep)
                repeated_sampled = collect_evaluation(args.install_dir, state["policy"], directory / "repeat_sample",
                    curriculum_jobs(mapping, stage, sampling=True), workers=args.workers,
                    executable=args.exe, no_sleep=not args.keep_sleep)
                repeated = curriculum_promotion(stage, repeated_argmax, repeated_sampled)
                gate["repeat_gate"] = repeated
                gate["promote"] = repeated["promote"]
                state["c3_confirmations"] = 2 if repeated["promote"] else 0
                if repeated["promote"]:
                    state["stage"] = 4
                    state["curriculum_complete"] = True
            else:
                state["stage"] += 1
        else:
            state["c3_confirmations"] = 0
        write_state(directory / "gate.json", gate)
        write_state(args.state, state)
        print(json.dumps({"curriculum_gate": gate, "next_stage": state["stage"]}), flush=True)
        if state["stage"] >= 4:
            break
    return state


def train_exploiter(args, state, directory):
    update = state.get("league_updates", 0)
    reset = "exploiter" not in state or update % args.exploiter_reset == 0
    policy = load_weights(state["champion"] if reset else state["exploiter"])
    optimizer = torch.optim.Adam(policy.parameters(), lr=3e-4)
    if not reset:
        optimizer_path = Path(state["exploiter"]).with_suffix(".bin.optimizer.npz")
        if optimizer_path.exists():
            load_optimizer(optimizer, optimizer_path, version=policy.weight_version)
    snapshot = directory / "exploiter_start.bin"
    save_checkpoint(policy, snapshot, version=policy.weight_version, metadata={"reset": reset})
    games_seen = state.get("exploiter_games", 0)
    jobs = league_jobs({}, {}, seed=args.seed + 1000000, start_game=games_seen,
                       games=args.games, primary=snapshot, champion_only=state["champion"])
    reports = run_games(args.install_dir, snapshot, directory / "exploiter_games", jobs,
                         workers=args.workers, executable=args.exe, no_sleep=not args.keep_sleep)
    output = args.work_dir / "exploiter.bin"
    iterations = 0 if reset else state.get("exploiter_updates", 0)
    _update(policy, optimizer, reports, output, iterations, 0.0, keep_rollouts=args.keep_rollouts)
    state.update(exploiter=str(output.resolve()), exploiter_games=games_seen + len(jobs),
                 exploiter_updates=iterations + 1)
    # Freeze each sampled exploiter version; future cohorts never see a mid-game replacement.
    name = f"exploiter_{update:05d}"
    frozen = args.work_dir / "pool" / f"{name}.bin"
    if frozen.exists():
        raise FileExistsError(frozen)
    save_checkpoint(policy, frozen, version=policy.weight_version, metadata={"exploiter": True})
    state["pool"][name] = str(frozen.resolve())


def run_league(args, state, mapping, catalog):
    policy = load_weights(state["policy"])
    if "champion" not in state:
        champion = args.work_dir / "pool" / "champion_000.bin"
        save_checkpoint(policy, champion, version=policy.weight_version, metadata={"league_champion": 0})
        state.update(champion=str(champion.resolve()), pool={"champion_000": str(champion.resolve())},
                     scores={}, league_games=0, replacements=0, league_updates=0)
        for candidate in sorted((Path(state["policy"]).parent / "pool").glob("commander_*.bin")):
            load_weights(candidate)  # Import only compatible, checksummed historical policies.
            state["pool"][candidate.stem] = str(candidate.resolve())
    # Validate challenge coverage before spending time on training.
    champion_challenge_jobs(catalog, state["policy"], state["champion"])
    optimizer = torch.optim.Adam(policy.parameters(), lr=3e-4)
    policy_path = Path(state["policy"])
    optimizer_path = policy_path.with_suffix(policy_path.suffix + ".optimizer.npz")
    if optimizer_path.exists():
        load_optimizer(optimizer, optimizer_path, version=policy.weight_version)
    shaping_scale = float(state.get("shaping_scale", 1.0))
    for _ in range(args.max_updates):
        update = state.get("league_updates", 0)
        directory = args.work_dir / "league" / f"update_{update:05d}"
        snapshot = directory / "weights.bin"
        save_checkpoint(policy, snapshot, version=policy.weight_version, metadata={"league_update": update})
        opponents = {**state["pool"], "main_self": str(snapshot.resolve())}
        jobs = league_jobs(opponents, state["scores"], seed=args.seed,
                           start_game=state["league_games"], games=args.games, primary=snapshot)
        reports = run_games(args.install_dir, snapshot, directory / "games", jobs,
                            workers=args.workers, executable=args.exe, no_sleep=not args.keep_sleep)
        for report in reports:
            opponent = report["opponent_key"]
            if report.get("valid") and opponent != "builtin":
                score = state["scores"].setdefault(opponent, {"games": 0, "wins": 0})
                score["games"] += 1
                score["wins"] += int(report["win"])
        current = args.work_dir / "current.bin"
        optimizer, shaping_scale = _update(policy, optimizer, reports, current,
            state["updates"], shaping_scale, keep_rollouts=args.keep_rollouts)
        state.update(policy=str(current.resolve()), updates=state["updates"] + 1,
                     league_updates=update + 1, league_games=state["league_games"] + len(jobs),
                     shaping_scale=shaping_scale)
        if (update + 1) % 20 == 0:
            name = f"historical_{policy.weight_version:05d}"
            frozen = args.work_dir / "pool" / f"{name}.bin"
            if frozen.exists():
                raise FileExistsError(frozen)
            save_checkpoint(policy, frozen, version=policy.weight_version, metadata={"frozen": True})
            state["pool"][name] = str(frozen.resolve())
        if args.exploiter_every and (update + 1) % args.exploiter_every == 0:
            train_exploiter(args, state, directory)
        write_state(args.state, state)
        if (update + 1) % args.evaluate_every:
            continue
        challenge = collect_evaluation(args.install_dir, current, directory / "challenge",
            champion_challenge_jobs(catalog, current.resolve(), state["champion"]),
            workers=args.workers, executable=args.exe, no_sleep=not args.keep_sleep)
        builtin = collect_evaluation(args.install_dir, current, directory / "builtin_gate",
            evaluation_jobs(mapping), workers=args.workers, deterministic=True,
            executable=args.exe, no_sleep=not args.keep_sleep)
        gate = champion_gate(challenge, builtin)
        state["last_champion_gate"] = gate
        if gate["promote"]:
            state["replacements"] += 1
            name = f"champion_{state['replacements']:03d}"
            champion = args.work_dir / "pool" / f"{name}.bin"
            if champion.exists():
                raise FileExistsError(champion)
            save_checkpoint(policy, champion, version=policy.weight_version, metadata=gate)
            state["champion"] = str(champion.resolve())
            state["pool"][name] = str(champion.resolve())
        write_state(directory / "champion_gate.json", gate)
        write_state(args.state, state)
        print(json.dumps({"champion_gate": gate, "replacements": state["replacements"]}), flush=True)
        if state["replacements"] >= args.target_replacements:
            state["league_complete"] = True
            write_state(args.state, state)
            break
    return state


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("curriculum", "league"))
    parser.add_argument("--install-dir", type=Path, required=True)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--policy", type=Path, help="initial commander checkpoint; persisted state takes precedence on resume")
    parser.add_argument("--teacher-policy", type=Path)
    parser.add_argument("--no-bc-control", action="store_true", help="explicit unassessed no-BC baseline exemption")
    parser.add_argument("--bc-warm-start", action="store_true", help="explicit PPO warm start from an assessed BC checkpoint that missed the gameplay gate")
    parser.add_argument("--keep-rollouts", action="store_true", help="retain accepted training RLOs; evaluation artifacts always remain")
    parser.add_argument("--mapping", type=Path, required=True, help="measured discovery manifest")
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--games", type=int, default=12)
    parser.add_argument("--seed", type=int, default=1000)
    parser.add_argument("--max-updates", type=int, default=300)
    parser.add_argument("--evaluate-every", type=int, default=20)
    parser.add_argument("--target-replacements", type=int, default=10)
    parser.add_argument("--exploiter-every", type=int, default=5, help="train a champion-only exploiter every N main updates; 0 disables")
    parser.add_argument("--exploiter-reset", type=int, default=20, help="reset exploiter from champion every N main updates")
    parser.add_argument("--keep-sleep", action="store_true", help="keep the engine's per-frame Sleep(1); default passes -AINOSLEEP")
    parser.add_argument("--teacher-kl-initial", type=float, default=0.05, help="PPO KL(BC reference || policy) coefficient at update 0")
    parser.add_argument("--teacher-kl-floor", type=float, default=0.0, help="PPO KL coefficient held after the decay")
    parser.add_argument("--teacher-kl-decay", type=int, default=30, help="updates over which the PPO KL coefficient decays")
    parser.add_argument("--critic-warmup", type=int, default=0, help="first N PPO updates train only the critic")
    args = parser.parse_args(argv)
    if min(args.workers, args.games, args.seed, args.max_updates, args.evaluate_every, args.target_replacements) < 1:
        parser.error("counts and seeds must be positive")
    if args.exploiter_every < 0 or args.exploiter_reset < 1:
        parser.error("exploiter frequency must be nonnegative and reset interval positive")
    args.work_dir = args.work_dir.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.mapping.read_text(encoding="utf-8"))
    mapping = discover_seed_mapping(manifest)
    evaluation_jobs(mapping)  # Full deterministic coverage is mandatory up front.
    if args.state.exists():
        state = json.loads(args.state.read_text(encoding="utf-8"))
        if state.get("schema_crc") != SCHEMA_CRC:
            parser.error("controller state schema mismatch")
    else:
        if args.policy is None:
            parser.error("a new controller requires --policy")
        metadata_path = args.policy.with_suffix(args.policy.suffix + ".json")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {}
        updates = int(metadata.get("iteration", -1)) + 1 if metadata.get("mode") == "ppo" else 0
        state = {"policy": str(args.policy.resolve()), "stage": 0, "updates": updates,
                 "c3_confirmations": 0, "curriculum_complete": False}
    if args.mode == "league" and not state.get("curriculum_complete"):
        parser.error("league entry requires this controller's completed C3 double evaluation gate")
    if args.mode == "curriculum":
        policy_path = Path(state["policy"])
        metadata_path = policy_path.with_suffix(policy_path.suffix + ".json")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {}
        policy = load_weights(policy_path)
        ppo_admission(metadata, version=policy.weight_version, no_bc_control=args.no_bc_control,
                      warm_start=args.bc_warm_start,
                      weights_sha256=hashlib.sha256(policy_path.read_bytes()).hexdigest())
    torch.set_num_threads(8)
    torch.manual_seed(args.seed)
    if args.mode == "curriculum":
        run_curriculum(args, state, mapping)
    else:
        run_league(args, state, mapping, measured_seed_catalog(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
