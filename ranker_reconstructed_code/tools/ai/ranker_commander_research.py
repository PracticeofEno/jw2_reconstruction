"""One auditable research PPO update; never collects games or grants admission."""
from __future__ import annotations

import os
for _variable in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
    os.environ[_variable] = "1"
import argparse
from dataclasses import asdict, fields
import hashlib
import json
import math
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
import numpy as np
import torch
import ranker_commander_model as model
import ranker_commander_rollout as rollout
import ranker_commander_train as training


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def save_json(path, value):
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def pin(inputs, path, expected=None):
    path = str(Path(path).resolve())
    actual = digest(path)
    require(expected is None or actual == expected, f"input hash mismatch: {path}")
    require(path not in inputs or inputs[path] == actual, f"conflicting input hash: {path}")
    inputs[path] = actual
    return actual


def verify_hashes(inputs):
    for path, expected in inputs.items():
        require(digest(path) == expected, f"input changed during update: {path}")


def restore_config(metadata, *, lr=2.5e-5, epochs=3, minibatch=2048, seed=1000,
                   iteration=None, gamma=None, gae_lambda=None, shaping_scale=None):
    """Research provenance is independent of approval and metadata.mode."""
    key = "training_config" if metadata.get("training_config") else "source_training_config"
    saved = metadata.get(key)
    required = {"iteration", "gamma", "gae_lambda", "shaping_scale", "teacher_kl_initial",
                "teacher_kl_floor", "teacher_kl_decay", "schedule_iterations", "critic_warmup"}
    require(isinstance(saved, dict) and required <= saved.keys(), "missing explicit saved research training settings")
    names = {field.name for field in fields(training.TrainConfig)}
    config_values = {name: value for name, value in saved.items() if name in names}
    previous_iteration = saved["iteration"]
    require(type(previous_iteration) is int and previous_iteration >= 0, "invalid saved iteration")
    overrides = {"mode": "ppo", "epochs": epochs, "minibatch": minibatch, "seed": seed,
                 "iteration": previous_iteration + 1 if iteration is None else iteration,
                 "learning_rate_initial": lr, "learning_rate_final": lr}
    overrides["shaping_scale"] = metadata.get("shaping_scale", saved["shaping_scale"])
    for name, value in (("gamma", gamma), ("gae_lambda", gae_lambda), ("shaping_scale", shaping_scale)):
        if value is not None:
            overrides[name] = value
    config_values.update(overrides)
    config = training.TrainConfig(**config_values)
    for name in ("epochs", "minibatch", "seed", "schedule_iterations"):
        require(type(getattr(config, name)) is int and getattr(config, name) > 0, f"invalid {name}")
    for name in ("iteration", "critic_warmup", "teacher_kl_decay"):
        require(type(getattr(config, name)) is int and getattr(config, name) >= 0, f"invalid {name}")
    for name in ("shaping_scale", "teacher_kl_initial", "teacher_kl_floor"):
        require(math.isfinite(getattr(config, name)) and getattr(config, name) >= 0, f"invalid {name}")
    require(math.isfinite(lr) and lr > 0, "learning rate must be finite and positive")
    return config, {"settings_source": key, "saved": saved, "deliberate_overrides": overrides,
                    "previous_iteration": previous_iteration, "source_admission_migrated": False}


def verify_phase(runtime_path, cohort_path, weights, version, expected_games, inputs):
    """Validate completed collection, exact source weights, jobs and native artifacts."""
    require(type(expected_games) is int and expected_games > 0, "expected games must be positive")
    runtime_path, cohort_path, weights = map(lambda p: Path(p).resolve(), (runtime_path, cohort_path, weights))
    runtime = read_json(runtime_path)
    required_schema = {"schema_crc": rollout.SCHEMA_CRC, "vector_size": 606, "map_size": 3072,
                       "weight_format": 2, "rollout_format": 4, "rollout_record_bytes": 4446}
    require(all(runtime.get(k) == v for k, v in required_schema.items()), "runtime schema must be 606/12 format2/RLO4")
    require(runtime.get("input_hashes"), "runtime has no pinned inputs")
    for key in ("input_hashes", "native_source_hashes"):
        for path, expected in runtime.get(key, {}).items():
            pin(inputs, path, expected)
    require(str(Path(runtime["executable"]).resolve()) in inputs, "runtime executable is not pinned")
    directory = cohort_path.parent
    plan_path, result_path, state_path = (directory / name for name in ("plan.json", "results.json", "state.json"))
    plan, result, state = map(read_json, (plan_path, result_path, state_path))
    runtime_sha = pin(inputs, runtime_path)
    links = {"phase_plan_sha256": pin(inputs, plan_path), "evaluation_sha256": pin(inputs, cohort_path),
             "runtime_contract_sha256": runtime_sha}
    require(state.get("state") == "complete" and result.get("complete") is True, "collection is not complete")
    require(result.get("diagnostic_only") is False and result.get("full_condition_coverage") is True and
            result.get("performance_games") == expected_games, "incomplete requested job coverage")
    require(plan.get("mode") == "sample" and plan.get("deterministic") is False and plan.get("teacher") is False,
            "PPO requires a sampled nonteacher phase")
    require(plan.get("max_frames") == 60000 and Path(plan["weights"]).resolve() == weights,
            "phase does not match source weights or frame contract")
    require(Path(plan["runtime_contract"]).resolve() == runtime_path and plan.get("runtime_contract_sha256") == runtime_sha,
            "phase runtime contract mismatch")
    require(Path(plan["executable"]).resolve() == Path(runtime["executable"]).resolve(), "phase executable mismatch")
    require(plan.get("schema_crc") == rollout.SCHEMA_CRC and plan.get("vector_size") == 606 and
            plan.get("map_size") == 3072 and plan.get("weight_version") == version, "phase model schema/version mismatch")
    require(all(result.get(k) == v and state.get(k) == v for k, v in links.items()), "phase provenance links disagree")
    require(state.get("results_sha256") == pin(inputs, result_path), "phase results changed")
    pin(inputs, state_path)
    for path, expected in plan["input_hashes"].items():
        pin(inputs, path, expected)
    weights_sha = pin(inputs, weights)
    reports = read_json(cohort_path)["reports"]
    jobs = plan["jobs"]
    indices = plan["job_indices"]
    require(len(jobs) == len(indices) == len(reports) == expected_games, "wrong number of jobs or reports")
    require(len(set(indices)) == expected_games and {job["job"] for job in jobs} == set(indices), "duplicate planned jobs")
    require({row["job"] for row in reports} == set(indices) and set(result["requested_jobs"]) == set(indices),
            "requested and completed job sets differ")
    require(len({row["job"] for row in reports}) == expected_games, "duplicate reports")
    planned = {job["job"]: job for job in jobs}
    episode_paths = set()
    for row in reports:
        require(row.get("valid") is True and row.get("evaluation_valid") is True and row.get("teacher") is False and
                row.get("deterministic") is False and row.get("diagnostic_only") is False and
                row.get("weights_sha256") == weights_sha and row.get("weight_version") == version,
                "report is not fresh sampled data from the exact initial policy")
        job = planned[row["job"]]
        for key in ("seed", "tribe", "start_pair", "curriculum", "max_frames", "opp_slow", "coordinated_transfers", "policy_seed"):
            require(key in job and row.get(key) == job[key], f"report/planned {key} mismatch")
        require(row.get("policy_seed_verified") is True and row.get("coordinated_transfers_verified") is True,
                "native execution overrides were not verified")
        require(row["coordinated_transfers"] == plan.get("coordinated_transfers"), "mixed execution contracts")
        artifacts = row.get("artifact_hashes", {})
        required_artifacts = {"job.json", "Jw2.log", "commander.rlo", "commander_metrics_1.json", "ai_selfplay_result.json"}
        require(required_artifacts <= {Path(path).name for path in artifacts}, "missing native artifact hashes")
        for path, expected in artifacts.items():
            pin(inputs, path, expected)
        path = str(Path(row["rollout"]).resolve())
        require(path in {str(Path(item).resolve()) for item in artifacts} and path not in episode_paths and
                Path(path).name == "commander.rlo", "missing or duplicate rollout")
        episode_paths.add(path)
        command = row.get("command", [])
        require(command and Path(command[0]).resolve() == Path(runtime["executable"]).resolve(), "report executable mismatch")
        require("-AIDETERMINISTIC" not in command and "-AIARGMAX" not in command and "-AITEACHER" not in command and
                f"-AIPOLICYSEED:{row['policy_seed']}" in command, "report command is not the planned sampling policy")
        require(f"-AIWEIGHTS:{weights}" in command and f"-AIROLLOUT:{path}" in command and
                f"-AICOORDINATEDTRANSFERS:{int(row['coordinated_transfers'])}" in command,
                "native command weights/rollout/execution contract mismatch")
    return reports, plan


def build_verified_batch(reports, version, config):
    episodes = []
    labels = {name: [] for name in ("frame", "tribe", "outcome", "job", "mc_target")}
    try:
        for row in reports:
            episode = rollout.read_rollout(row["rollout"], current_version=version, teacher=False)
            episodes.append(episode)
            require(episode.weight_version == version and episode.seed == row["seed"] and episode.owner == 1,
                    "rollout source version/seed/owner mismatch")
            require(episode.records.dtype["vector"].shape == (606,) and episode.records.dtype["map"].shape == (3072,),
                    "historical or padded observations cannot train")
            require(len(episode.decisions) == row["decisions"] and int(episode.terminal["status"]) == row["status"] and
                    int(episode.terminal["frame"]) == row["end_frame"], "rollout/report outcome mismatch")
        batch, rewards = training.build_batch(episodes, config)
        for row, episode in zip(reports, episodes):
            count = len(episode.decisions)
            labels["frame"].append(episode.decisions["frame"].copy())
            for key, value in (("tribe", row["tribe"]), ("outcome", row["status"]), ("job", row["job"])):
                labels[key].append(np.full(count, value, dtype=np.int64))
            returns = rollout.episode_returns(episode, iteration=config.iteration, shaping_scale=config.shaping_scale,
                                               gamma=config.gamma, gae_lambda=config.gae_lambda)
            labels["mc_target"].append(returns["mc_return"])
        require(torch.count_nonzero(batch["vector"][:, 542:]).item() > 0 and
                torch.count_nonzero(batch["maps"][:, 9:]).item() > 0, "new native observation blocks are absent")
        return batch, rewards, {name: np.concatenate(parts) for name, parts in labels.items()}
    finally:
        for episode in episodes:
            episode.close()


def prediction(policy, batch):
    policy.eval()
    parts = {key: [] for key in ("logits", "logp", "value")}
    with torch.no_grad():
        for start in range(0, len(batch["target"]), 2048):
            sample = {key: value[start:start+2048] for key, value in batch.items()}
            out = policy.evaluate(sample["vector"], sample["maps"], sample["actions"], sample["masks"], sample["privileged"])
            for key in parts:
                parts[key].append(out[key].detach())
    return {key: torch.cat(value) for key, value in parts.items()}


def distribution(values):
    values = np.asarray(values, dtype=np.float64)
    if not len(values):
        return {"count": 0, "mean": None, "min": None, "max": None, "p50": None, "p95": None, "p99": None}
    return {"count": len(values), "mean": float(values.mean()), "min": float(values.min()), "max": float(values.max()),
            **{f"p{q}": float(np.percentile(values, q)) for q in (50, 95, 99)}}


def critic_metrics(values, targets):
    values, targets = np.asarray(values, dtype=np.float64), np.asarray(targets, dtype=np.float64)
    if not len(values):
        return {"decisions": 0, "mse": None, "explained_variance": None}
    variance = float(np.var(targets))
    return {"decisions": len(values), "mse": float(np.mean((values-targets)**2)),
            "explained_variance": None if variance <= 1e-12 else float(1-np.var(targets-values)/variance)}


def diagnostics(current, reference, batch, labels):
    """Conditional distributions use recorded old-policy prefixes, not new trajectories."""
    difference = current["logp"].sum(1) - batch["old_logp"]
    ratio = difference.exp()
    require(torch.isfinite(ratio).all().item(), "nonfinite full-cohort PPO ratio")
    advantage = batch["advantage"]
    values = current["value"].reshape(-1).numpy()
    targets = batch["target"].numpy()
    result = {"decisions": len(ratio), "approximate_kl": float(((ratio-1)-difference).double().mean()),
              "clip_fraction": float(((ratio-1).abs()>.2).double().mean()),
              "actor_loss": -float(torch.minimum(ratio*advantage, ratio.clamp(.8,1.2)*advantage).double().mean()),
              "value_loss": float(np.mean(.5*(values.astype(np.float64)-targets)**2)),
              "max_abs_joint_log_ratio": float(difference.abs().max()),
              "joint_log_ratio": distribution(difference.numpy()), "joint_ratio": distribution(ratio.numpy()),
              "heads": [], "critic": {},
              "scope": "training cohort; reference conditional KL uses recorded prefixes; not generalization or trajectory proof"}
    for head, (offset, width) in enumerate(zip(rollout.HEAD_OFFSETS, rollout.HEAD_SIZES)):
        legal = batch["masks"][:, offset:offset+width]
        old = reference["logits"][:, offset:offset+width].masked_fill(~legal, -1e9).log_softmax(-1)
        new = current["logits"][:, offset:offset+width].masked_fill(~legal, -1e9).log_softmax(-1)
        kl = (old.exp()*(old-new)).double().sum(-1).clamp_min(0).numpy()
        active = legal.sum(1).numpy() > 1
        flips = old.argmax(-1).ne(new.argmax(-1)).numpy()
        if head == 0:
            old_action, new_action = old.argmax(-1).numpy(), new.argmax(-1).numpy()
            confusion = np.zeros((width, width), dtype=np.int64)
            np.add.at(confusion, (old_action, new_action), 1)
            old_production = (old_action >= 1) & (old_action <= 11)
            new_production = (new_action >= 1) & (new_action <= 11)
            result["macro_transitions"] = {
                "confusion_old_rows_new_columns": confusion.tolist(),
                "unit_production_action_ids": list(range(1,12)),
                "old_noop_to_production": int(((old_action == 0) & new_production).sum()),
                "old_production_to_noop": int((old_production & (new_action == 0)).sum()),
                "old_noop_count": int((old_action == 0).sum()), "old_production_count": int(old_production.sum()),
                "old_noop_to_production_rate": float(((old_action == 0) & new_production).sum()/max(1,(old_action == 0).sum())),
                "old_production_to_noop_rate": float((old_production & (new_action == 0)).sum()/max(1,old_production.sum()))}
        result["heads"].append({"index": head, "conditional_kl": distribution(kl),
            "active_choice_kl": distribution(kl[active]), "forced_choice_kl": distribution(kl[~active]),
            "argmax_flips": int(flips.sum()), "active_argmax_flips": int(flips[active].sum()),
            "active_meaning": "more than one legal choice; not proof the executor applies this head",
            "sampled_log_ratio": distribution((current["logp"][:,head]-reference["logp"][:,head]).numpy())})
    result["macro_argmax_flips"] = result["heads"][0]["argmax_flips"]
    groups = {"all": np.ones(len(values), dtype=bool), "early_lt4000": labels["frame"] < 4000}
    groups.update({f"tribe_{value}": labels["tribe"] == value for value in np.unique(labels["tribe"])})
    groups.update({f"outcome_{value}": labels["outcome"] == value for value in np.unique(labels["outcome"])})
    for name, mask in groups.items():
        result["critic"][name] = {"gae_target": critic_metrics(values[mask], targets[mask]),
                                  "same_mc_target": critic_metrics(values[mask], labels["mc_target"][mask])}
    return result


def optimizer_steps(optimizer, policy):
    return {name: int(optimizer.state.get(parameter, {}).get("step", 0)) for name, parameter in policy.named_parameters()}


def verify_checkpoint(path, policy, optimizer, version, expected_steps):
    restored = model.load_weights(path)
    require(restored.weight_version == version, "reload weight version mismatch")
    require(all(torch.equal(value, restored.state_dict()[name]) for name, value in policy.state_dict().items()),
            "checkpoint weights failed exact reload")
    loaded = training.load_optimizer(torch.optim.Adam(restored.parameters()), Path(str(path)+".optimizer.npz"), version=version)
    left, right = optimizer.state_dict(), loaded.state_dict()
    require(json.dumps(left["param_groups"], sort_keys=True) == json.dumps(right["param_groups"], sort_keys=True),
            "optimizer param groups failed reload")
    require(left["state"].keys() == right["state"].keys(), "optimizer state keys failed reload")
    for index, slots in left["state"].items():
        require(slots.keys() == right["state"][index].keys() and
                all(torch.equal(value, right["state"][index][key]) for key, value in slots.items()),
                f"optimizer moments failed exact reload: {index}")
    require(optimizer_steps(loaded, restored) == expected_steps, "optimizer step progression mismatch")
    return {"weights_sha256": digest(path), "sidecar_sha256": digest(Path(str(path)+".json")),
            "optimizer_sha256": digest(Path(str(path)+".optimizer.npz")), "weight_version": version,
            "all_tensors_reload_exact": True, "optimizer_reload_exact": True, "adam_steps": expected_steps}


def select_safe(initial, epochs):
    safe = [row for row in epochs if row["eligible_for_selection"]]
    return safe[-1] if safe else initial


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime-contract", "cohort", "weights", "teacher", "out"):
        result.add_argument("--"+name, type=Path, required=True)
    result.add_argument("--expected-games", type=int, default=48)
    result.add_argument("--lr", type=float, default=2.5e-5)
    result.add_argument("--epochs", type=int, default=3)
    result.add_argument("--minibatch", type=int, default=2048)
    result.add_argument("--seed", type=int, default=1000)
    result.add_argument("--max-kl", type=float, default=.03)
    result.add_argument("--iteration", type=int)
    result.add_argument("--gamma", type=float)
    result.add_argument("--gae-lambda", type=float)
    result.add_argument("--shaping-scale", type=float)
    result.add_argument("--verify-only", action="store_true")
    return result


def run(args):
    torch.set_num_threads(1)
    torch.manual_seed(args.seed)
    torch.use_deterministic_algorithms(True)
    require(math.isfinite(args.max_kl) and args.max_kl > 0, "max KL must be finite and positive")
    require(rollout.SCHEMA_CRC == 0x53DD6137 and rollout.VECTOR_SIZE == 606 and rollout.MAP_SHAPE == (12,16,16),
            "unsupported training module schema")
    started = time.monotonic()
    inputs = {}
    for module in (model, rollout, training):
        pin(inputs, module.__file__)
    pin(inputs, __file__)
    weights, teacher_path, output = args.weights.resolve(), args.teacher.resolve(), args.out.resolve()
    metadata_path, optimizer_path = Path(str(weights)+".json"), Path(str(weights)+".optimizer.npz")
    metadata = read_json(metadata_path)
    require(isinstance(metadata.get("weights_sha256"), str), "source metadata lacks weights hash")
    require(metadata.get("eligible_for_selection") is not False, "unsafe epoch cannot start another research update")
    pin(inputs, weights, metadata["weights_sha256"])
    pin(inputs, metadata_path)
    pin(inputs, optimizer_path, metadata.get("optimizer_sha256"))
    if metadata.get("reload_verification"):
        proof_path = Path(metadata["reload_verification"])
        proof = read_json(proof_path)
        require(proof.get("weights_sha256") == digest(weights) and proof.get("sidecar_sha256") == digest(metadata_path)
                and proof.get("optimizer_sha256") == digest(optimizer_path) and proof.get("optimizer_reload_exact") is True,
                "source reload verification does not match checkpoint")
        pin(inputs, proof_path)
    pin(inputs, teacher_path)
    teacher_meta = Path(str(teacher_path)+".json")
    if teacher_meta.is_file():
        pin(inputs, teacher_meta)
        require(read_json(teacher_meta).get("weights_sha256") == digest(teacher_path), "teacher metadata hash mismatch")
    policy = model.load_weights(weights).eval()
    teacher = model.load_weights(teacher_path).eval()
    require(metadata.get("schema_crc") == rollout.SCHEMA_CRC and metadata.get("weight_version") == policy.weight_version,
            "source metadata schema/version mismatch")
    config, restoration = restore_config(metadata, **{name: getattr(args, name) for name in
        ("lr", "epochs", "minibatch", "seed", "iteration", "gamma", "gae_lambda", "shaping_scale")})
    version, output_version = policy.weight_version, policy.weight_version + 1
    reports, phase_plan = verify_phase(args.runtime_contract, args.cohort, weights, version, args.expected_games, inputs)
    optimizer = training.load_optimizer(torch.optim.Adam(policy.parameters()), optimizer_path, version=version)
    require(len(list(policy.parameters())) == 67, "unexpected parameter layout")
    steps_before = optimizer_steps(optimizer, policy)
    batch, reward_metrics, labels = build_verified_batch(reports, version, config)
    old_prediction = prediction(policy, batch)
    initial_metrics = diagnostics(old_prediction, old_prediction, batch, labels)
    require(initial_metrics["max_abs_joint_log_ratio"] < 1e-3 and initial_metrics["clip_fraction"] == 0,
            "native/Python initial probabilities disagree")
    verify_hashes(inputs)
    output.mkdir(parents=True, exist_ok=False)
    plan = {"mode": "unapproved_research_ppo", "config": asdict(config), "restoration": restoration,
            "runtime_contract": str(args.runtime_contract.resolve()), "cohort": str(args.cohort.resolve()),
            "initial_weights": str(weights), "teacher_weights": str(teacher_path), "expected_games": args.expected_games,
            "initial_weight_version": version, "output_weight_version_fixed": output_version,
            "initial_optimizer_steps": steps_before, "optimizer_method": "preserve exact matching Adam state; full parameters train",
            "coordinated_transfers": phase_plan["coordinated_transfers"], "input_hashes": inputs,
            "reward_metrics": reward_metrics, "max_epoch_cohort_kl": args.max_kl,
            "bc_gate": {"passed": False, "gameplay_passed": False}, "source_admission_migrated": False,
            "initial_metrics": initial_metrics, "verify_only": args.verify_only,
            "software": {"torch": torch.__version__, "numpy": np.__version__, "python": sys.version, "threads": 1}}
    save_json(output/"plan.json", plan)
    if args.verify_only:
        save_json(output/"verification.json", {"verified": True, "gradient_updates": 0, "input_hashes_preserved": True})
        return {"verified": True, "output": str(output), "gradient_updates": 0}
    epochs = []
    initial_selection = {"path": str(weights), "sha256": digest(weights), "epoch": 0,
                         "weight_version": version, "eligible_for_selection": True}
    stopped = False

    class StopForKL(Exception):
        pass

    def completed_epoch(row):
        current_metrics = diagnostics(prediction(policy, batch), old_prediction, batch, labels)
        eligible = current_metrics["approximate_kl"] <= args.max_kl
        candidate = output / f"epoch_{row['epoch']:03d}.bin"
        proof_path = output / f"verification_epoch_{row['epoch']:03d}.json"
        epoch_metadata = {"mode": "unapproved_research_ppo", "training_config": asdict(config),
            "iteration": config.iteration, "shaping_scale": config.shaping_scale, "epoch": row["epoch"],
            "optimizer_method": plan["optimizer_method"], "coordinated_transfers": plan["coordinated_transfers"],
            "bc_gate": {"passed": False, "gameplay_passed": False}, "source_admission_migrated": False,
            "eligible_for_selection": eligible, "epoch_cohort_kl_within_limit": eligible,
            "plan": str(output/"plan.json"), "reload_verification": str(proof_path),
            "teacher_reference": str(teacher_path), "teacher_reference_sha256": digest(teacher_path),
            "final_full_cohort": current_metrics}
        training.save_checkpoint(policy, candidate, version=output_version, metadata=epoch_metadata, optimizer=optimizer)
        expected_steps = {name: step+row["optimizer_steps"] for name, step in steps_before.items()}
        proof = verify_checkpoint(candidate, policy, optimizer, output_version, expected_steps)
        save_json(proof_path, proof)
        report = {**row, "path": str(candidate), "sha256": proof["weights_sha256"], "weight_version": output_version,
                  "eligible_for_selection": eligible, "metrics": current_metrics, "verification": str(proof_path)}
        epochs.append(report)
        with (output/"progress.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(report, allow_nan=False)+"\n")
        print(json.dumps({"phase": "epoch_complete", "epoch": row["epoch"], "optimizer_steps": row["optimizer_steps"],
                          "cohort_kl": current_metrics["approximate_kl"], "eligible_for_selection": eligible,
                          "head_conditional_kl": [head["conditional_kl"]["mean"] for head in current_metrics["heads"]],
                          "macro_argmax_flips": current_metrics["macro_argmax_flips"],
                          "macro_noop_to_production": current_metrics["macro_transitions"]["old_noop_to_production"],
                          "macro_production_to_noop": current_metrics["macro_transitions"]["old_production_to_noop"],
                          "reload_exact": True}), flush=True)
        policy.train()
        if not eligible:
            raise StopForKL()

    try:
        _, metrics = training.train_update(policy, batch, config, optimizer=optimizer,
                                            teacher_policy=teacher, progress_callback=completed_epoch)
    except StopForKL:
        stopped = True
        metrics = {"stopped_after_unsafe_epoch": True}
    verify_hashes(inputs)
    selected = select_safe(initial_selection, epochs)
    eligible = selected["epoch"] != 0
    result = {"complete": True, "epochs": epochs, "training": metrics, "stopped_for_kl": stopped,
              "selected": selected, "selection_rule": "last epoch within KL limit; initial weights if none",
              "selected_weights": selected["path"], "selected_weights_sha256": selected["sha256"],
              "selected_epoch": selected["epoch"] if eligible else None, "eligible": eligible,
              "iteration": config.iteration, "output_version": output_version,
              "native_python_initial_parity_passed": True, "checkpoint_reload_passed": True,
              "input_hashes_preserved": True, "elapsed_seconds": time.monotonic()-started,
              "games_launched": 0, "deployed": False, "admitted": False}
    save_json(output/"results.json", result)
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    result = run(args)
    print(json.dumps({key: result[key] for key in ("complete", "selected", "stopped_for_kl", "verified", "gradient_updates")
                      if key in result}, allow_nan=False), flush=True)


if __name__ == "__main__":
    main()
