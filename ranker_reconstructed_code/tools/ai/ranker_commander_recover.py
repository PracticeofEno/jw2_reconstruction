"""Recover Elf, Demon and Primitive against full-speed builtin opponents.

prepare: fork the stopped campaign and fit BC candidates to recorded wins.
train: compare each candidate with its best prior policy on new seeds, then PPO.
No game executable is needed for prepare. Candidates are not assumed stronger.
The Tyrano checkpoint and parent campaign remain untouched; self-play is off.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import torch

import ranker_commander_multirace as races
from ranker_commander_model import SCHEMA_CRC, load_weights
from ranker_commander_rollout import WIN, read_rollout
from ranker_commander_train import (TrainConfig, actor_trajectory_fingerprint, build_batch,
                                    close_episode, save_checkpoint, train_update)
from ranker_commander_watch import receipts

ACTIVE = (1, 3, 0)
DEFAULT_WORK = races.WORKSPACE / "debug_artifacts/commander/builtin_recovery_20260909/training_run"


def checkpoint_entry(row):
    return {key: row[key] for key in ("policy", "sha256", "version")}


def winning_reports(parent, tribe):
    """One winning trajectory per game seed/opponent, not repeated eval versions."""
    selected = {}
    for path in receipts(parent):
        row = json.loads(path.read_text(encoding="utf-8"))
        if (not row.get("valid") or row.get("status") != WIN or row.get("teacher")
                or row.get("own_tribe") != tribe or row.get("train_owner", 1) != 1
                or row.get("rollout2") or row.get("curriculum") != 2
                or row.get("opponent_key", "builtin") != "builtin"
                or row.get("opp_slow", 0) != 0 or not Path(row["rollout"]).is_file()
                or row.get("commander_metrics", {}).get("mask_violations", 0)):
            continue
        key = row["seed"], row["tribe"]
        if key not in selected or row["end_frame"] < selected[key]["end_frame"]:
            selected[key] = dict(row, receipt=str(path.resolve()))
    return [selected[key] for key in sorted(selected)]


def prefer_candidate(baseline, candidate):
    """Conservative development selection, not a statistical/champion claim."""
    if not baseline["coverage_complete"] or not candidate["coverage_complete"] or candidate["mask_violations"]:
        return False
    if baseline["games"] != candidate["games"]:
        return False
    modes = ("argmax_win_rate", "sampling_win_rate")
    gained_wins = sum(candidate[k] - baseline[k] for k in modes) * candidate["games"] / 2
    return all(candidate[k] >= baseline[k] for k in modes) and gained_wins >= 2 - 1e-9


def fork_state(args):
    state_path = args.work_dir / "state.json"
    if state_path.exists():
        raise FileExistsError("recovery exists; use warmstart or train to resume it")
    parent = args.parent.resolve()
    with races.runner_lock(parent.parent):
        raw = parent.read_bytes()
        state = json.loads(raw)
        if state["schema_crc"] != SCHEMA_CRC:
            raise ValueError("recovery requires the current skill observation schema")
        for tribe in range(4):
            races.verify_policy(state["races"][str(tribe)], tribe)
        args.work_dir.mkdir(parents=True, exist_ok=True)
        (args.work_dir / "parent_state.json").write_bytes(raw)
    state.update(parent_state=str(parent), parent_state_sha256=hashlib.sha256(raw).hexdigest(),
                 parent_cycles=state["cycles"], phase="bootstrap", cycles=0, next_race=0,
                 next_seed=1800000000, seed=260909, pid=None, active=None, status="preparing",
                 started_utc=datetime.now(timezone.utc).isoformat(), training_sources=races.training_sources())
    state.pop("error", None)
    state["config"].update(active_races=list(ACTIVE), builtin_only=True, evaluate_every=2,
                           evaluation_seed=3600000000, teacher_kl=0.02)
    state["recovery"] = dict(method="winning-episode BC followed by on-policy PPO",
        comparison_complete=False, frozen_tyrano=deepcopy(state["races"]["2"]),
        evaluation_use="new development seeds; old evaluation wins now belong to training data",
        selection="at least two more wins; neither argmax nor sampling win rate may fall")
    for tribe in ACTIVE:
        row = state["races"][str(tribe)]
        previous = checkpoint_entry(row)
        baseline = checkpoint_entry(row.get("best_evaluated", row))
        races.verify_policy(baseline, tribe)
        row.update(previous_checkpoint=previous, baseline=baseline, **baseline,
                   candidate=None, ready=False, recovery_updates=0)
        row.pop("evaluation", None)
        row.pop("best_evaluated", None)
    races.write_json(state_path, state)
    return state


def verify_fork(state):
    if state.get("training_sources") != races.training_sources():
        raise ValueError("recovery sources changed; prepare an explicit new experiment")
    if state["races"]["2"] != state["recovery"]["frozen_tyrano"]:
        raise ValueError("the excluded Tyrano state changed")
    races.verify_policy(state["races"]["2"], 2)
    if races.sha(state["parent_state"]) != state["parent_state_sha256"]:
        raise ValueError("parent campaign changed after the fork")
    for file, key in (("commander_races.json", "catalog_sha256"), ("commander_skills.json", "skills_catalog_sha256")):
        if races.sha(Path(__file__).with_name(file)) != state[key]:
            raise ValueError(f"runtime action catalog changed: {file}")


def warmstart(args, state):
    verify_fork(state)
    torch.set_num_threads(state["config"]["threads"])
    for tribe in ACTIVE:
        row = state["races"][str(tribe)]
        if row.get("candidate"):
            races.verify_policy(row["candidate"], tribe)
            continue
        reports = winning_reports(Path(state["parent_state"]).parent, tribe)
        episodes, sources, hashes = [], [], set()
        try:
            for report in reports:
                episode = read_rollout(report["rollout"], teacher=False)
                if (episode.owner != 1 or episode.seed != report["seed"]
                        or episode.weight_version != report["weight_version"]
                        or int(episode.terminal["status"]) != WIN
                        or not np.all(episode.records["vector"][:, 606:610] == np.eye(4)[tribe])):
                    close_episode(episode)
                    raise ValueError("winning source does not match its race/receipt")
                fingerprint = actor_trajectory_fingerprint(episode)
                if fingerprint in hashes:
                    close_episode(episode)
                    continue
                hashes.add(fingerprint)
                episodes.append(episode)
                sources.append(dict(receipt=report["receipt"], rollout=report["rollout"],
                    seed=episode.seed, opponent=report["tribe"], weight_version=episode.weight_version,
                    sha256=races.sha(episode.path), actor_fingerprint=fingerprint))
            if not episodes:
                raise RuntimeError(f"no intact current-schema winning episodes for {races.RACES[tribe]}")
            directory = args.work_dir / races.RACES[tribe] / "warmstart"
            races.write_json(directory / "sources.json", sources)
            state.update(status="offline_warmstart", active=dict(tribe=tribe, episodes=len(episodes)))
            races.write_json(args.work_dir / "state.json", state)
            config = TrainConfig(mode="bc", epochs=args.epochs, minibatch=2048, gamma=1.0,
                gae_lambda=0.98, learning_rate_initial=2e-5, learning_rate_final=2e-5,
                bc_class_power=0.25, bc_class_cap=4, bc_class_skip=(38, 39, 40, 41),
                seed=260909 + tribe)
            policy = load_weights(row["baseline"]["policy"])
            batch, reward_metrics = build_batch(episodes, config)
            _, metrics = train_update(policy, batch, config,
                progress_callback=lambda progress: races.emit(dict(warmstart=races.RACES[tribe], training=progress)))
            checkpoint = directory / "policy.bin"
            # BC Adam moments are deliberately not resumed as on-policy PPO moments.
            save_checkpoint(policy, checkpoint, version=row["previous_checkpoint"]["version"] + 1,
                metadata=dict(mode="winning_episode_bc", own_tribe=tribe, phase="offline_warmstart",
                    source_policy=row["baseline"]["policy"], source_sha256=row["baseline"]["sha256"],
                    sources=str(directory / "sources.json"), unique_winning_episodes=len(episodes),
                    training_config=asdict(config), admission="unvalidated development candidate",
                    optimizer_reset_for_ppo=True, **reward_metrics, **metrics))
            row["candidate"] = dict(policy=str(checkpoint.resolve()), sha256=races.sha(checkpoint),
                                    version=policy.weight_version)
            races.write_json(args.work_dir / "state.json", state)
            races.emit(dict(warmstart_complete=races.RACES[tribe], episodes=len(episodes),
                            decisions=reward_metrics["decisions"], candidate=row["candidate"], gameplay_evaluated=False))
        finally:
            for episode in episodes:
                close_episode(episode)
    state.update(status="awaiting_runtime_comparison", active=None)
    races.write_json(args.work_dir / "state.json", state)
    verify_fork(state)


def compare(args, state):
    verify_fork(state)
    if state["recovery"]["comparison_complete"]:
        return True
    if races.sha(state["executable"]) != state["executable_sha256"]:
        raise ValueError("runtime changed; this experiment requires its recorded executable hash")
    for tribe in ACTIVE:
        row = state["races"][str(tribe)]
        if row.get("recovery_selection"):
            continue
        measured = {}
        for name, directory in (("baseline", args.work_dir / races.RACES[tribe] / "baseline"),
                                ("candidate", args.work_dir / races.RACES[tribe] / "warmstart/evaluation")):
            if (args.work_dir / "STOP").exists():
                state.update(status="stopped", active=None)
                races.write_json(args.work_dir / "state.json", state)
                return False
            entry = row.get(name)
            if not entry:
                raise ValueError("finish warmstart before gameplay comparison")
            races.verify_policy(entry, tribe)
            paths = row.setdefault("comparison_paths", {})
            directory = Path(paths.get(name, directory))
            if directory.exists() and not (directory / "evaluation.json").exists():
                # Keep interrupted matches for diagnosis and start a fresh attempt.
                directory = directory.with_name(directory.name + "_retry_" + races.uuid.uuid4().hex[:8])
            paths[name] = str(directory)
            state.update(status="recovery_comparison", active=dict(tribe=tribe, policy=name))
            races.write_json(args.work_dir / "state.json", state)
            result_path = directory / "evaluation.json"
            if not result_path.exists():
                testing = deepcopy(state)
                testing["races"][str(tribe)].update(entry)
                races.evaluate(testing, tribe, directory)
            result = json.loads(result_path.read_text(encoding="utf-8"))
            if result["weights_sha256"] != entry["sha256"]:
                raise ValueError("cached comparison belongs to different weights")
            # Recheck coverage and seeds instead of trusting only a cached score.
            expected = state["config"]["evaluation_games"]
            for report in result["reports"]:
                if (report.get("weights_sha256") != entry["sha256"] or report.get("own_tribe") != tribe
                        or not state["config"]["evaluation_seed"] + tribe * 10000 < report["seed"]
                        <= state["config"]["evaluation_seed"] + tribe * 10000 + expected // 2):
                    raise ValueError("comparison contains a different policy, race or seed")
            measured[name] = races.bootstrap_gate(result["reports"], updates=0, minimum_updates=1, expected_games=expected)
            if not measured[name]["coverage_complete"] or measured[name]["mask_violations"]:
                raise RuntimeError("incomplete comparison cannot select a recovery policy")
        chosen = "candidate" if prefer_candidate(measured["baseline"], measured["candidate"]) else "baseline"
        entry = row[chosen]
        row.update(entry, anchor_policy=entry["policy"], anchor_sha256=entry["sha256"], recovery_updates=0,
                   recovery_selection=dict(selected=chosen, measurements=measured))
        row["pool"].append(checkpoint_entry(row))
        row["best_evaluated"] = dict(**checkpoint_entry(row),
            score=(measured[chosen]["argmax_win_rate"] + measured[chosen]["sampling_win_rate"]) / 2,
            evaluation=str(Path(row["comparison_paths"][chosen]) / "evaluation.json"))
        races.trim_pool(row)
        races.write_json(args.work_dir / "state.json", state)
        races.emit(dict(recovery_selection=races.RACES[tribe], **row["recovery_selection"]))
    state["recovery"]["comparison_complete"] = True
    state.update(status="ready_to_train", active=None)
    races.write_json(args.work_dir / "state.json", state)
    verify_fork(state)
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "warmstart", "compare", "train", "status", "stop"))
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--parent", type=Path, default=races.DEFAULT_WORK / "state.json")
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--cycles", type=int, default=24)
    parser.add_argument("--keep-rollouts", action="store_true")
    args = parser.parse_args(argv)
    if min(args.epochs, args.cycles) < 1:
        parser.error("epochs and cycles must be positive")
    args.work_dir = args.work_dir.resolve()
    if args.command == "stop":
        (args.work_dir / "STOP").write_text("stop after the current cohort\n", encoding="utf-8")
        return
    if args.command == "status":
        races.emit(json.loads((args.work_dir / "state.json").read_text(encoding="utf-8")))
        return
    with races.runner_lock(args.work_dir):
        state = (fork_state(args) if args.command == "prepare" else
                 json.loads((args.work_dir / "state.json").read_text(encoding="utf-8")))
        state.update(pid=os.getpid(), started_utc=datetime.now(timezone.utc).isoformat())
        state.pop("error", None)
        (args.work_dir / "STOP").unlink(missing_ok=True)
        races.write_json(args.work_dir / "state.json", state)
        try:
            if args.command in ("prepare", "warmstart"):
                warmstart(args, state)
            elif compare(args, state) and args.command == "train":
                races.train(args)
                state = json.loads((args.work_dir / "state.json").read_text(encoding="utf-8"))
        except BaseException as error:
            # Preserve any last completed PPO checkpoint saved by the inner runner.
            state = json.loads((args.work_dir / "state.json").read_text(encoding="utf-8"))
            state.update(status="error", error=f"{type(error).__name__}: {error}")
            raise
        finally:
            state["pid"] = None
            races.write_json(args.work_dir / "state.json", state)


if __name__ == "__main__":
    main()
