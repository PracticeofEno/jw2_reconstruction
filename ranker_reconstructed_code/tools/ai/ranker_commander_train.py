"""Offline BC/PPO cohorts for the in-process JW2 commander (no policy server)."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import random

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
    # PPO: KL(reference || policy) coefficient decays linearly from
    # teacher_kl_initial to teacher_kl_floor over teacher_kl_decay updates and
    # then holds the floor. The design's original schedule (0.05 -> 0 over 30
    # updates) let a warm-started policy drift from the teacher's production
    # habits within ~60 updates; a floor keeps the anchor for the whole run.
    teacher_kl_initial: float = 0.05
    teacher_kl_floor: float = 0.0
    teacher_kl_decay: int = 30
    # PPO: for the first N updates only the critic (and the KL anchor) train,
    # so early noisy advantages cannot move the warm-started actor.
    critic_warmup: int = 0

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


def split_teacher_episodes(episodes, *, seed=1):
    """Hold out complete policy-seed groups so related states cannot leak."""
    seeds = sorted({episode.seed for episode in episodes})
    if len(seeds) < 2:
        return list(episodes), []
    random.Random(seed).shuffle(seeds)
    held_seeds = set(seeds[:max(1, math.ceil(len(seeds) / 10))])
    return ([episode for episode in episodes if episode.seed not in held_seeds],
            [episode for episode in episodes if episode.seed in held_seeds])


def assess_bc_accuracy(policy, episodes, *, minibatch=2048):
    """Teacher-forced accuracy on held-out episodes; never a gameplay pass."""
    matches = np.zeros(8, dtype=np.int64)
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
                    matches[head] += int((logits.argmax(-1) == actions[:, head]).sum())
                    offset += width
                decisions += len(sample)
    accuracy = (matches / decisions).tolist() if decisions else [0.0] * 8
    passed = decisions > 0 and accuracy[0] >= 0.85 and accuracy[3] >= 0.80 and accuracy[4] >= 0.80
    return {"held_out": True, "episodes": len(episodes), "decisions": decisions,
            "seed_groups": sorted({episode.seed for episode in episodes}),
            "head_accuracy": accuracy, "H1_accuracy": accuracy[0],
            "H3_accuracy": accuracy[3], "H4_accuracy": accuracy[4],
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

    def __init__(self, episodes, targets, advantages):
        self.episodes = episodes
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
        self.indices = indices
        return self.cache


def build_batch(episodes: list[Episode], config: TrainConfig):
    if not episodes:
        raise ValueError("no valid completed episodes in this cohort")
    targets = [episode_returns(episode, iteration=config.iteration,
                               shaping_scale=config.shaping_scale) for episode in episodes]
    shape_mean = float(np.mean([abs(_discounted_sum(item["shape"].sum(axis=1),
                                                   item["discount"])) for item in targets]))
    terminal_mean = float(np.mean([abs(_discounted_sum(item["terminal"], item["discount"]))
                                  for item in targets]))
    # Apply one halving per iteration; persist the multiplier in checkpoint metadata.
    reduced = shape_mean > 2.0 * terminal_mean and shape_mean > 0.0
    if reduced:
        config.shaping_scale *= 0.5
        targets = [episode_returns(episode, iteration=config.iteration,
                                   shaping_scale=config.shaping_scale) for episode in episodes]
    advantages = np.concatenate([target["advantage"] for target in targets])
    advantages = (advantages - advantages.mean()) / max(float(advantages.std()), 1e-8)
    target_values = np.concatenate([
        item["mc_return" if config.mode == "bc" else "return"] for item in targets])
    if config.mode == "bc":
        batch = LazyBcBatch(episodes, target_values, advantages)
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
                   "discounted_abs_terminal_mean": terminal_mean, **decomposition}


def train_update(policy, batch, config: TrainConfig, *, optimizer=None, teacher_policy=None):
    if config.mode not in ("bc", "ppo") or config.epochs < 1 or config.minibatch < 1:
        raise ValueError("invalid optimizer configuration")
    progress = min(1.0, max(0.0, config.iteration / max(1, config.schedule_iterations)))
    learning_rate = 3e-4 + (1e-4 - 3e-4) * progress
    optimizer = optimizer or torch.optim.Adam(policy.parameters(), lr=learning_rate)
    for group in optimizer.param_groups:
        group["lr"] = learning_rate
    entropy_coefficients = torch.tensor([0.01 - 0.008 * progress] +
                                        [0.005 - 0.004 * progress] * 7)
    teacher_coefficient = config.teacher_coefficient() if config.mode == "ppo" else 0.0
    critic_only = config.mode == "ppo" and config.iteration < config.critic_warmup
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
    for _ in range(config.epochs):
        permutation = torch.randperm(len(batch["target"]), generator=generator)
        for indices in permutation.split(config.minibatch):
            sample = {key: value[indices] for key, value in batch.items()}
            output = policy.evaluate(sample["vector"], sample["maps"], sample["actions"],
                                     sample["masks"], sample["privileged"])
            logp = output["logp"].sum(dim=1)
            if config.mode == "bc":
                weights = torch.ones(len(logp))
                if config.bc_rare_weight > 0:
                    important = (sample["actions"][:, 0] != 0) | (sample["actions"][:, 2] != 0)
                    weights = weights * (1.0 + config.bc_rare_weight * important.float())
                if config.bc_class_power > 0:
                    weights = weights * class_weights[sample["actions"][:, 0]]
                if config.bc_rare_weight > 0 or config.bc_class_power > 0:
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
            gradient_norm = torch.nn.utils.clip_grad_norm_(policy.parameters(), 1.0,
                                                           error_if_nonfinite=True)
            optimizer.step()
            reports.append({"loss": float(loss.detach()), "actor_loss": float(actor_loss.detach()),
                "value_loss": float(value_loss.detach()), "entropy_bonus": float(entropy_bonus.detach()),
                "teacher_kl": float(teacher_kl.detach()), "clip_fraction": float(clipped.detach()),
                "approximate_kl": float(approximate_kl.detach()), "gradient_norm": float(gradient_norm)})
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
                        help="BC collection: cycle rule-commander variants 0..N (0 = default teacher only)")
    parser.add_argument("--variant-opponents", type=float, default=0.0,
                        help="PPO cohorts: fraction of games played against a rule-commander variant (-AIVS -AITEACHER2)")
    parser.add_argument("--bc-rare-weight", type=float, default=0.0,
                        help="BC: extra loss weight for non-NOOP macro / squad-order decisions (0 = plain cross entropy)")
    parser.add_argument("--bc-class-power", type=float, default=0.0,
                        help="BC: macro-class weight (n_max/n_class)^power, clipped to [1, --bc-class-cap] (0 = off)")
    parser.add_argument("--bc-class-cap", type=float, default=20.0)
    parser.add_argument("--bc-class-skip", type=str, default="",
                        help="BC: comma-separated macro indices kept at weight 1 under --bc-class-power")
    parser.add_argument("--teacher-kl-initial", type=float, default=0.05,
                        help="PPO: KL(BC reference || policy) coefficient at update 0")
    parser.add_argument("--teacher-kl-floor", type=float, default=0.0,
                        help="PPO: coefficient held after the decay (0 = anchor released, the original schedule)")
    parser.add_argument("--teacher-kl-decay", type=int, default=30,
                        help="PPO: updates over which the coefficient decays from initial to floor")
    parser.add_argument("--critic-warmup", type=int, default=0,
                        help="PPO: first N updates train only the value head (plus the KL anchor)")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--curriculum", type=int, choices=range(4), help="BC defaults to normal-speed four-tribe C2; PPO defaults to C0")
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--minibatch", type=int, default=2048)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--keep-rollouts", action="store_true", help="retain accepted training RLOs after publication; default deletes them")
    parser.add_argument("--discard-rollouts", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.curriculum is None:
        args.curriculum = 2 if args.mode == "bc" else 0
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
                             teacher_kl_initial=args.teacher_kl_initial,
                             teacher_kl_floor=args.teacher_kl_floor,
                             teacher_kl_decay=args.teacher_kl_decay,
                             critic_warmup=args.critic_warmup)
        paths = []
        collected_paths = []
        if iteration == start_iteration:
            for value in args.rollouts:
                item = Path(value)
                paths.extend(item.rglob("*.rlo") if item.is_dir() else [item])
        if args.install_dir:
            from ranker_commander_eval import curriculum_settings, run_games
            cohort_dir = args.io.resolve() / f"cohort_{iteration:05d}"
            snapshot = cohort_dir / f"weights_{version:05d}.bin"
            save_checkpoint(policy, snapshot, version=version, metadata={"shaping_scale": shaping_scale})
            settings = curriculum_settings(args.curriculum)
            count = args.teacher_games if args.mode == "bc" else args.games_per_cohort
            # The engine and the teacher are deterministic, so a (start pair,
            # tribe) condition replays identically under any seed. Teacher
            # variants (design 8.3) give BC distinct trajectories: game g uses
            # variant g % (N + 1), where 0 is the default teacher.
            # PPO cohorts: a fraction of the games (design C1 40%, C2 20%) put
            # a rule-commander variant on the second owner (-AIVS -AITEACHER2)
            # instead of the built-in opponent; the policy still owns slot 1.
            def variant_opponent(game):
                if args.mode != "ppo" or args.variant_opponents <= 0:
                    return {}
                # One game per block of (1/fraction) games, with the offset
                # rotating per block so the variant slots do not always land
                # on the same `game % 4` tribe: 0.25 -> games 0,5,10,15 of 16
                # (the built-in games then cover every tribe 3 times).
                period = max(1, int(round(1.0 / args.variant_opponents)))
                if game % period != (game // period) % period:
                    return {}
                # -AIVS games: the second owner is the Tyrano policy slot.
                return {"teacher2": True, "tribe": 2,
                        "teacher_variant2": 1 + (args.seed + iteration * count + game) % 15}
            jobs = [{"seed": args.seed + iteration * count + game,
                     "tribe": 2 if args.curriculum == 0 else game % 4,
                     **({"teacher_variant": game % (args.teacher_variants + 1)}
                        if args.mode == "bc" and args.teacher_variants else {}),
                     **settings,
                     **variant_opponent(game)} for game in range(count)]
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
        training_episodes, validation_episodes = (split_teacher_episodes(episodes, seed=args.seed)
            if args.mode == "bc" else (episodes, []))
        batch, reward_metrics = build_batch(training_episodes, config)
        optimizer, metrics = train_update(policy, batch, config, optimizer=optimizer,
                                            teacher_policy=teacher_policy)
        shaping_scale = config.shaping_scale
        version += 1
        metadata = {"mode": args.mode, "iteration": iteration, "curriculum": args.curriculum,
                    "shaping_scale": shaping_scale,
                    "rejected": rejected, **reward_metrics, **metrics}
        if args.mode == "bc":
            metadata["bc_validation"] = assess_bc_accuracy(policy, validation_episodes, minibatch=args.minibatch)
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
