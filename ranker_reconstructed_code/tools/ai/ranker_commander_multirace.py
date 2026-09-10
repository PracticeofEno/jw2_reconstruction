"""Resumable, separate-race PPO followed by a four-race historical self-play pool.

Examples (from the workspace root):
  .venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py init
  .venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py train --cycles 24
  .venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py status
  .venv-ai/Scripts/python.exe ranker_reconstructed_code/tools/ai/ranker_commander_multirace.py stop

Use the repository tools/ai path when invoking from the workspace. A stop
request finishes the current cohort/checkpoint. Resuming removes that request.
No policy is automatically deployed or certified as a champion.
"""
from __future__ import annotations

import argparse
from collections import Counter
from contextlib import contextmanager
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import random
import uuid

import numpy as np
import torch

from ranker_commander_eval import run_games
from ranker_commander_model import SCHEMA_CRC, RACE_SCHEMA_CRC, VECTOR_SIZE, load_weights, upgrade_policy
from ranker_commander_upgrade import migrate
from ranker_commander_train import (TrainConfig, build_batch, close_episode, discard_accepted,
                                    load_cohort, load_optimizer, save_checkpoint, train_update)
from ranker_commander_rollout import WIN

WORKSPACE = Path(__file__).resolve().parents[3]
RACES = {0: "primitive", 1: "elf", 2: "tyrano", 3: "demon"}
ORDER = (1, 3, 0, 2)
DEFAULT_WORK = WORKSPACE / "debug_artifacts" / "commander" / "skills_20260908" / "training_run"


def training_sources():
    directory = Path(__file__).parent
    names = ("ranker_commander_multirace.py", "ranker_commander_model.py", "ranker_commander_train.py",
             "ranker_commander_rollout.py", "ranker_commander_eval.py", "ranker_commander_upgrade.py",
             "ranker_commander_recover.py", "ranker_commander_watch.py")
    return {name: sha(directory / name) for name in names}


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def emit(value):
    print(json.dumps(value, ensure_ascii=False), flush=True)


@contextmanager
def runner_lock(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "runner.lock").open("a+b") as stream:
        stream.seek(0)
        if not stream.read(1):
            stream.write(b"0")
            stream.flush()
        stream.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == "nt":
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)


def verify_policy(row, tribe):
    path = Path(row["policy"])
    metadata = json.loads(Path(str(path) + ".json").read_text(encoding="utf-8"))
    if (metadata.get("own_tribe") != tribe or metadata.get("schema_crc") != SCHEMA_CRC or
            metadata.get("weight_version") != row["version"] or
            row["sha256"] != sha(path) or metadata.get("weights_sha256") != row["sha256"]):
        raise ValueError(f"race/checkpoint mismatch: {RACES[tribe]}")
    return metadata


def migrate_race_campaign(state, directory):
    """Fork 642-input policies and Adam slots without editing any parent file."""
    if state["schema_crc"] != RACE_SCHEMA_CRC:
        raise ValueError("unsupported parent skill migration schema")
    for tribe in ORDER:
        row = state["races"][str(tribe)]
        converted = {}
        entries = row["pool"] + [row] + ([row["best_evaluated"]] if "best_evaluated" in row else [])
        for entry in entries:
            source = Path(entry["policy"])
            metadata = json.loads(Path(str(source) + ".json").read_text(encoding="utf-8"))
            if (metadata.get("own_tribe") != tribe or metadata.get("schema_crc") != RACE_SCHEMA_CRC or
                    sha(source) != entry["sha256"] or metadata.get("weights_sha256") != entry["sha256"] or
                    metadata.get("weight_version") != entry["version"]):
                raise ValueError("parent race history does not match its checkpoint")
            if str(source) not in converted:
                target = directory / RACES[tribe] / f"migrated_{len(converted):03d}_v{entry['version']}.bin"
                optimizer = Path(str(source) + ".optimizer.npz")
                record = migrate(source, target, source_optimizer=optimizer if optimizer.exists() else None)
                record.update(own_tribe=tribe, mode="skill_upgrade", parent_updates=row["updates"])
                write_json(str(target) + ".json", record)
                converted[str(source)] = dict(policy=str(target.resolve()), sha256=sha(target), version=record["weight_version"])
            entry.update(converted[str(source)])
        row.update(pre_skill_updates=row["updates"], ready=False)
        row.pop("evaluation", None)
        row.pop("best_evaluated", None)  # prior evaluations did not exercise skills
    state.update(schema_crc=SCHEMA_CRC, phase="bootstrap", scores={})


def initialize(args):
    path = args.work_dir / "state.json"
    if path.exists():
        raise FileExistsError("state exists; train resumes it, init never replaces it")
    if args.resume_from is not None:
        parent = args.resume_from.resolve()
        # An explicit fork preserves completed learning and optimizer state,
        # but records a new runtime identity. Never edit the parent campaign.
        with runner_lock(parent.parent):
            raw = parent.read_bytes()
            state = json.loads(raw)
            if state["schema_crc"] != SCHEMA_CRC:
                migrate_race_campaign(state, args.work_dir)
            for tribe in range(4):
                row = state["races"][str(tribe)]
                verify_policy(row, tribe)
                if load_weights(row["policy"]).weight_version != row["version"]:
                    raise ValueError("parent policy version differs from the campaign state")
            args.work_dir.mkdir(parents=True, exist_ok=True)
            (args.work_dir / "parent_state.json").write_bytes(raw)
        state.update(parent_state=str(parent), parent_state_sha256=hashlib.sha256(raw).hexdigest(),
            executable=str(args.exe.resolve()), executable_sha256=sha(args.exe),
            install_dir=str(args.install_dir.resolve()), training_sources=training_sources(),
            catalog_sha256=sha(Path(__file__).with_name("commander_races.json")),
            skills_catalog_sha256=sha(Path(__file__).with_name("commander_skills.json")),
            status="initialized", active=None, pid=None)
        state.pop("error", None)
        write_json(path, state)
        emit(dict(initialized=str(path), resumed_from=str(parent), parent_unchanged=sha(parent) == state["parent_state_sha256"]))
        return
    source = args.source.resolve()
    source_hash = sha(source)
    original = load_weights(source, allow_legacy=True)
    upgraded = upgrade_policy(original) if original.vector_size != VECTOR_SIZE else original
    state = dict(schema_crc=SCHEMA_CRC, phase="bootstrap", cycles=0, next_race=0,
                 next_seed=args.seed, seed=args.seed, status="initialized", races={}, scores={},
                 executable=str(args.exe.resolve()), executable_sha256=sha(args.exe),
                 install_dir=str(args.install_dir.resolve()), source=str(source), source_sha256=source_hash,
                 catalog_sha256=sha(Path(__file__).with_name("commander_races.json")),
                 skills_catalog_sha256=sha(Path(__file__).with_name("commander_skills.json")),
                 training_sources=training_sources(),
                 config=dict(games=args.games, workers=args.workers, threads=args.threads,
                             min_updates=args.min_updates, evaluate_every=args.evaluate_every,
                             evaluation_games=args.evaluation_games, learning_rate=args.learning_rate,
                             gamma=1.0, gae_lambda=0.98, epochs=3, minibatch=2048))
    for tribe in ORDER:
        directory = args.work_dir / RACES[tribe]
        checkpoint = directory / "initial.bin"
        if checkpoint.exists():
            raise FileExistsError(f"initial checkpoint exists: {checkpoint}")
        save_checkpoint(upgraded, checkpoint, version=0, metadata=dict(
            mode="race_transfer", own_tribe=tribe, source=str(source), source_sha256=source_hash,
            source_weight_version=original.weight_version, optimizer_reset=True,
            admission="independent race transfer experiment; no inherited BC/gameplay approval"))
        entry = dict(policy=str(checkpoint.resolve()), sha256=sha(checkpoint), version=0,
                     updates=0, games=0, ready=False, pool=[])
        entry["pool"].append(dict(policy=entry["policy"], sha256=entry["sha256"], version=0))
        state["races"][str(tribe)] = entry
    if sha(source) != source_hash:
        raise RuntimeError("source weights changed during initialization")
    write_json(path, state)
    emit(dict(initialized=str(path), source_unchanged=True, races=list(RACES.values())))


def select_opponent(state, tribe, game, rng):
    # Race coverage is explicit; PFSP chooses the historical version within it.
    opponent_tribe = ((game // 5) * 4 + game % 5) % 4
    entries = state["races"][str(opponent_tribe)]["pool"]
    weights = []
    for entry in entries:
        key = f"{tribe}:{opponent_tribe}:{entry['sha256']}"
        score = state["scores"].get(key, dict(wins=0, games=0))
        probability = (score["wins"] + 1) / (score["games"] + 2)
        weights.append(max(0.01, (1 - probability) ** 2))
    entry = rng.choices(entries, weights=weights)[0]
    return opponent_tribe, entry


def training_jobs(state, tribe, count):
    row = state["races"][str(tribe)]
    rng = random.Random(state["next_seed"])
    jobs = []
    for index in range(count):
        game = row["games"] + index
        seed = state["next_seed"] + index
        job = dict(seed=seed, policy_seed=state["seed"] * 100000 + seed,
                   tribe=game % 4, own_tribe=tribe, train_owner=1,
                   curriculum=2, max_frames=60000, coordinated_transfers=False,
                   opponent_key="builtin", opp_slow=0)
        if state.get("config", {}).get("builtin_only"):
            pass  # Full-speed builtin opponents only, including after readiness.
        elif state["phase"] == "bootstrap":
            if row["updates"] < 2 and tribe != 2:
                job.update(curriculum=1, max_frames=40000, opp_slow=2)
        elif game % 5 != 4:
            other, entry = select_opponent(state, tribe, game, rng)
            if sha(entry["policy"]) != entry["sha256"]:
                raise ValueError("historical opponent checkpoint changed")
            job.update(opponent_policy_tribe=other, opponent_weights=entry["policy"],
                       opponent_key=f"{tribe}:{other}:{entry['sha256']}", curriculum=4)
            if game % 2:
                job.update(train_owner=2, own_tribe=other, opponent_policy_tribe=tribe,
                           primary_weights=entry["policy"], opponent_weights=row["policy"])
        jobs.append(job)
    return jobs


def learning_path(report):
    return report["rollout2"] if report.get("train_owner", 1) == 2 else report["rollout"]


def learning_status(report):
    return report["status2"] if report.get("train_owner", 1) == 2 else report["status"]


def learning_metrics(report):
    return report.get("commander_metrics2", {}) if report.get("train_owner", 1) == 2 else report.get("commander_metrics", {})


def trim_pool(row):
    entries = row["pool"]
    keep = {entries[0]["sha256"]}
    if row.get("best_evaluated"):
        keep.add(row["best_evaluated"]["sha256"])
        if row["best_evaluated"]["sha256"] not in {entry["sha256"] for entry in entries}:
            entries = [entries[0], row["best_evaluated"], *entries[1:]]
    for entry in reversed(entries):
        if len(keep) >= 8:
            break
        keep.add(entry["sha256"])
    row["pool"] = [entry for entry in entries if entry["sha256"] in keep]


def bootstrap_gate(reports, *, updates, minimum_updates, expected_games):
    valid = [r for r in reports if r.get("valid") and r.get("evaluation_valid")]
    coverage = Counter((r["tribe"], r["deterministic"]) for r in valid)
    complete = len(valid) == expected_games and all(coverage[(tribe, mode)] >= expected_games // 8
        for tribe in range(4) for mode in (False, True))
    argmax = [r for r in valid if r["deterministic"]]
    sampled = [r for r in valid if not r["deterministic"]]
    rate = lambda rows: sum(r["status"] == WIN for r in rows) / max(1, len(rows))
    violations = sum(r.get("commander_metrics", {}).get("mask_violations", 0) for r in valid)
    per_tribe = {str(t): rate([r for r in valid if r["tribe"] == t]) for t in range(4)}
    ready = (updates >= minimum_updates and complete and not violations and
             rate(argmax) >= 0.60 and rate(sampled) >= 0.50 and min(per_tribe.values()) >= 0.25)
    return dict(ready=ready, purpose="bootstrap readiness only; not champion/deployment approval",
                games=len(valid), coverage_complete=complete, argmax_win_rate=rate(argmax),
                sampling_win_rate=rate(sampled), per_opponent=per_tribe, mask_violations=violations)


def collect(state, weights, directory, jobs, *, deterministic=False):
    config = state["config"]
    reports = run_games(state["install_dir"], weights, directory, jobs,
                        workers=config["workers"], executable=state["executable"],
                        deterministic=deterministic)
    failed = [i for i, row in enumerate(reports) if not row.get("valid")]
    if failed:
        again = run_games(state["install_dir"], weights, directory.with_name(directory.name + "_retry"),
                          [jobs[i] for i in failed], workers=config["workers"],
                          executable=state["executable"], deterministic=deterministic)
        for index, report in zip(failed, again):
            reports[index] = report
    write_json(directory / "accepted_reports.json", reports)
    if not all(r.get("valid") for r in reports):
        raise RuntimeError("cohort incomplete: " + "; ".join(r["reason"] for r in reports if not r.get("valid")))
    return reports


def evaluate(state, tribe, directory):
    count = state["config"]["evaluation_games"] // 2
    row = state["races"][str(tribe)]
    reports = []
    for deterministic in (True, False):
        # Disjoint from the monotonically allocated training seeds. Repeated
        # development evaluation is documented, not a generalization claim.
        jobs = [dict(seed=state["config"].get("evaluation_seed", 3000000000) + tribe * 10000 + i + 1, own_tribe=tribe,
                     tribe=i % 4, curriculum=2, max_frames=60000,
                     policy_seed=7000000000 + tribe * 10000 + i,
                     coordinated_transfers=False) for i in range(count)]
        reports.extend(collect(state, row["policy"], directory / ("argmax" if deterministic else "sampling"),
                               jobs, deterministic=deterministic))
    gate = bootstrap_gate(reports, updates=row["updates"] - row.get("pre_skill_updates", 0), minimum_updates=state["config"]["min_updates"],
                          expected_games=state["config"]["evaluation_games"])
    write_json(directory / "evaluation.json", dict(gate=gate, reports=reports, weights_sha256=row["sha256"]))
    row.update(ready=gate["ready"], evaluation=dict(path=str(directory / "evaluation.json"), **gate))
    score = (gate["argmax_win_rate"] + gate["sampling_win_rate"]) / 2
    if gate["coverage_complete"] and not gate["mask_violations"] and score > row.get("best_evaluated", {}).get("score", -1):
        row["best_evaluated"] = dict(policy=row["policy"], sha256=row["sha256"], version=row["version"],
                                     score=score, evaluation=str(directory / "evaluation.json"))
        trim_pool(row)
    emit(dict(evaluation=RACES[tribe], **gate))


def train_race(args, state, tribe):
    row, config = state["races"][str(tribe)], state["config"]
    verify_policy(row, tribe)
    policy = load_weights(row["policy"])
    optimizer_path = Path(row["policy"] + ".optimizer.npz")
    optimizer = load_optimizer(torch.optim.Adam(policy.parameters()), optimizer_path, version=row["version"]) if optimizer_path.exists() else None
    jobs = training_jobs(state, tribe, config["games"])
    run_id = f"update_{row['updates'] + 1:05d}_{uuid.uuid4().hex[:8]}"
    directory = args.work_dir / RACES[tribe] / run_id
    state["next_seed"] += len(jobs)
    state.update(status="collecting", active=dict(tribe=tribe, run=run_id, phase=state["phase"], jobs=jobs))
    write_json(args.work_dir / "state.json", state)
    emit(dict(collection=RACES[tribe], phase=state["phase"], update=row["updates"] + 1, games=len(jobs)))
    reports = collect(state, row["policy"], directory / "games", jobs)
    for report in reports:
        digest_key = "weights_sha2562" if report.get("train_owner", 1) == 2 else "weights_sha256"
        if report.get(digest_key) != row["sha256"] or not learning_metrics(report):
            raise RuntimeError("learning owner's checkpoint or metrics were not confirmed")
    if any(learning_metrics(r).get("mask_violations", 0) for r in reports):
        raise RuntimeError("masked action in learning owner; cohort rejected")
    paths = [learning_path(r) for r in reports]
    episodes, rejected = load_cohort(paths, version=row["version"])
    if len(episodes) != len(reports) or any(e.weight_version != row["version"] for e in episodes):
        raise RuntimeError(f"mixed/incomplete on-policy cohort: {rejected}")
    for episode in episodes:
        if not np.all(episode.records["vector"][:, 606:610] == np.eye(4)[tribe]):
            raise RuntimeError("training cohort contains another race")
    teacher_policy = None
    if config.get("teacher_kl", 0):
        if sha(row["anchor_policy"]) != row["anchor_sha256"]:
            raise ValueError("recovery reference policy changed")
        teacher_policy = load_weights(row["anchor_policy"])
    training_config = TrainConfig(iteration=row.get("recovery_updates", row["updates"]), epochs=config["epochs"], minibatch=config["minibatch"],
        learning_rate_initial=config["learning_rate"], learning_rate_final=config["learning_rate"],
        gamma=config["gamma"], gae_lambda=config["gae_lambda"],
        teacher_kl_initial=config.get("teacher_kl", 0), teacher_kl_floor=config.get("teacher_kl", 0),
        seed=state["seed"] + tribe * 1000 + row["updates"], critic_warmup=2 if tribe != 2 else 0)
    state["status"] = "optimizing"
    write_json(args.work_dir / "state.json", state)
    batch, reward_metrics = build_batch(episodes, training_config)
    optimizer, metrics = train_update(policy, batch, training_config, optimizer=optimizer, teacher_policy=teacher_policy,
        progress_callback=lambda progress: emit(dict(tribe=tribe, training=progress)))
    checkpoint = directory / "policy.bin"
    version = row["version"] + 1
    save_checkpoint(policy, checkpoint, version=version, optimizer=optimizer, metadata=dict(
        mode="ppo", own_tribe=tribe, phase=state["phase"], iteration=row["updates"],
        cohort_number=row["updates"] + 1, skill_updates=row["updates"] - row.get("pre_skill_updates", 0) + 1,
        recovery_cohort=row["recovery_updates"] + 1 if "recovery_updates" in row else None,
        training_config=asdict(training_config), source_policy=row["policy"], source_sha256=row["sha256"],
        cohort=str(directory / "games" / "accepted_reports.json"), **reward_metrics, **metrics))
    wins = sum(learning_status(r) == WIN for r in reports)
    for report in reports:
        if report["opponent_key"] != "builtin":
            score = state["scores"].setdefault(report["opponent_key"], dict(wins=0, games=0))
            score["games"] += 1
            score["wins"] += int(learning_status(report) == WIN)
    row.update(policy=str(checkpoint.resolve()), sha256=sha(checkpoint), version=version,
               updates=row["updates"] + 1, games=row["games"] + len(reports), last_cohort=dict(games=len(reports), wins=wins))
    if "recovery_updates" in row:
        row["recovery_updates"] += 1
    row["pool"].append(dict(policy=row["policy"], sha256=row["sha256"], version=version))
    # Keep the transfer, best measured checkpoint and recent history per race.
    trim_pool(row)
    state.update(status="checkpoint_saved", active=None)
    write_json(args.work_dir / "state.json", state)
    emit(dict(checkpoint=RACES[tribe], version=version, games=len(reports), wins=wins, path=str(checkpoint)))
    if args.keep_rollouts:
        for episode in episodes:
            close_episode(episode)
    else:
        discard_accepted(episodes)
        # run_games has already closed the other owner's diagnostic rollout.
        for report in reports:
            for key in ("rollout", "rollout2"):
                if report.get(key) and report[key] not in paths:
                    Path(report[key]).unlink(missing_ok=True)
    if row["updates"] % config["evaluate_every"] == 0:
        evaluate(state, tribe, directory / "evaluation")
    return row


def active_order(state):
    order = tuple(state.get("config", {}).get("active_races", ORDER))
    if not order or len(set(order)) != len(order) or any(type(t) is not int or t not in RACES for t in order):
        raise ValueError("active_races must be a nonempty list of distinct race IDs")
    return order


def train(args):
    state_path = args.work_dir / "state.json"
    state = json.loads(state_path.read_text(encoding="utf-8"))
    if state["schema_crc"] != SCHEMA_CRC or sha(state["executable"]) != state["executable_sha256"]:
        raise ValueError("training runtime/schema changed; initialize an explicit new experiment")
    if sha(Path(__file__).with_name("commander_races.json")) != state["catalog_sha256"]:
        raise ValueError("race action catalog changed")
    if sha(Path(__file__).with_name("commander_skills.json")) != state.get("skills_catalog_sha256"):
        raise ValueError("skill action catalog changed")
    if state.get("training_sources") != training_sources():
        raise ValueError("training sources changed; initialize an explicit new experiment")
    if state.get("recovery") and not state["recovery"].get("comparison_complete"):
        raise ValueError("compare recovery candidates against the baseline before PPO")
    order = active_order(state)
    torch.set_num_threads(state["config"]["threads"])
    stop_file = args.work_dir / "STOP"
    stop_file.unlink(missing_ok=True)
    state.update(pid=os.getpid(), status="running", started_utc=datetime.now(timezone.utc).isoformat())
    write_json(state_path, state)
    target_cycle = state["cycles"] + args.cycles
    try:
        while state["cycles"] < target_cycle and not stop_file.exists():
            position = state["next_race"]
            tribe = order[position]
            train_race(args, state, tribe)
            state["next_race"] = (position + 1) % len(order)
            if not state["next_race"]:
                state["cycles"] += 1
                if (not state["config"].get("builtin_only") and state["phase"] == "bootstrap"
                        and all(r["ready"] for r in state["races"].values())):
                    state["phase"] = "selfplay"
                    emit(dict(phase="selfplay", builtin_fraction=0.2, historical_fraction=0.8))
            write_json(state_path, state)
        state["status"] = "stopped" if stop_file.exists() else "cycle_limit"
    except BaseException as error:
        state.update(status="error", error=f"{type(error).__name__}: {error}")
        raise
    finally:
        state["pid"] = None
        write_json(state_path, state)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("init", "train", "status", "stop"))
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--source", type=Path, default=WORKSPACE / "debug_artifacts/commander/strong_bot_20260908/initial_v9.bin")
    parser.add_argument("--resume-from", type=Path, help="init only: fork a stopped campaign, preserving progress and optimizer")
    parser.add_argument("--exe", type=Path, default=WORKSPACE / "build/commander_skills/ranker_rebuild.exe")
    parser.add_argument("--install-dir", type=Path, default=WORKSPACE / "RankerOCPV_Win")
    parser.add_argument("--games", type=int, default=12)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=260908)
    parser.add_argument("--min-updates", type=int, default=8)
    parser.add_argument("--evaluate-every", type=int, default=4)
    parser.add_argument("--evaluation-games", type=int, default=48)
    parser.add_argument("--learning-rate", type=float, default=2.5e-5)
    parser.add_argument("--cycles", type=int, default=24)
    parser.add_argument("--keep-rollouts", action="store_true")
    args = parser.parse_args(argv)
    args.work_dir = args.work_dir.resolve()
    if args.resume_from is not None and args.command != "init":
        parser.error("--resume-from is only valid with init")
    if min(args.games, args.workers, args.threads, args.cycles, args.min_updates, args.evaluate_every, args.evaluation_games) < 1:
        parser.error("counts must be positive")
    if args.evaluation_games % 8 or args.evaluation_games < 8:
        parser.error("evaluation-games must be a positive multiple of 8")
    if not np.isfinite(args.learning_rate) or args.learning_rate <= 0:
        parser.error("learning rate must be finite and positive")
    if not 1 <= args.seed <= 0xFFFFFFFF:
        parser.error("seed must be a nonzero u32")
    if args.command == "status":
        state = json.loads((args.work_dir / "state.json").read_text(encoding="utf-8"))
        emit({key: state[key] for key in ("status", "phase", "cycles", "races")})
    elif args.command == "stop":
        (args.work_dir / "STOP").write_text("stop after the current cohort\n", encoding="utf-8")
        emit(dict(stop_requested=True))
    else:
        with runner_lock(args.work_dir):
            initialize(args) if args.command == "init" else train(args)


if __name__ == "__main__":
    main()
