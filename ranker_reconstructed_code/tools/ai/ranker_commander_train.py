"""Offline BC/PPO cohorts for the in-process JW2 commander (no policy server)."""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import math
from pathlib import Path
import random
import time

import numpy as np
import torch

from ranker_commander_rollout import (Episode, HEAD_SIZES, MAP_SHAPE, RECORD_DTYPE, RolloutError,
                                    SCHEMA_CRC, episode_returns, read_rollout, relabel_with_teacher)


@dataclass
class TrainConfig:
    mode: str = "ppo"
    iteration: int = 0
    epochs: int = 3
    minibatch: int = 2048
    schedule_iterations: int = 300
    shaping_scale: float = 1.0
    seed: int = 1
    # BC: extra weight on decisions whose macro head is not NOOP or that set
    # a squad order. Plain cross entropy reproduces the 70% NOOP majority and
    # barely learns the rare build/train/transfer actions that decide games.
    bc_rare_weight: float = 0.0
    # BC: per-decision weight (n_max / n_class)^power for the macro-head class,
    # clipped to [1, cap]. Held-out recall of the teacher's rarest macro
    # actions (dilophos, eggthrower, upnest, thrownest) stayed below 30% with
    # the flat rare weight alone.
    bc_class_power: float = 0.0
    bc_class_cap: float = 20.0
    # Macro classes kept at weight 1 under the class weighting. The squad
    # transfer classes had low teacher-forced precision even unweighted, and
    # boosting them made one BC policy shuffle units between squads hundreds
    # of times per game instead of sieging.
    bc_class_skip: tuple = ()
    # Opt-in BC objective: keep macro/squad balancing on their own heads and
    # average each head only over states in which it has a real choice.
    # The default retains the historical joint per-decision objective.
    bc_head_specific_weights: bool = False
    # PPO: KL(reference || policy) coefficient decays linearly from
    # teacher_kl_initial to teacher_kl_floor over teacher_kl_decay updates and
    # then holds the floor. The design's original schedule (0.05 -> 0 over 30
    # updates) let a warm-started policy drift from the teacher's production
    # habits within ~60 updates; a floor keeps the anchor for the whole run.
    teacher_kl_initial: float = 0.05
    teacher_kl_floor: float = 0.0
    teacher_kl_decay: int = 30
    # PPO: for the first N updates only value1/value2 train. Freeze the shared
    # actor features too, so value regression cannot move the BC policy.
    critic_warmup: int = 0
    learning_rate_initial: float = 3e-4
    learning_rate_final: float = 1e-4
    # Both discounts are per 32 simulation frames, including variable-duration
    # decisions. Keep the historical defaults for controlled one-variable runs.
    gamma: float = 0.997
    gae_lambda: float = 0.95

    def __post_init__(self):
        if not math.isfinite(self.gamma) or not 0 < self.gamma <= 1:
            raise ValueError("gamma must be finite and in (0, 1]")
        if not math.isfinite(self.gae_lambda) or not 0 <= self.gae_lambda <= 1:
            raise ValueError("gae_lambda must be finite and in [0, 1]")

    def teacher_coefficient(self) -> float:
        progress = max(0.0, 1.0 - self.iteration / max(1, self.teacher_kl_decay))
        return self.teacher_kl_floor + (self.teacher_kl_initial - self.teacher_kl_floor) * progress


def load_cohort(paths, *, version: int, teacher: bool = False):
    episodes, rejected = [], []
    for path in sorted(set(Path(item).resolve() for item in paths)):
        try:
            episodes.append(read_rollout(path, current_version=None if teacher else version,
                                         teacher=teacher))
        except (OSError, RolloutError) as error:
            rejected.append({"path": str(path), "reason": str(error)})
    return episodes, rejected


def load_dagger_cohort(paths):
    """Policy rollouts recorded under -AIDAGGER, relabeled with the teacher's
    decisions (DAgger). They join the teacher cohort for BC."""
    episodes, rejected = [], []
    for path in sorted(set(Path(item).resolve() for item in paths)):
        try:
            policy_episode = read_rollout(path, current_version=None, teacher=False)
            episodes.append(relabel_with_teacher(policy_episode))
            close_episode(policy_episode)
        except (OSError, RolloutError) as error:
            rejected.append({"path": str(path), "reason": str(error)})
    return episodes, rejected


def close_episode(episode):
    if hasattr(episode, "close"):
        episode.close()
    elif isinstance(episode.records, np.memmap):
        episode.records._mmap.close()


def discard_accepted(episodes):
    paths = [episode.path for episode in episodes]
    for episode in episodes:
        close_episode(episode)
    for path in paths:
        path.unlink()


def actor_trajectory_fingerprint(episode):
    """Hash the BC actor's inputs and labels, independent of seed/critic data."""
    records = episode.decisions
    digest = hashlib.sha256()
    digest.update(len(records).to_bytes(8, "little"))
    for name, dtype in (("vector", "<f4"), ("map", "u1"), ("mask", "u1"), ("action", "u1")):
        digest.update(name.encode("ascii"))
        for start in range(0, len(records), 2048):
            digest.update(np.asarray(records[start:start + 2048][name], dtype=dtype).tobytes())
    return digest.hexdigest()


def independent_teacher_holdout(training, candidates):
    """Keep the fitted training set fixed; exclude leaked validation seed groups.

    Different seeds or teacher variants can produce identical trajectories.
    Hashing only actor inputs/labels also detects these repeats when the
    model version, critic targets or privileged observations differ.
    """
    training_hashes = {actor_trajectory_fingerprint(episode) for episode in training} if candidates else set()
    excluded = {episode.seed for episode in candidates
                if actor_trajectory_fingerprint(episode) in training_hashes}
    held_out = [episode for episode in candidates if episode.seed not in excluded]
    return held_out, {"method": "seed_groups_excluding_repeated_actor_trajectories",
        "training_episodes": len(training), "training_seed_groups": sorted({e.seed for e in training}),
        "candidate_validation_episodes": len(candidates),
        "candidate_validation_seed_groups": sorted({e.seed for e in candidates}),
        "excluded_validation_seed_groups": sorted(excluded),
        "validation_episodes": len(held_out), "validation_seed_groups": sorted({e.seed for e in held_out})}


def split_teacher_episodes(episodes, *, seed=1, audit=None):
    """Hold out seed groups and remove exact actor trajectories seen in training."""
    seeds = sorted({episode.seed for episode in episodes})
    random.Random(seed).shuffle(seeds)
    held_seeds = set(seeds[:max(1, math.ceil(len(seeds) / 10))]) if len(seeds) >= 2 else set()
    training = [episode for episode in episodes if episode.seed not in held_seeds]
    candidates = [episode for episode in episodes if episode.seed in held_seeds]
    held_out, split_audit = independent_teacher_holdout(training, candidates)
    if audit is not None:
        audit.update(split_audit)
    return training, held_out


def assess_bc_accuracy(policy, episodes, *, minibatch=2048):
    """Teacher-forced accuracy on held-out episodes; never a gameplay pass."""
    matches = np.zeros(8, dtype=np.int64)
    action_labels = np.zeros(8, dtype=np.int64)
    action_matches = np.zeros(8, dtype=np.int64)
    false_actions = np.zeros(8, dtype=np.int64)
    decisions = 0
    policy.eval()
    with torch.no_grad():
        for episode in episodes:
            records = episode.decisions
            for start in range(0, len(records), minibatch):
                sample = records[start:start + minibatch]
                masks = torch.from_numpy(sample["mask"].copy()).bool()
                actions = torch.from_numpy(sample["action"].astype(np.int64))
                output = policy.evaluate(torch.from_numpy(sample["vector"].astype(np.float32)),
                    torch.from_numpy(sample["map"].copy()).float().reshape(-1, *MAP_SHAPE) / 255,
                    actions, masks, torch.from_numpy(sample["privileged"].astype(np.float32)))
                offset = 0
                for head, width in enumerate(HEAD_SIZES):
                    logits = output["logits"][:, offset:offset + width].masked_fill(~masks[:, offset:offset + width], -1e9)
                    prediction = logits.argmax(-1)
                    correct = prediction == actions[:, head]
                    matches[head] += int(correct.sum())
                    if head in (0, 2):  # Macro and squad selection use zero for NOOP.
                        active = actions[:, head] != 0
                        action_labels[head] += int(active.sum())
                        action_matches[head] += int((correct & active).sum())
                        false_actions[head] += int(((prediction != 0) & ~active).sum())
                    offset += width
                decisions += len(sample)
    accuracy = (matches / decisions).tolist() if decisions else [0.0] * 8
    passed = decisions > 0 and accuracy[0] >= 0.85 and accuracy[3] >= 0.80 and accuracy[4] >= 0.80
    return {"held_out": True, "episodes": len(episodes), "decisions": decisions,
            "seed_groups": sorted({episode.seed for episode in episodes}),
            "head_accuracy": accuracy, "H1_accuracy": accuracy[0],
            "H3_accuracy": accuracy[3], "H4_accuracy": accuracy[4],
            "macro_action_decisions": int(action_labels[0]),
            "macro_action_recall": float(action_matches[0] / action_labels[0]) if action_labels[0] else None,
            "macro_false_actions_on_noop": int(false_actions[0]),
            "squad_order_accuracy": accuracy[2],
            "squad_order_decisions": int(action_labels[2]),
            "squad_order_recall": float(action_matches[2] / action_labels[2]) if action_labels[2] else None,
            "squad_false_orders_on_noop": int(false_actions[2]),
            "accuracy_passed": passed, "gameplay_passed": False,
            "passed": False, "requires": "48 distinct teacher and BC gameplay evaluations"}


def ppo_admission(metadata, *, version, weights_sha256=None, no_bc_control=False, fresh=False,
                  warm_start=False):
    previous = metadata.get("bc_admission", {})
    if no_bc_control:
        if metadata.get("mode") == "bc" or previous.get("mode") == "bc_approved":
            raise ValueError("--no-bc-control cannot relabel a BC-trained policy; omit --policy for fresh initialization")
        return {"mode": "no_bc_control", "initialization": "fresh_random" if fresh else "explicit_checkpoint",
                "assessment_exempt": True}
    if previous.get("mode") in ("bc_approved", "bc_warm_start"):
        if not weights_sha256 or metadata.get("weights_sha256") != weights_sha256:
            raise ValueError("PPO checkpoint hash does not match its BC admission provenance")
        return previous
    gate = metadata.get("bc_gate", {})
    if warm_start:
        # Explicit warm start from an assessed BC checkpoint that passed one
        # of the two BC criteria but not both: held-out teacher-forced
        # accuracy, or the 48-case gameplay delta. (A class-weighted BC can
        # play within the gameplay margin while its argmax agrees with the
        # teacher on fewer than 85% of held-out decisions.) Recorded as such;
        # it never counts as a passed BC gate.
        accuracy_passed = bool(metadata.get("bc_validation", {}).get("accuracy_passed"))
        gameplay_passed = bool(gate.get("gameplay_passed")) and bool(gate.get("complete"))
        if not (gate.get("weight_version") == version and weights_sha256
                and gate.get("policy_sha256") == weights_sha256
                and (accuracy_passed or gameplay_passed)):
            raise ValueError("--bc-warm-start requires an assessed BC checkpoint with passing held-out accuracy or a passing 48-case gameplay delta")
        return {"mode": "bc_warm_start", "bc_weight_version": version,
                "bc_weights_sha256": weights_sha256, "gate": gate,
                "accuracy_passed": accuracy_passed, "gameplay_passed": gameplay_passed,
                "note": ("gameplay gate not passed; explicit warm start" if not gameplay_passed
                         else "held-out accuracy below threshold; explicit warm start on the gameplay delta")}
    if not (gate.get("passed") and gate.get("weight_version") == version and weights_sha256
            and gate.get("policy_sha256") == weights_sha256 and metadata.get("weights_sha256") == weights_sha256 and
            metadata.get("bc_validation", {}).get("accuracy_passed")):
        raise ValueError("PPO requires a passing BC held-out and 48-case gameplay gate; run eval bc-assess, or explicitly use --no-bc-control")
    return {"mode": "bc_approved", "bc_weight_version": version,
            "bc_weights_sha256": weights_sha256, "gate": gate}


def _discounted_sum(values, discounts):
    weights = np.concatenate(([1.0], np.cumprod(discounts[:-1])))
    return float(np.sum(weights * values))


class LazyBcField:
    def __init__(self, batch, name):
        self.batch, self.name = batch, name

    def __getitem__(self, indices):
        return self.batch.select(indices)[self.name]


class LazyBcBatch:
    """Decode only the sampled BC minibatch, retaining compact files as memmaps."""
    names = ("vector", "maps", "masks", "actions", "privileged", "old_logp", "advantage", "target")

    def __init__(self, episodes, targets, advantages, *, episode_weights=None):
        self.episodes = episodes
        self.episode_weights = None
        if episode_weights is not None:
            weights = np.asarray(episode_weights, dtype=np.float32)
            if (weights.shape != (len(episodes),) or not np.isfinite(weights).all()
                    or np.any(weights <= 0)):
                raise ValueError("BC episode weights must be finite, positive, and match the episodes")
            if not np.all(weights == 1):
                self.episode_weights = weights.copy()
                self.names = self.names + ("bc_weight",)
        self.ends = np.cumsum([len(episode.decisions) for episode in episodes])
        self.starts = np.r_[0, self.ends[:-1]]
        self.targets = torch.from_numpy(targets)
        self.advantages = torch.from_numpy(advantages)
        self.indices = None
        self.cache = None

    def __getitem__(self, name):
        return self.targets if name == "target" else LazyBcField(self, name)

    def items(self):
        return ((name, self[name]) for name in self.names)

    def select(self, indices):
        if indices is self.indices:
            return self.cache
        positions = indices.cpu().numpy()
        owners = np.searchsorted(self.ends, positions, side="right")
        records = np.empty(len(positions), dtype=RECORD_DTYPE)
        for owner in np.unique(owners):
            selection = np.flatnonzero(owners == owner)
            records[selection] = np.asarray(self.episodes[owner].decisions[
                positions[selection] - self.starts[owner]])
        self.cache = {
            "vector": torch.from_numpy(records["vector"].copy()),
            "maps": torch.from_numpy(records["map"].copy()).float().reshape(-1, *MAP_SHAPE) / 255,
            "masks": torch.from_numpy(records["mask"].copy()).bool(),
            "actions": torch.from_numpy(records["action"].astype(np.int64)),
            "privileged": torch.from_numpy(records["privileged"].copy()),
            "old_logp": torch.from_numpy(records["logp"].copy()).sum(1),
            "advantage": self.advantages[indices], "target": self.targets[indices]}
        if self.episode_weights is not None:
            self.cache["bc_weight"] = torch.from_numpy(self.episode_weights[owners].copy())
        self.indices = indices
        return self.cache


def build_batch(episodes: list[Episode], config: TrainConfig, *, bc_episode_weights=None):
    if not episodes:
        raise ValueError("no valid completed episodes in this cohort")
    if any(episode.records.dtype["vector"].shape != RECORD_DTYPE["vector"].shape or
           episode.records.dtype["map"].shape != RECORD_DTYPE["map"].shape for episode in episodes):
        raise ValueError("historical observations cannot train this schema; collect the new native observations")
    if bc_episode_weights is not None and config.mode != "bc":
        raise ValueError("BC episode weights are only supported for behavior cloning")
    targets = [episode_returns(episode, iteration=config.iteration,
                               shaping_scale=config.shaping_scale,
                               gamma=config.gamma, gae_lambda=config.gae_lambda) for episode in episodes]
    shape_mean = float(np.mean([abs(_discounted_sum(item["shape"].sum(axis=1),
                                                   item["discount"])) for item in targets]))
    terminal_mean = float(np.mean([abs(_discounted_sum(item["terminal"], item["discount"]))
                                  for item in targets]))
    # Apply one halving per iteration; persist the multiplier in checkpoint metadata.
    reduced = shape_mean > 2.0 * terminal_mean and shape_mean > 0.0
    if reduced:
        config.shaping_scale *= 0.5
        targets = [episode_returns(episode, iteration=config.iteration,
                                   shaping_scale=config.shaping_scale,
                                   gamma=config.gamma, gae_lambda=config.gae_lambda) for episode in episodes]
    # The signed PBRS return above telescopes even when individual steps have
    # a strong shaping signal. Measure step magnitudes on the final training
    # rewards separately; this diagnostic must not change the halving rule.
    step_shape_mean = float(np.mean([_discounted_sum(np.abs(item["shape"].sum(axis=1)),
                                                     item["discount"]) for item in targets]))
    advantages = np.concatenate([target["advantage"] for target in targets])
    advantages = (advantages - advantages.mean()) / max(float(advantages.std()), 1e-8)
    target_values = np.concatenate([
        item["mc_return" if config.mode == "bc" else "return"] for item in targets])
    if config.mode == "bc":
        batch = LazyBcBatch(episodes, target_values, advantages, episode_weights=bc_episode_weights)
        decision_count = len(target_values)
    else:
        records = np.concatenate([episode.decisions for episode in episodes])
        decision_count = len(records)
        batch = {
        "vector": torch.from_numpy(records["vector"].copy()),
        "maps": torch.from_numpy(records["map"].copy()).float().reshape(-1, *MAP_SHAPE) / 255.0,
        "masks": torch.from_numpy(records["mask"].copy()).bool(),
        "actions": torch.from_numpy(records["action"].astype(np.int64)),
        "privileged": torch.from_numpy(records["privileged"].copy()),
        "old_logp": torch.from_numpy(records["logp"].copy()).sum(dim=1),
        "advantage": torch.from_numpy(advantages),
        "target": torch.from_numpy(target_values),
        }
    decomposition = {
        "terminal": float(sum(item["terminal"].sum() for item in targets)),
        **{name: float(sum(item["shape"][:, column].sum() for item in targets))
           for column, name in enumerate(("shape_K", "shape_R", "shape_T", "shape_hq"))},
    }
    return batch, {"episodes": len(episodes), "decisions": decision_count,
                   "shaping_scale": config.shaping_scale, "shaping_halved": reduced,
                   "discounted_abs_shape_mean": shape_mean,
                   "discounted_abs_step_shape_mean": step_shape_mean,
                   "discounted_abs_terminal_mean": terminal_mean, **decomposition}


def head_specific_bc_loss(logps, actions, masks, *, episode_weights=None,
                          rare_weight=0.0, class_weights=None):
    """Sum independently normalized head losses over non-forced decisions.

    An uncommon macro must not change the target distribution for army
    orders, and an army order must not boost a macro NOOP. Conditional heads
    with only one legal action have no learning signal and no denominator
    contribution. Episode quality weights remain attached to every head.
    """
    if logps.shape != actions.shape or logps.ndim != 2 or logps.shape[1] != len(HEAD_SIZES):
        raise ValueError("head-specific BC requires one log-probability per action head")
    if masks.shape != (len(logps), sum(HEAD_SIZES)):
        raise ValueError("head-specific BC requires current per-head legal masks")
    base = logps.new_ones(len(logps)) if episode_weights is None else episode_weights
    losses = []
    offset = 0
    for head, width in enumerate(HEAD_SIZES):
        active = masks[:, offset:offset + width].sum(1) > 1
        weights = base * active.to(logps.dtype)
        if head in (0, 2) and rare_weight > 0:
            weights = weights * (1.0 + rare_weight * (actions[:, head] != 0).to(logps.dtype))
        if head == 0 and class_weights is not None:
            weights = weights * class_weights[actions[:, 0]]
        # A forced head contributes zero, even when the whole minibatch is forced.
        denominator = weights.sum()
        denominator = torch.where(denominator > 0, denominator, denominator.new_ones(()))
        losses.append(-(logps[:, head] * weights).sum() / denominator)
        offset += width
    return torch.stack(losses).sum()


def train_update(policy, batch, config: TrainConfig, *, optimizer=None, teacher_policy=None,
                 progress_callback=None):
    if config.mode not in ("bc", "ppo") or config.epochs < 1 or config.minibatch < 1:
        raise ValueError("invalid optimizer configuration")
    if not all(math.isfinite(rate) and rate > 0
               for rate in (config.learning_rate_initial, config.learning_rate_final)):
        raise ValueError("learning rates must be finite and positive")
    progress = min(1.0, max(0.0, config.iteration / max(1, config.schedule_iterations)))
    learning_rate = config.learning_rate_initial + (config.learning_rate_final - config.learning_rate_initial) * progress
    optimizer = optimizer or torch.optim.Adam(policy.parameters(), lr=learning_rate)
    for group in optimizer.param_groups:
        group["lr"] = learning_rate
    entropy_coefficients = torch.tensor([0.01 - 0.008 * progress] +
                                        [0.005 - 0.004 * progress] * 7)
    teacher_coefficient = config.teacher_coefficient() if config.mode == "ppo" else 0.0
    critic_only = config.mode == "ppo" and config.iteration < config.critic_warmup
    critic_parameters = set(policy.value1.parameters()) | set(policy.value2.parameters()) if critic_only else set()
    class_weights = None
    if config.mode == "bc" and config.bc_class_power > 0:
        if isinstance(batch, LazyBcBatch):
            counts = np.zeros(HEAD_SIZES[0], dtype=np.int64)
            for episode in batch.episodes:
                counts += np.bincount(episode.decisions["action"][:, 0], minlength=HEAD_SIZES[0])
            counts = torch.from_numpy(counts).float()
        else:
            counts = torch.bincount(batch["actions"][:, 0], minlength=HEAD_SIZES[0]).float()
        class_weights = (counts.max() / counts.clamp(min=1)).pow(config.bc_class_power)
        class_weights = class_weights.clamp(1.0, config.bc_class_cap)
        for index in config.bc_class_skip:
            class_weights[int(index)] = 1.0
    generator = torch.Generator().manual_seed(config.seed + config.iteration)
    policy.train()
    if teacher_policy is not None:
        teacher_policy.eval()
    reports = []
    for epoch in range(config.epochs):
        epoch_started = time.monotonic()
        first_report = len(reports)
        permutation = torch.randperm(len(batch["target"]), generator=generator)
        for indices in permutation.split(config.minibatch):
            sample = {key: value[indices] for key, value in batch.items()}
            output = policy.evaluate(sample["vector"], sample["maps"], sample["actions"],
                                     sample["masks"], sample["privileged"])
            logp = output["logp"].sum(dim=1)
            if config.mode == "bc":
                # Source replay weights affect imitation only; critic targets and
                # their regression remain on the original sampled state distribution.
                if config.bc_head_specific_weights:
                    actor_loss = head_specific_bc_loss(output["logp"], sample["actions"],
                        sample["masks"], episode_weights=sample.get("bc_weight"),
                        rare_weight=config.bc_rare_weight, class_weights=class_weights)
                else:
                    weights = sample.get("bc_weight", torch.ones(len(logp)))
                    if config.bc_rare_weight > 0:
                        important = (sample["actions"][:, 0] != 0) | (sample["actions"][:, 2] != 0)
                        weights = weights * (1.0 + config.bc_rare_weight * important.float())
                    if config.bc_class_power > 0:
                        weights = weights * class_weights[sample["actions"][:, 0]]
                    if config.bc_rare_weight > 0 or config.bc_class_power > 0 or "bc_weight" in sample:
                        actor_loss = -(logp * weights).sum() / weights.sum()
                    else:
                        actor_loss = -logp.mean()
                clipped = torch.zeros(())
                approximate_kl = torch.zeros(())
            else:
                log_ratio = logp - sample["old_logp"]
                ratio = log_ratio.exp()
                if not torch.isfinite(ratio).all():
                    raise ValueError("nonfinite PPO ratio; cohort/policy mismatch")
                advantage = sample["advantage"]
                actor_loss = -torch.minimum(ratio * advantage,
                                           ratio.clamp(0.8, 1.2) * advantage).mean()
                clipped = ((ratio - 1).abs() > 0.2).float().mean()
                approximate_kl = ((ratio - 1) - log_ratio).mean()
            value_loss = 0.5 * (output["value"].reshape(-1) - sample["target"]).square().mean()
            entropy_bonus = ((output["entropy"] * entropy_coefficients).sum(dim=1).mean()
                             if config.mode == "ppo" else torch.zeros(()))
            teacher_kl = torch.zeros(())
            if config.mode == "ppo" and teacher_policy is not None and teacher_coefficient:
                with torch.no_grad():
                    reference = teacher_policy.evaluate(sample["vector"], sample["maps"],
                        sample["actions"], sample["masks"], sample["privileged"])
                offset = 0
                for width in HEAD_SIZES:
                    legal = sample["masks"][:, offset:offset + width]
                    current_logits = output["logits"][:, offset:offset + width].masked_fill(~legal, -1e9)
                    teacher_logits = reference["logits"][:, offset:offset + width].masked_fill(~legal, -1e9)
                    current_log = current_logits.log_softmax(-1)
                    teacher_log = teacher_logits.log_softmax(-1)
                    teacher_kl = teacher_kl + (teacher_log.exp() * (teacher_log - current_log)).sum(-1).mean()
                    offset += width
            if critic_only:
                loss = value_loss + teacher_coefficient * teacher_kl
            else:
                loss = actor_loss + value_loss - entropy_bonus + teacher_coefficient * teacher_kl
            if not torch.isfinite(loss):
                raise ValueError("nonfinite commander training loss")
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            if critic_only:
                # The value head reads the actor's shared trunk. Dropping the
                # policy loss alone still changes its features and logits.
                # None also prevents existing Adam momentum from moving the
                # frozen parameters; zero gradients would not do that.
                for parameter in policy.parameters():
                    if parameter not in critic_parameters:
                        parameter.grad = None
            gradient_norm = torch.nn.utils.clip_grad_norm_(policy.parameters(), 1.0,
                                                           error_if_nonfinite=True)
            optimizer.step()
            reports.append({"loss": float(loss.detach()), "actor_loss": float(actor_loss.detach()),
                "value_loss": float(value_loss.detach()), "entropy_bonus": float(entropy_bonus.detach()),
                "teacher_kl": float(teacher_kl.detach()), "clip_fraction": float(clipped.detach()),
                "approximate_kl": float(approximate_kl.detach()), "gradient_norm": float(gradient_norm)})
        if progress_callback is not None:
            recent = reports[first_report:]
            elapsed = time.monotonic() - epoch_started
            progress_callback({"mode": config.mode, "iteration": config.iteration,
                "epoch": epoch + 1, "epochs": config.epochs, "optimizer_steps": len(reports),
                "wall_seconds": elapsed, "decisions_per_second": len(batch["target"]) / max(elapsed, 1e-9),
                **{key: float(np.mean([row[key] for row in recent])) for key in recent[0]}})
    metrics = {key: float(np.mean([item[key] for item in reports])) for key in reports[0]}
    metrics.update(lr=learning_rate, optimizer_steps=len(reports),
                   teacher_kl_coefficient=teacher_coefficient if teacher_policy is not None else 0.0,
                   critic_only=bool(critic_only))
    policy.eval()
    return optimizer, metrics


def save_optimizer(optimizer, path: str | Path, *, version: int):
    """Save numeric Adam state without executable pickle objects."""
    state = optimizer.state_dict()
    arrays = {"schema_crc": np.array(SCHEMA_CRC, dtype=np.uint32),
              "weight_version": np.array(version, dtype=np.uint32)}
    for parameter, slots in state["state"].items():
        for name, value in slots.items():
            if name not in ("step", "exp_avg", "exp_avg_sq", "max_exp_avg_sq"):
                raise ValueError(f"unsupported Adam state {name}")
            arrays[f"p{parameter}_{name}"] = value.detach().cpu().numpy()
    arrays["metadata"] = np.frombuffer(json.dumps({"param_groups": state["param_groups"],
        "state": {str(parameter): list(slots) for parameter, slots in state["state"].items()}}).encode("utf-8"), dtype=np.uint8)
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as stream:
        np.savez(stream, **arrays)
    temporary.replace(path)


def load_optimizer(optimizer, path: str | Path, *, version: int):
    with np.load(path, allow_pickle=False) as data:
        if int(data["schema_crc"]) != SCHEMA_CRC or int(data["weight_version"]) != version:
            raise ValueError("optimizer schema/version does not match commander weights")
        metadata = json.loads(data["metadata"].tobytes().decode("utf-8"))
        actual = optimizer.state_dict()
        if [group["params"] for group in actual["param_groups"]] != [group["params"] for group in metadata["param_groups"]]:
            raise ValueError("optimizer parameter layout mismatch")
        parameters = {index: parameter for group, stored in zip(optimizer.param_groups, actual["param_groups"])
                      for index, parameter in zip(stored["params"], group["params"])}
        state = {}
        for parameter_text, names in metadata["state"].items():
            parameter = int(parameter_text)
            if parameter not in parameters:
                raise ValueError("unknown optimizer parameter")
            slots = {}
            for name in names:
                if name not in ("step", "exp_avg", "exp_avg_sq", "max_exp_avg_sq"):
                    raise ValueError("unknown Adam state slot")
                values = data[f"p{parameter}_{name}"]
                expected = () if name == "step" else tuple(parameters[parameter].shape)
                if values.shape != expected or not np.isfinite(values).all():
                    raise ValueError("optimizer state shape/value mismatch")
                slots[name] = torch.from_numpy(values.copy())
            state[parameter] = slots
        optimizer.load_state_dict({"state": state, "param_groups": metadata["param_groups"]})
    return optimizer


def save_checkpoint(policy, path: str | Path, *, version: int, metadata: dict, optimizer=None):
    """Versioned binary publication is atomic; legacy policy formats are rejected."""
    from ranker_commander_model import ARCHITECTURE, export_weights
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    export_weights(policy, temporary, version)
    temporary.replace(path)
    sidecar = path.with_suffix(path.suffix + ".json")
    temporary_json = sidecar.with_name(sidecar.name + ".tmp")
    temporary_json.write_text(json.dumps({**metadata, "schema_crc": SCHEMA_CRC,
        "architecture": ARCHITECTURE, "weight_version": version,
        "weights_sha256": hashlib.sha256(path.read_bytes()).hexdigest()}, indent=2) + "\n", encoding="utf-8")
    temporary_json.replace(sidecar)
    if optimizer is not None:
        save_optimizer(optimizer, path.with_suffix(path.suffix + ".optimizer.npz"), version=version)
    policy.weight_version = version


def collection_jobs(*, mode, seed, iteration, count, curriculum,
                    teacher_variants=0, variant_opponents=0.0):
    from ranker_commander_eval import curriculum_settings
    settings = curriculum_settings(curriculum)

    def variant_opponent(game):
        if mode != "ppo" or variant_opponents <= 0:
            return {}
        # Rotate the slot in each block so variant opponents do not remove
        # all built-in games of a particular tribe from the PPO cohort.
        period = max(1, int(round(1.0 / variant_opponents)))
        if game % period != (game // period) % period:
            return {}
        return {"teacher2": True, "tribe": 2,
                "teacher_variant2": 1 + (seed + iteration * count + game) % 15}

    return [{"seed": seed + iteration * count + game,
             "tribe": 2 if curriculum == 0 else game % 4,
             # Advance the teacher only after all four opponent tribes. With
             # game % 16, variant v previously faced only tribe v % 4.
             **({"teacher_variant": (game // 4) % (teacher_variants + 1)}
                if mode == "bc" and teacher_variants else {}),
             **settings, **variant_opponent(game)} for game in range(count)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("bc", "ppo"))
    parser.add_argument("--rollouts", nargs="*", default=[], help="RLO1 files or directories")
    parser.add_argument("--dagger-rollouts", nargs="*", default=[],
                        help="BC: policy rollouts recorded under -AIDAGGER; their teacher labels replace the sampled actions")
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--teacher-policy", type=Path, help="frozen BC reference for first 30 PPO updates")
    parser.add_argument("--no-bc-control", action="store_true", help="explicit no-BC control; omit --policy for fresh random initialization")
    parser.add_argument("--bc-warm-start", action="store_true",
                        help="explicit PPO warm start from an assessed BC checkpoint that passed held-out accuracy but not the gameplay gate")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--install-dir", type=Path, help="optionally collect fresh in-process game cohorts")
    parser.add_argument("--exe", type=Path, help="built ranker_rebuild.exe; assets from --install-dir")
    parser.add_argument("--keep-sleep", action="store_true", help="keep the engine's per-frame Sleep(1); default passes -AINOSLEEP")
    parser.add_argument("--io", type=Path, default=Path("commander_training_io"))
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--iteration", type=int, help="override iteration resumed from matching PPO metadata")
    parser.add_argument("--games-per-cohort", type=int, default=12)
    parser.add_argument("--teacher-games", type=int, default=400)
    parser.add_argument("--teacher-variants", type=int, default=0,
                        help="BC collection: cross rule-commander variants 0..N with all four opponent tribes")
    parser.add_argument("--variant-opponents", type=float, default=0.0,
                        help="PPO cohorts: fraction of games played against a rule-commander variant (-AIVS -AITEACHER2)")
    parser.add_argument("--bc-rare-weight", type=float, default=0.0,
                        help="BC: extra loss weight for non-NOOP macro / squad-order decisions (0 = plain cross entropy)")
    parser.add_argument("--bc-class-power", type=float, default=0.0,
                        help="BC: macro-class weight (n_max/n_class)^power, clipped to [1, --bc-class-cap] (0 = off)")
    parser.add_argument("--bc-class-cap", type=float, default=20.0)
    parser.add_argument("--bc-class-skip", type=str, default="",
                        help="BC: comma-separated macro indices kept at weight 1 under --bc-class-power")
    parser.add_argument("--bc-head-specific-weights", action="store_true",
                        help="BC: balance macro/squad heads separately and exclude forced heads from each mean")
    parser.add_argument("--teacher-kl-initial", type=float,
                        help="PPO: KL(BC reference || policy) coefficient at update 0")
    parser.add_argument("--teacher-kl-floor", type=float,
                        help="PPO: coefficient held after the decay (0 = anchor released, the original schedule)")
    parser.add_argument("--teacher-kl-decay", type=int,
                        help="PPO: updates over which the coefficient decays from initial to floor")
    parser.add_argument("--critic-warmup", type=int,
                        help="PPO: first N updates train only the value head; actor and shared features stay fixed")
    parser.add_argument("--learning-rate-initial", type=float,
                        help="initial learning rate; defaults to saved PPO schedule or 3e-4")
    parser.add_argument("--learning-rate-final", type=float,
                        help="final learning rate; defaults to saved PPO schedule or 1e-4")
    parser.add_argument("--gamma", type=float,
                        help="discount per 32 frames; defaults to saved PPO setting or 0.997")
    parser.add_argument("--gae-lambda", type=float,
                        help="GAE trace discount per 32 frames; defaults to saved PPO setting or 0.95")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--curriculum", type=int, choices=range(4), help="BC defaults to normal-speed four-tribe C2; PPO defaults to C0")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--minibatch", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--keep-rollouts", action="store_true", help="retain accepted training RLOs after publication; default deletes them")
    parser.add_argument("--discard-rollouts", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if min(args.iterations, args.games_per_cohort, args.teacher_games, args.workers,
           args.epochs, args.minibatch, args.threads, args.seed) < 1:
        parser.error("counts, threads, seed, epochs and minibatch must be positive")
    if args.iterations > 1 and args.install_dir is None:
        parser.error("multiple PPO iterations require fresh collection, not replay of one cohort")
    if args.mode == "bc" and args.iterations != 1:
        parser.error("BC collects one teacher dataset; use --epochs for repeated optimization")
    if args.mode == "ppo" and args.policy is None and not args.no_bc_control:
        parser.error("PPO requires a pinned --policy (export a fresh model for the no-BC control)")
    if args.mode == "bc" and args.install_dir and args.teacher_games < 400:
        parser.error("design BC collection requires at least 400 complete teacher games")
    if args.keep_rollouts and args.discard_rollouts:
        parser.error("--keep-rollouts and --discard-rollouts conflict")
    if args.dagger_rollouts and args.mode != "bc":
        parser.error("--dagger-rollouts applies to BC only")
    if not args.rollouts and not args.dagger_rollouts and args.install_dir is None:
        parser.error("provide --rollouts or --install-dir")
    from ranker_commander_model import CommanderPolicy, load_weights
    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.set_num_threads(args.threads)
    policy = load_weights(args.policy) if args.policy else CommanderPolicy()
    teacher_policy = load_weights(args.teacher_policy) if args.teacher_policy else None
    version = getattr(policy, "weight_version", 0)
    shaping_scale = 1.0
    metadata = {}
    start_iteration = args.iteration if args.iteration is not None else 0
    optimizer = None
    if args.policy and args.policy.with_suffix(args.policy.suffix + ".json").exists():
        metadata = json.loads(args.policy.with_suffix(args.policy.suffix + ".json").read_text(encoding="utf-8"))
        if metadata.get("schema_crc") != SCHEMA_CRC:
            parser.error("checkpoint metadata schema mismatch")
        shaping_scale = float(metadata.get("shaping_scale", 1.0))
        if args.mode == "ppo" and metadata.get("mode") == "ppo":
            if args.iteration is None:
                start_iteration = int(metadata.get("iteration", -1)) + 1
            optimizer_path = args.policy.with_suffix(args.policy.suffix + ".optimizer.npz")
            if optimizer_path.exists():
                optimizer = load_optimizer(torch.optim.Adam(policy.parameters(), lr=3e-4),
                                           optimizer_path, version=version)
    if start_iteration < 0:
        parser.error("iteration must be nonnegative")
    saved_config = (metadata.get("training_config", {})
                    if args.mode == "ppo" and metadata.get("mode") == "ppo" else {})
    # Omitted resume flags inherit the saved PPO configuration; explicit zero
    # remains an override. Fresh PPO and BC warm starts retain their defaults.
    if args.curriculum is None:
        args.curriculum = (metadata.get("curriculum", 0)
                           if args.mode == "ppo" and metadata.get("mode") == "ppo"
                           else 2 if args.mode == "bc" else 0)
    if args.curriculum not in range(4):
        parser.error("saved curriculum must be C0..C3")
    defaults = TrainConfig()
    for name in ("teacher_kl_initial", "teacher_kl_floor", "teacher_kl_decay", "critic_warmup",
                 "gamma", "gae_lambda"):
        if getattr(args, name) is None:
            setattr(args, name, saved_config.get(name, getattr(defaults, name)))
    try:
        args.gamma, args.gae_lambda = float(args.gamma), float(args.gae_lambda)
        TrainConfig(gamma=args.gamma, gae_lambda=args.gae_lambda)
    except (ValueError, TypeError) as error:
        parser.error(str(error))
    for name, default in (("learning_rate_initial", 3e-4), ("learning_rate_final", 1e-4)):
        rate = getattr(args, name)
        if rate is None:
            rate = float(saved_config.get(name, default))
        if not math.isfinite(rate) or rate <= 0:
            parser.error("learning rates must be finite and positive")
        setattr(args, name, rate)
    admission = ppo_admission(metadata, version=version, no_bc_control=args.no_bc_control,
                              fresh=args.policy is None, warm_start=args.bc_warm_start,
                              weights_sha256=hashlib.sha256(args.policy.read_bytes()).hexdigest() if args.policy else None
                              ) if args.mode == "ppo" else None
    reference = str(args.teacher_policy.resolve()) if args.teacher_policy else metadata.get("bc_reference")
    if args.mode == "ppo" and teacher_policy is None and admission["mode"] in ("bc_approved", "bc_warm_start"):
        reference = metadata.get("bc_reference") or (str(args.policy) if metadata.get("mode") == "bc" else None)
        if reference and Path(reference).is_file():
            teacher_policy = load_weights(reference)
        elif start_iteration < args.teacher_kl_decay or args.teacher_kl_floor > 0:
            parser.error("BC-initialized PPO updates under the KL anchor require --teacher-policy or the saved BC reference")
    if (args.mode == "ppo" and metadata.get("mode") == "bc" and args.policy
            and args.out.resolve() == args.policy.resolve()):
        # Preserve the teacher before replacing an in-place BC checkpoint.
        frozen_reference = args.out.with_name(args.out.stem + ".bc_reference.bin")
        save_checkpoint(teacher_policy or policy, frozen_reference, version=version,
                        metadata={"mode": "bc_reference", "source_weight_version": version})
        reference = str(frozen_reference.resolve())
    for iteration in range(start_iteration, start_iteration + args.iterations):
        config = TrainConfig(args.mode, iteration, args.epochs, args.minibatch,
                             shaping_scale=shaping_scale, seed=args.seed,
                             bc_rare_weight=args.bc_rare_weight,
                             bc_class_power=args.bc_class_power, bc_class_cap=args.bc_class_cap,
                             bc_class_skip=tuple(int(x) for x in args.bc_class_skip.split(",") if x.strip()),
                             bc_head_specific_weights=args.bc_head_specific_weights,
                             teacher_kl_initial=args.teacher_kl_initial,
                             teacher_kl_floor=args.teacher_kl_floor,
                             teacher_kl_decay=args.teacher_kl_decay,
                             critic_warmup=args.critic_warmup,
                             learning_rate_initial=args.learning_rate_initial,
                             learning_rate_final=args.learning_rate_final,
                             gamma=args.gamma, gae_lambda=args.gae_lambda)
        paths = []
        collected_paths = []
        if iteration == start_iteration:
            for value in args.rollouts:
                item = Path(value)
                paths.extend(item.rglob("*.rlo") if item.is_dir() else [item])
        if args.install_dir:
            from ranker_commander_eval import run_games
            cohort_dir = args.io.resolve() / f"cohort_{iteration:05d}"
            snapshot = cohort_dir / f"weights_{version:05d}.bin"
            save_checkpoint(policy, snapshot, version=version, metadata={"shaping_scale": shaping_scale})
            count = args.teacher_games if args.mode == "bc" else args.games_per_cohort
            jobs = collection_jobs(mode=args.mode, seed=args.seed, iteration=iteration,
                count=count, curriculum=args.curriculum, teacher_variants=args.teacher_variants,
                variant_opponents=args.variant_opponents)
            print(json.dumps({"collection_started": {"mode": args.mode, "iteration": iteration,
                "games": count, "workers": args.workers, "directory": str(cohort_dir)}}), flush=True)
            reports = run_games(args.install_dir, snapshot, cohort_dir / "games", jobs,
                                 workers=args.workers, teacher=args.mode == "bc",
                                 executable=args.exe, no_sleep=not args.keep_sleep)
            # A game that never finished (process timeout under host load, a
            # crash) is replayed once; the engine is deterministic, so a retry
            # is the same game, not a different sample.
            retry = [job for job, report in zip(jobs, reports) if not report.get("valid")]
            if retry:
                print(json.dumps({"cohort_retry": [r.get("reason") for r in reports if not r.get("valid")]}), flush=True)
                again = run_games(args.install_dir, snapshot, cohort_dir / "games_retry", retry,
                                  workers=args.workers, teacher=args.mode == "bc",
                                  executable=args.exe, no_sleep=not args.keep_sleep)
                replacements = iter(again)
                reports = [report if report.get("valid") else next(replacements) for report in reports]
            if args.mode == "bc" and sum(bool(report["valid"]) for report in reports) != count:
                raise RuntimeError("BC requires every requested teacher game to complete successfully")
            collected_paths = [Path(report["rollout"]) for report in reports if report["valid"]]
            paths.extend(collected_paths)
        episodes, rejected = load_cohort(paths, version=version, teacher=args.mode == "bc")
        if args.mode == "bc" and args.dagger_rollouts and iteration == start_iteration:
            dagger_paths = []
            for value in args.dagger_rollouts:
                item = Path(value)
                dagger_paths.extend(p for p in (item.rglob("*.rlo") if item.is_dir() else [item])
                                    if not str(p).endswith(".owner2.rlo"))
            dagger_episodes, dagger_rejected = load_dagger_cohort(dagger_paths)
            print(json.dumps({"dagger_episodes": len(dagger_episodes), "dagger_rejected": dagger_rejected}), flush=True)
            episodes.extend(dagger_episodes)
            rejected.extend(dagger_rejected)
        if args.install_dir:
            expected = args.teacher_games if args.mode == "bc" else args.games_per_cohort
            minimum = expected if args.mode == "bc" else math.ceil(expected * 10 / 12)
            if len(episodes) < minimum:
                raise RuntimeError(f"incomplete cohort: {len(episodes)}/{expected} valid games; {rejected}")
        split_audit = {}
        training_episodes, validation_episodes = (split_teacher_episodes(episodes, seed=args.seed, audit=split_audit)
            if args.mode == "bc" else (episodes, []))
        batch, reward_metrics = build_batch(training_episodes, config)
        optimizer, metrics = train_update(policy, batch, config, optimizer=optimizer,
            teacher_policy=teacher_policy,
            progress_callback=lambda row: print(json.dumps({"training_progress": row}), flush=True))
        shaping_scale = config.shaping_scale
        version += 1
        metadata = {"mode": args.mode, "iteration": iteration, "curriculum": args.curriculum,
                    "shaping_scale": shaping_scale,
                    "training_config": asdict(config), "threads": args.threads,
                    "collection_config": {"workers": args.workers,
                        "teacher_variants": args.teacher_variants,
                        "teacher_schedule": "four_tribes_per_variant",
                        "variant_opponents": args.variant_opponents},
                    "rejected": rejected, **reward_metrics, **metrics}
        if args.mode == "bc":
            metadata["bc_validation"] = assess_bc_accuracy(policy, validation_episodes, minibatch=args.minibatch)
            metadata["bc_validation"]["split_audit"] = split_audit
            metadata["bc_validation"]["teacher_games_total"] = len(episodes)
            metadata["teacher_games_total"] = len(episodes)
        else:
            metadata["bc_admission"] = admission
            if admission["mode"] in ("bc_approved", "bc_warm_start") and reference:
                metadata["bc_reference"] = str(Path(reference).resolve())
        save_checkpoint(policy, args.out, version=version, metadata=metadata, optimizer=optimizer)
        if (iteration + 1) % 20 == 0:
            frozen = args.out.parent / "pool" / f"commander_{version:05d}.bin"
            if frozen.exists():
                raise FileExistsError(f"frozen league checkpoint already exists: {frozen}")
            save_checkpoint(policy, frozen, version=version, metadata=metadata)
        print(json.dumps({"version": version, **metadata}), flush=True)
        # Only rollouts this run collected itself are ever deleted. Inputs
        # named by --rollouts / --dagger-rollouts are the caller's data (a BC
        # run without --keep-rollouts once erased the 400-game teacher set).
        collected = {Path(item).resolve() for item in collected_paths}
        for episode in episodes:
            if not args.keep_rollouts and Path(episode.path).resolve() in collected:
                discard_accepted([episode])
            else:
                close_episode(episode)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
