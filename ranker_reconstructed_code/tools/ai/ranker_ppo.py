"""PPO self-play trainer (#7): make the Computer(AI) high-level policy stronger
by reinforcement learning, warm-started from the imitation policy.

Loop (on-policy):
  1. The torch actor-critic net drives owner 1 over the #5b IPC socket while the
     game runs headless (built-in Owner AI plays owner 2 as the opponent).
  2. The game emits the reward time-series (ai_rl_episode.jsonl): dense
     potential-based shaping + sparse win/loss.  We merge each decision's
     (state, action, logprob, value) recorded here with its reward there.
  3. Compute GAE advantages/returns, run a clipped PPO update.
  4. Repeat; periodically report mean return and owner-1 unit count.

The neural net lives entirely off-sim (AGENTS.md): the simulation stays the
single deterministic controller and publishes the returned action as ordered
Mode1 packets.  The scripted micro executor still turns each high-level action
into concrete unit commands; only the high-level *chooser* is learned.

Warm start: the MLP is behavior-cloned on the imitation dataset first, so PPO
fine-tunes a competent macro policy instead of exploring from scratch.

Opponent note: this phase trains vs the built-in AI (a strong fixed opponent,
the AlphaStar "vs built-in bots" stage).  Literal policy-vs-policy self-play
(both owners on IPC) is a later extension of the same harness.
"""

from __future__ import annotations

import argparse
import json
import socket
import subprocess
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

from ranker_rl_env import (ACTION_NAMES, N_ACTIONS, N_FEATURES, N_TARGET_CELLS,
                           TARGET_ACTION_IDS, DEFAULT_DISCOUNT)

NEG_INF = -1e9

# --- v8 network input layout (docs/3순위.md) ------------------------------
# The C++ encoder writes 9 grid channels of 8x8 cells (row-major, channel-
# major runs) scattered through the append-only feature vector:
#   [86..469]   v5: own buildings / own army / enemy mobile / enemy building
#               (visible+remembered) / resources / explored (6 channels)
#   [568..631]  v8: enemy-army fog memory
#   [636..699]  v8: passable-terrain ratio
#   [700..763]  v8: buildable ratio
# Everything else is scalar.  These constants MUST match
# ranker_ai_rl_features.cpp; the --selftest reshape check pins them.
GRID_SLICES = ((86, 470), (568, 632), (636, 700), (700, 764))
GRID_CHANNELS = sum((hi - lo) // 64 for lo, hi in GRID_SLICES)  # 9
_GRID_IDX = np.concatenate(
    [np.arange(lo, hi) for lo, hi in GRID_SLICES])
_SCALAR_IDX = np.array(
    sorted(set(range(N_FEATURES)) - set(_GRID_IDX.tolist())), dtype=np.int64)
N_SCALARS = len(_SCALAR_IDX)

# Action-history / delta auxiliary input (Python-side augmentation, NOT part
# of the C++ feature contract): the last HISTORY_K chosen actions as one-hots
# plus the one-step delta of a few key scalars.  This is the cheap 60% of a
# recurrent policy: "what did I just order" (kills the 8-frame objective
# thrash — with the same state producing the same distribution, objectives
# were re-sampled independently every decision) and "which way are things
# trending".  The aux pathway is ZERO-INITIALIZED so a BC warm start (which
# trains with zero aux) is exactly preserved at PPO iter 0.
HISTORY_K = 4
# f[1] resources, f[29] own army, f[30] own total, f[31..33] visible enemy,
# f[81] health force ratio, f[567] power force ratio.
DELTA_IDX = (1, 29, 30, 31, 32, 33, 81, 567)
AUX_DIM = HISTORY_K * N_ACTIONS + len(DELTA_IDX)


class HistoryState:
    """Per-owner rolling context for the aux input: last K actions + previous
    feature row.  Reset per game."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.actions = []
        self.prev_feat = None

    def aux(self, feat) -> np.ndarray:
        out = np.zeros(AUX_DIM, dtype=np.float32)
        for slot, action in enumerate(reversed(self.actions[-HISTORY_K:])):
            if 0 <= action < N_ACTIONS:
                out[slot * N_ACTIONS + action] = 1.0
        base = HISTORY_K * N_ACTIONS
        if self.prev_feat is not None:
            for j, idx in enumerate(DELTA_IDX):
                if idx < len(feat):
                    out[base + j] = float(feat[idx]) - float(self.prev_feat[idx])
        return out

    def push(self, feat, action):
        self.actions.append(int(action))
        if len(self.actions) > HISTORY_K:
            self.actions = self.actions[-HISTORY_K:]
        self.prev_feat = np.asarray(feat, dtype=np.float32).copy()


class _Tower(nn.Module):
    """One independent tower: grid CNN + scalar/aux branch -> trunk embed."""

    def __init__(self, hidden: int):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(GRID_CHANNELS, 16, 3, padding=1), nn.Tanh(),
            nn.Conv2d(16, 32, 3, padding=1), nn.Tanh(),
        )
        self.grid_fc = nn.Sequential(nn.Linear(32 * 8 * 8, 128), nn.Tanh())
        self.scalar_fc = nn.Sequential(nn.Linear(N_SCALARS, 128), nn.Tanh())
        # Zero-initialized aux pathway: contributes nothing until PPO trains
        # it, so a zero-aux BC warm start is exactly preserved.
        self.aux_fc = nn.Linear(AUX_DIM, 128)
        nn.init.zeros_(self.aux_fc.weight)
        nn.init.zeros_(self.aux_fc.bias)
        self.trunk = nn.Sequential(nn.Linear(256, hidden), nn.Tanh())

    def forward(self, x, aux=None):
        grid = x[:, _GRID_IDX].view(-1, GRID_CHANNELS, 8, 8)
        scalars = x[:, _SCALAR_IDX]
        grid_embed = self.grid_fc(self.conv(grid).flatten(1))
        scalar_embed = self.scalar_fc(scalars)
        if aux is not None:
            scalar_embed = scalar_embed + torch.tanh(self.aux_fc(aux))
        return self.trunk(torch.cat([grid_embed, scalar_embed], dim=1))


class ActorCritic(nn.Module):
    """v8 policy/value net (docs/3순위.md): two fully independent towers (a
    shared trunk lets value-loss gradients perturb a narrow BC-warm-started
    policy), each = grid CNN over the 9 8x8 channels + scalar branch.  The
    policy tower carries TWO heads: the 64-way action head and the 64-way
    spatial-target head (the 8x8 cell argument of the actions in
    TARGET_ACTION_IDS)."""

    def __init__(self, hidden: int = 256):
        super().__init__()
        self.hidden = hidden
        self.policy_tower = _Tower(hidden)
        self.value_tower = _Tower(hidden)
        self.action_head = nn.Linear(hidden, N_ACTIONS)
        self.target_head = nn.Linear(hidden, N_TARGET_CELLS)
        self.value_head = nn.Linear(hidden, 1)

    def policy_parameters(self):
        for module in (self.policy_tower, self.action_head, self.target_head):
            yield from module.parameters()

    def value_parameters(self):
        for module in (self.value_tower, self.value_head):
            yield from module.parameters()

    def forward(self, x, aux=None):
        policy_embed = self.policy_tower(x, aux)
        value_embed = self.value_tower(x, aux)
        return (self.action_head(policy_embed), self.target_head(policy_embed),
                self.value_head(value_embed).squeeze(-1))

    def masked_logits(self, x, mask, aux=None):
        """Action logits + value (BC / update path; target head separate)."""
        logits, _, value = self.forward(x, aux)
        logits = torch.where(mask > 0, logits, torch.full_like(logits, NEG_INF))
        return logits, value

    def full_logits(self, x, mask, tmask, aux=None):
        logits, tlogits, value = self.forward(x, aux)
        logits = torch.where(mask > 0, logits, torch.full_like(logits, NEG_INF))
        if tmask is not None:
            tlogits = torch.where(tmask > 0, tlogits,
                                  torch.full_like(tlogits, NEG_INF))
        return logits, tlogits, value

    @torch.no_grad()
    def act(self, feat, mask, deterministic: bool = False, tmask=None,
            aux=None):
        """One decision.  Returns (action, target_cell, joint_logprob, value);
        target_cell is -1 unless the chosen action takes one and a legal cell
        exists.  The joint logprob includes the target head only in that case
        (the same conditioning the PPO update recomputes)."""
        x = torch.as_tensor(feat, dtype=torch.float32).unsqueeze(0)
        m = torch.as_tensor(mask, dtype=torch.float32).unsqueeze(0)
        t = (torch.as_tensor(tmask, dtype=torch.float32).unsqueeze(0)
             if tmask is not None else None)
        a = (torch.as_tensor(aux, dtype=torch.float32).unsqueeze(0)
             if aux is not None else None)
        logits, tlogits, value = self.full_logits(x, m, t, a)
        dist = torch.distributions.Categorical(logits=logits)
        action = logits.argmax(-1) if deterministic else dist.sample()
        logp = dist.log_prob(action)
        target = -1
        if (int(action.item()) in TARGET_ACTION_IDS and t is not None and
                float(t.sum().item()) > 0):
            tdist = torch.distributions.Categorical(logits=tlogits)
            tchoice = tlogits.argmax(-1) if deterministic else tdist.sample()
            logp = logp + tdist.log_prob(tchoice)
            target = int(tchoice.item())
        return (int(action.item()), target, float(logp.item()),
                float(value.item()))


def _hidden_of(state_dict) -> int:
    """Hidden width recorded by the trunk layer (checkpoints stay
    self-describing when the default changes)."""
    for k, v in state_dict.items():
        if k.endswith("trunk.0.weight"):
            return int(v.shape[0])
    return 256


def _check_checkpoint_shape(ckpt, path):
    """Refuse to load a checkpoint trained on a different feature/action space
    or architecture.

    Space changes alter layer shapes; without this check torch fails with an
    opaque shape mismatch (or, worse, a resumed run silently mixes spaces)."""
    saved_features = ckpt.get("n_features")
    saved_actions = ckpt.get("n_actions")
    saved_arch = ckpt.get("arch")
    if saved_features is not None and saved_features != N_FEATURES:
        raise SystemExit(
            f"checkpoint {path} was trained with n_features={saved_features}, "
            f"current build expects {N_FEATURES} — incompatible feature space")
    if saved_actions is not None and saved_actions != N_ACTIONS:
        raise SystemExit(
            f"checkpoint {path} was trained with n_actions={saved_actions}, "
            f"current build expects {N_ACTIONS} — incompatible action space")
    if saved_arch is not None and saved_arch != CHECKPOINT_ARCH:
        raise SystemExit(
            f"checkpoint {path} arch={saved_arch}, current build is "
            f"{CHECKPOINT_ARCH} — retrain or convert")


CHECKPOINT_ARCH = "cnn2_v8"


def checkpoint_payload(net):
    """The standard checkpoint dict: state plus the self-describing contract
    (feature/action/target sizes, architecture, aux dim)."""
    return {"state_dict": net.state_dict(), "n_features": N_FEATURES,
            "n_actions": N_ACTIONS, "n_target_cells": N_TARGET_CELLS,
            "arch": CHECKPOINT_ARCH, "aux_dim": AUX_DIM,
            "hidden": getattr(net, "hidden", 256)}


class TorchPolicy:
    """A trained ActorCritic checkpoint as a plug-in Policy for
    ranker_ipc_server / ranker_rl_env — deterministic (argmax) by default so
    evaluation matches on a fixed seed.

    v8: stateful.  Keeps a per-owner HistoryState (action history + feature
    deltas) and answers with (action, target_cell) when the server passes the
    target-cell mask; ``wants_target_mask`` tells the server to use the
    extended call and to ``reset()`` between games."""

    wants_target_mask = True

    def __init__(self, path, deterministic: bool = True):
        ckpt = torch.load(path, map_location="cpu", weights_only=False)
        _check_checkpoint_shape(ckpt, path)
        self.net = ActorCritic(hidden=ckpt.get(
            "hidden", _hidden_of(ckpt["state_dict"])))
        self.net.load_state_dict(ckpt["state_dict"])
        self.net.eval()
        self.deterministic = deterministic
        self.history = {}

    def reset(self):
        self.history.clear()

    def act(self, features, mask, tmask=None, owner: int = 1):
        state = self.history.setdefault(owner, HistoryState())
        aux = state.aux(features)
        action, target, _, _ = self.net.act(
            features, mask, self.deterministic, tmask, aux)
        state.push(features, action)
        return action, target


def load_policy(path, deterministic: bool = True) -> TorchPolicy:
    return TorchPolicy(path, deterministic)


# --- Warm start: behavior-clone the MLP on the imitation dataset ---

def bc_pretrain(net, dataset_path, epochs: int = 300, lr: float = 1e-3,
                power: float = 0.9, device="cpu"):
    from ranker_imitation import class_weights
    cache = np.load(dataset_path)
    X = torch.as_tensor(cache["X"], dtype=torch.float32, device=device)
    y = torch.as_tensor(cache["y"], dtype=torch.long, device=device)
    M = torch.as_tensor(cache["M"], dtype=torch.float32, device=device)
    w = torch.as_tensor(class_weights(cache["y"], power=power),
                        dtype=torch.float32, device=device)
    # Optional per-sample weights (ranker_dataset_merge.py --weights): e.g.
    # a human replay's decisions count more than the built-in AI's.
    sw = (torch.as_tensor(cache["w"], dtype=torch.float32, device=device)
          if "w" in cache.files else torch.ones(len(y), device=device))
    opt = torch.optim.Adam(net.parameters(), lr=lr)
    for _ in range(epochs):
        logits, _ = net.masked_logits(X, M)
        # reduction="none" leaves the class weight inside each term, so
        # normalise by the summed (class x sample) weights - the same scale
        # as the weighted-mean cross entropy without sample weights.
        per_sample = F.cross_entropy(logits, y, weight=w, reduction="none")
        loss = (per_sample * sw).sum() / (w[y] * sw).sum()
        opt.zero_grad(); loss.backward(); opt.step()
    with torch.no_grad():
        pred = net.masked_logits(X, M)[0].argmax(-1)
        acc = float((pred == y).float().mean())
    print(f"BC pretrain: {len(X)} samples, final loss {loss.item():.3f}, "
          f"train acc {acc:.3f}")
    return net


# --- Rollout: the net drives one live game and returns its transitions ---

def _recv_lines(conn):
    buf = b""
    while True:
        try:
            chunk = conn.recv(65536)
        except ConnectionError:
            return
        if not chunk:
            return
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if line.strip():
                yield line


def drop_idle(mask):
    """Training curriculum ("no idle"): when any other action is legal, make
    no_op (#0) illegal for the sampled decision, so the early policy cannot
    collapse into doing nothing while production/build/attack chains never get
    tried.  The modified mask is what gets stored with the transition, so the
    PPO update sees the same distribution the sample came from.  Evaluation and
    later generations use the game's own mask."""
    if mask[0] and mask[1:].any():
        mask = mask.copy()
        mask[0] = 0
    return mask


def rollout(net, install_dir: Path, port: int, seed: int | None,
            max_frames: int, exe="ranker_rebuild.exe", timeout=300.0,
            deterministic=False, out_dir: Path | None = None,
            net_offset: int = 0, opp_tribe: int | None = None,
            no_idle: bool = False):
    install = Path(install_dir)
    # Parallel workers share the game-data CWD (install) but each writes its
    # result/episode JSON into a private -AIOUT dir so they never collide.
    io_dir = Path(out_dir) if out_dir is not None else install
    io_dir.mkdir(parents=True, exist_ok=True)
    episode_path = io_dir / "ai_rl_episode.jsonl"
    result_path = io_dir / "ai_selfplay_result.json"
    for stale in (episode_path, result_path):
        try:
            stale.unlink()
        except FileNotFoundError:
            pass

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(1)
    server.settimeout(timeout)
    bound = server.getsockname()[1]

    args = [str(install / exe), "-AISELF", f"-AIIPC:{bound}",
            f"-MAXFRAMES:{max_frames}"]
    if seed is not None:
        args.append(f"-SEED:{seed}")
    if out_dir is not None:
        args.append(f"-AIOUT:{io_dir.as_posix()}")
    if net_offset:
        args.append(f"-AINET:{net_offset}")
    if opp_tribe is not None:
        args.append(f"-AITRIBE:{opp_tribe}")
    proc = subprocess.Popen(args, cwd=str(install))

    # (frame, feat, mask, tmask, aux, action, target, logprob, value)
    records = []
    history = HistoryState()
    try:
        conn, _ = server.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        for raw in _recv_lines(conn):
            msg = json.loads(raw)
            if msg.get("t") == "end":
                break
            feat = np.asarray(msg["feat"], dtype=np.float32)
            mask = np.asarray(msg["mask"], dtype=np.int8)
            tmask = (np.asarray(msg["tmask"], dtype=np.int8)
                     if "tmask" in msg else
                     np.zeros(N_TARGET_CELLS, dtype=np.int8))
            if no_idle:
                mask = drop_idle(mask)
            aux = history.aux(feat)
            action, target, logprob, value = net.act(
                feat, mask, deterministic, tmask, aux)
            if mask[action] == 0:
                legal = np.nonzero(mask)[0]
                action = int(legal[0]) if len(legal) else 0
                target = -1
            history.push(feat, action)
            reply = {"action": action}
            if target >= 0:
                reply["target"] = target
            conn.sendall((json.dumps(reply) + "\n").encode())
            records.append((msg["frame"], feat, mask, tmask, aux, action,
                            target, logprob, value))
        conn.close()
    finally:
        server.close()
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:  # pragma: no cover
            proc.kill()

    # Merge rewards (emitted by the game) by frame.
    reward_by_frame, done_by_frame, loss_by_frame = {}, {}, {}
    if episode_path.exists():
        for line in episode_path.read_text().splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            if row["owner"] != 1:
                continue
            reward_by_frame[row["f"]] = row["r"]
            done_by_frame[row["f"]] = bool(row["done"])
            # War-score accounting (v3 exe): cumulative production value lost
            # [own units, own buildings, hostile units, hostile buildings].
            loss_by_frame[row["f"]] = (
                row.get("vl", 0), row.get("bl", 0),
                row.get("ovl", 0), row.get("obl", 0))

    (feats, masks, tmasks, auxes, actions, targets, logps, values, rewards,
     dones, losses) = ([] for _ in range(11))
    for (frame, feat, mask, tmask, aux, action, target, logprob,
         value) in records:
        if frame not in reward_by_frame:
            continue
        feats.append(feat); masks.append(mask); tmasks.append(tmask)
        auxes.append(aux); actions.append(action); targets.append(target)
        logps.append(logprob); values.append(value)
        rewards.append(reward_by_frame[frame]); dones.append(done_by_frame[frame])
        losses.append(loss_by_frame[frame])

    result = {}
    if result_path.exists():
        try:
            result = json.loads(result_path.read_text())
        except json.JSONDecodeError:
            pass
    return {
        "feat": np.asarray(feats, dtype=np.float32),
        "mask": np.asarray(masks, dtype=np.float32),
        "tmask": np.asarray(tmasks, dtype=np.float32),
        "aux": np.asarray(auxes, dtype=np.float32),
        "action": np.asarray(actions, dtype=np.int64),
        "target": np.asarray(targets, dtype=np.int64),
        "logp": np.asarray(logps, dtype=np.float32),
        "value": np.asarray(values, dtype=np.float32),
        "reward": np.asarray(rewards, dtype=np.float32),
        "done": np.asarray(dones, dtype=np.bool_),
        "losses": np.asarray(losses, dtype=np.float64),
        "max_frames": max_frames,
        "result": result,
    }


def rollout_versus(net, install_dir: Path, port: int, seed: int | None,
                   max_frames: int, exe="ranker_rebuild.exe", timeout=300.0,
                   out_dir: Path | None = None, net_offset: int = 0,
                   net2=None, no_idle: bool = False):
    """One -AIVS self-play game: BOTH owners are Computer(AI) and both ask the
    served policy for actions (owner 2 uses ``net2`` when given, else ``net``).
    Returns a list of TWO rollout dicts (owner 1 and owner 2), each shaped like
    ``rollout()``'s return value plus an ``owner`` key, so the same PPO update
    consumes both sides of every game — the self-play data doubling."""
    install = Path(install_dir)
    io_dir = Path(out_dir) if out_dir is not None else install
    io_dir.mkdir(parents=True, exist_ok=True)
    episode_path = io_dir / "ai_rl_episode.jsonl"
    result_path = io_dir / "ai_selfplay_result.json"
    for stale in (episode_path, result_path):
        try:
            stale.unlink()
        except FileNotFoundError:
            pass

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(1)
    server.settimeout(timeout)
    bound = server.getsockname()[1]

    args = [str(install / exe), "-AISELF", "-AIVS", f"-AIIPC:{bound}",
            f"-MAXFRAMES:{max_frames}"]
    if seed is not None:
        args.append(f"-SEED:{seed}")
    if out_dir is not None:
        args.append(f"-AIOUT:{io_dir.as_posix()}")
    if net_offset:
        args.append(f"-AINET:{net_offset}")
    proc = subprocess.Popen(args, cwd=str(install))

    # per owner: (frame, feat, mask, tmask, aux, act, target, logp, value)
    records = {1: [], 2: []}
    histories = {1: HistoryState(), 2: HistoryState()}
    try:
        conn, _ = server.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        for raw in _recv_lines(conn):
            msg = json.loads(raw)
            if msg.get("t") == "end":
                break
            owner = int(msg.get("owner", 1))
            feat = np.asarray(msg["feat"], dtype=np.float32)
            mask = np.asarray(msg["mask"], dtype=np.int8)
            tmask = (np.asarray(msg["tmask"], dtype=np.int8)
                     if "tmask" in msg else
                     np.zeros(N_TARGET_CELLS, dtype=np.int8))
            if no_idle:
                mask = drop_idle(mask)
            chooser = net2 if (net2 is not None and owner == 2) else net
            history = histories.setdefault(owner, HistoryState())
            aux = history.aux(feat)
            action, target, logprob, value = chooser.act(
                feat, mask, False, tmask, aux)
            if mask[action] == 0:
                legal = np.nonzero(mask)[0]
                action = int(legal[0]) if len(legal) else 0
                target = -1
            history.push(feat, action)
            reply = {"action": action}
            if target >= 0:
                reply["target"] = target
            conn.sendall((json.dumps(reply) + "\n").encode())
            records.setdefault(owner, []).append(
                (msg["frame"], feat, mask, tmask, aux, action, target,
                 logprob, value))
        conn.close()
    finally:
        server.close()
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:  # pragma: no cover
            proc.kill()

    rewards_by_owner = {1: {}, 2: {}}
    dones_by_owner = {1: {}, 2: {}}
    losses_by_owner = {1: {}, 2: {}}
    if episode_path.exists():
        for line in episode_path.read_text().splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            owner = int(row.get("owner", 1))
            rewards_by_owner.setdefault(owner, {})[row["f"]] = row["r"]
            dones_by_owner.setdefault(owner, {})[row["f"]] = bool(row["done"])
            losses_by_owner.setdefault(owner, {})[row["f"]] = (
                row.get("vl", 0), row.get("bl", 0),
                row.get("ovl", 0), row.get("obl", 0))

    result = {}
    if result_path.exists():
        try:
            result = json.loads(result_path.read_text())
        except json.JSONDecodeError:
            pass

    rolls = []
    for owner, rows in sorted(records.items()):
        reward_by_frame = rewards_by_owner.get(owner, {})
        done_by_frame = dones_by_owner.get(owner, {})
        loss_by_frame = losses_by_owner.get(owner, {})
        (feats, masks, tmasks, auxes, actions, targets, logps, values,
         rewards, dones, losses) = ([] for _ in range(11))
        for (frame, feat, mask, tmask, aux, action, target, logprob,
             value) in rows:
            if frame not in reward_by_frame:
                continue
            feats.append(feat); masks.append(mask); tmasks.append(tmask)
            auxes.append(aux); actions.append(action); targets.append(target)
            logps.append(logprob); values.append(value)
            rewards.append(reward_by_frame[frame])
            dones.append(done_by_frame[frame])
            losses.append(loss_by_frame.get(frame, (0, 0, 0, 0)))
        rolls.append({
            "feat": np.asarray(feats, dtype=np.float32),
            "mask": np.asarray(masks, dtype=np.float32),
            "tmask": np.asarray(tmasks, dtype=np.float32),
            "aux": np.asarray(auxes, dtype=np.float32),
            "action": np.asarray(actions, dtype=np.int64),
            "target": np.asarray(targets, dtype=np.int64),
            "logp": np.asarray(logps, dtype=np.float32),
            "value": np.asarray(values, dtype=np.float32),
            "reward": np.asarray(rewards, dtype=np.float32),
            "done": np.asarray(dones, dtype=np.bool_),
            "losses": np.asarray(losses, dtype=np.float64),
            "max_frames": max_frames,
            "result": result,
            "owner": owner,
        })
    return rolls


def compute_gae(rewards, values, dones, gamma, lam):
    n = len(rewards)
    adv = np.zeros(n, dtype=np.float32)
    last = 0.0
    for t in range(n - 1, -1, -1):
        nonterminal = 0.0 if dones[t] else 1.0
        next_value = values[t + 1] if t + 1 < n else 0.0
        delta = rewards[t] + gamma * next_value * nonterminal - values[t]
        last = delta + gamma * lam * nonterminal * last
        adv[t] = last
    returns = adv + values
    return adv, returns


# Audited production costs for the value-weighted army strength (econ term).
# Index pairs: (feature index of the completed count, un-normalize scale, cost).
# v2 feature layout: 15/16 masos(+uc), 17/18 dilophos(+uc), 42/43 x22(+uc),
# 56..66 completed-only roster counts (see ranker_ai_rl_features.cpp).
_ARMY_VALUE_TERMS = (
    # (count_idx, count_scale, uc_idx, uc_scale, unit_cost)
    (15, 50.0, 16, 10.0, 100.0),    # 마소스
    (17, 50.0, 18, 10.0, 250.0),    # 딜로포스
    (42, 50.0, 43, 10.0, 250.0),    # 벨로시스
    (56, 50.0, None, 0.0, 300.0),   # 트윈 벨로시스
    (57, 50.0, None, 0.0, 300.0),   # 람포스
    (58, 50.0, None, 0.0, 300.0),   # 트윈 람포스 (merge product; nominal)
    (59, 50.0, None, 0.0, 450.0),   # 프테라스
    (60, 50.0, None, 0.0, 800.0),   # 트리세스
    (61, 50.0, None, 0.0, 400.0),   # 둥가리
    (62, 50.0, None, 0.0, 600.0),   # 에그 스로워
    (63, 10.0, None, 0.0, 1500.0),  # 뮤턴트
    (64, 10.0, None, 0.0, 5000.0),  # 티라노스
    (65, 50.0, None, 0.0, 800.0),   # 트윈 프테라스
    (66, 50.0, None, 0.0, 600.0),   # 켄트로스
)


def _army_value(feat_row):
    total = 0.0
    for count_idx, count_scale, uc_idx, uc_scale, cost in _ARMY_VALUE_TERMS:
        count = feat_row[count_idx] * count_scale
        if uc_idx is not None:
            count -= feat_row[uc_idx] * uc_scale
        total += max(count, 0.0) * cost
    return total


def augment_rewards(roll, reward_scale, terminal_weight,
                    war_loss_weight: float = 0.5,
                    econ_scale: float = 2.0,
                    econ_cap: float = 8000.0,
                    approach_weight: float = 1.0,
                    opp_army_value=None):
    """Build the training reward from war-score accounting + saturated economy.

    v4 (2026-08-30, league combat analysis): 40000-frame self-play games ended
    with unit_value_lost 0..200 on both sides while each side massed 100..250
    workers.  Two reward defects drove that: the terminal absolute term paid
    own_v/5000 UNCAPPED and WORKER-INCLUSIVE (246 workers ~ +5.3, on par with
    the +6 elimination bonus), and nothing rewarded moving the army toward the
    enemy, so the war term never fired (no kills -> no gradient).  Changes:
      * terminal absolute/relative value now use the COMBAT army value
        (``_army_value`` of the final state, workers/buildings excluded,
        saturated by tanh(v/econ_cap)); relative uses the opponent's army
        value when the caller has it (self-play) and falls back to the result
        unit_value difference otherwise.
      * potential-based APPROACH shaping: Phi = -approach_weight * f[478]
        (army centroid -> nearest known enemy building distance / 2048; 1.0
        while unknown), reward Phi(s') - Phi(s), UNDISCOUNTED so the sum
        telescopes to Phi(s_T) - Phi(s_0) in [-1, 1] (with the learner's
        gamma the (gamma-1)*Phi drift alone paid +15 over 5000 steps).  The
        residual bias vs. the exact Ng form is bounded by approach_weight;
        it gives attack a dense edge over retreat and pays for finding the
        enemy base.

    v3 (docs/AI_PLAY_TYRANO_FULL_CAPABILITY_DESIGN.md follow-up): the previous
    reward paid for ACCUMULATION (any unit/building growth, uncapped, losses
    free), which taught tech-building farms and army hoarding.  RTS victory is
    destroying the opponent, so the reward now measures COMBAT PROGRESS:

      * dense war score (dominant): Δ(hostile value lost) − w·Δ(own value lost)
        per decision step, from the game's death accounting (vl/bl/ovl/obl,
        cumulative production cost of dead units incl. buildings).  Killing and
        razing pays; feeding the army costs (at half weight so the policy still
        takes fights: symmetric weights teach combat avoidance).
      * dense economy (auxiliary, SATURATED): growth of tanh(army_value/cap) —
        value-weighted from per-type counts x audited costs, buildings excluded.
        Marginal reward → 0 as the army approaches the cap, so beyond a healthy
        force the only remaining reward is USING it.  Growth-only (losses are
        already priced by the war term).
      * terminal: relative surviving value (tanh) + small absolute value +
        building-elimination bonus scaled by EARLY FINISH (faster kill pays up
        to 2x), matching the league's elimination>value>count judgment.

    Falls back to the accumulation reward shape when the episode has no loss
    accounting (old exe).
    """
    feat = roll["feat"]                      # (N, F), normalized
    n = len(feat)
    r = np.zeros(n, dtype=np.float32)
    losses = roll.get("losses")
    has_losses = losses is not None and len(losses) == n and n >= 2

    if has_losses:
        own_lost = losses[:, 0] + losses[:, 1]        # units + buildings
        hostile_lost = losses[:, 2] + losses[:, 3]
        war = (np.diff(hostile_lost) -
               war_loss_weight * np.diff(own_lost)) / 1000.0
        r[:-1] += (war * reward_scale).astype(np.float32)
        if feat.shape[1] > 66:
            sat = np.tanh(np.asarray(
                [_army_value(row) for row in feat]) / econ_cap)
            r[:-1] += (np.clip(np.diff(sat), 0.0, None) *
                       econ_scale).astype(np.float32)
    elif n >= 2:
        # Legacy shape (no accounting in the episode rows).
        strength = feat[:, 29] + 0.5 * feat[:, 30]
        r[:-1] = np.clip(np.diff(strength), 0.0, None) * reward_scale

    if n >= 2 and feat.shape[1] > 479 and approach_weight > 0.0:
        # Potential-based approach shaping (see docstring).  Unknown enemy
        # base counts as the maximum distance so locating it pays once.
        dist = np.where(feat[:, 479] > 0.5, feat[:, 478], 1.0)
        phi = -approach_weight * dist
        r[:-1] += (phi[1:] - phi[:-1]).astype(np.float32)

    # In -AIVS self-play rollouts carry their owner (1 or 2); "opp" is then the
    # other Computer(AI).  Plain rollouts keep the owner-1 vs built-in framing.
    own_owner = roll.get("owner", 1)
    opp_owner = 3 - own_owner if own_owner in (1, 2) else 2
    own = _units(roll["result"], own_owner)
    opp = _units(roll["result"], opp_owner)
    own_v = _unit_value(roll["result"], own_owner)
    opp_v = _unit_value(roll["result"], opp_owner)
    if n and (own or opp):
        if own_v is not None and opp_v is not None:
            # Relative surviving COMBAT value leads (workers excluded, so
            # out-massing workers is worth nothing); the absolute term is
            # saturated so hoarding cannot out-reward fighting.
            own_av = _army_value(feat[-1]) if feat.shape[1] > 66 else None
            if own_av is not None and opp_army_value is not None:
                r[-1] += float(np.tanh((own_av - opp_army_value) / 2500.0)) * \
                    (terminal_weight * 0.5)
            else:
                r[-1] += float(np.tanh((own_v - opp_v) / 2500.0)) * \
                    (terminal_weight * 0.5)
            if own_av is not None:
                r[-1] += float(np.tanh(own_av / econ_cap)) * (terminal_weight * 0.2)
            else:
                r[-1] += min(own_v / 5000.0, 1.0) * (terminal_weight * 0.2)
            own_b = _buildings(roll["result"], own_owner)
            opp_b = _buildings(roll["result"], opp_owner)
            if own_b is None or opp_b is None:
                own_b, opp_b = own, opp  # old JSON: approximate with units
            # Elimination dominates, and an EARLY kill pays up to double —
            # dragging a decided game out must not be reward-neutral.
            early = 1.0
            end_frame = roll.get("result", {}).get("end_frame")
            max_frames = roll.get("max_frames")
            if end_frame is not None and max_frames:
                early = 1.0 + max(0.0, 1.0 - float(end_frame) / max_frames)
            if opp_b == 0 and own_b > 0:
                r[-1] += terminal_weight * 2.0 * early
            elif own_b == 0 and opp_b > 0:
                r[-1] -= terminal_weight * 2.0
        else:
            # Older exe without unit_value in the result JSON.
            r[-1] += (own / 20.0) * terminal_weight
            r[-1] += float(np.tanh((own - opp) / 8.0)) * (terminal_weight * 0.5)
    return r


def ppo_update(net, opt, batch, adv, returns, clip=0.2, epochs=4,
               vf_coef=0.5, ent_coef=0.01, minibatch=1024, device="cpu",
               value_only=False):
    """One PPO update.  value_only=True trains just the value head (policy
    frozen) — used to warm up the critic before touching the BC policy, so the
    first real policy steps use calibrated advantages instead of the garbage a
    randomly-initialized value head produces (which otherwise collapses the
    narrow BC policy).

    v8: the policy logprob is JOINT over (action, target_cell) — the target
    head participates exactly on the steps whose sampled action carried a
    cell (target >= 0), mirroring the conditioning used at act() time.  The
    value loss is clipped against the rollout-time values (PPO2-style), so
    the critic cannot move arbitrarily far in one update."""
    feat = torch.as_tensor(batch["feat"], device=device)
    mask = torch.as_tensor(batch["mask"], device=device)
    tmask = torch.as_tensor(batch["tmask"], device=device)
    aux = torch.as_tensor(batch["aux"], device=device)
    action = torch.as_tensor(batch["action"], device=device)
    target = torch.as_tensor(batch["target"], device=device)
    old_logp = torch.as_tensor(batch["logp"], device=device)
    old_value = torch.as_tensor(batch["value"], device=device)
    adv_t = torch.as_tensor((adv - adv.mean()) / (adv.std() + 1e-8), device=device)
    ret_t = torch.as_tensor(returns, device=device)

    n = len(action)
    idx = np.arange(n)
    stats = {}
    for _ in range(epochs):
        np.random.shuffle(idx)
        for start in range(0, n, minibatch):
            b = idx[start:start + minibatch]
            logits, tlogits, value = net.full_logits(
                feat[b], mask[b], tmask[b], aux[b])
            dist = torch.distributions.Categorical(logits=logits)
            logp = dist.log_prob(action[b])
            ent = dist.entropy()
            targeted = target[b] >= 0
            if bool(targeted.any()):
                tdist = torch.distributions.Categorical(logits=tlogits)
                tlogp = tdist.log_prob(target[b].clamp(min=0))
                logp = logp + torch.where(targeted, tlogp,
                                          torch.zeros_like(tlogp))
                ent = ent + torch.where(targeted, tdist.entropy(),
                                        torch.zeros_like(tlogp))
            ratio = torch.exp(logp - old_logp[b])
            surr1 = ratio * adv_t[b]
            surr2 = torch.clamp(ratio, 1 - clip, 1 + clip) * adv_t[b]
            pol_loss = -torch.min(surr1, surr2).mean()
            value_clipped = old_value[b] + torch.clamp(
                value - old_value[b], -clip, clip)
            val_loss = torch.max((value - ret_t[b]).pow(2),
                                 (value_clipped - ret_t[b]).pow(2)).mean()
            ent_mean = ent.mean()
            if value_only:
                loss = vf_coef * val_loss
            else:
                loss = pol_loss + vf_coef * val_loss - ent_coef * ent_mean
            opt.zero_grad(); loss.backward()
            nn.utils.clip_grad_norm_(net.parameters(), 0.5)
            opt.step()
            stats = {"pol_loss": float(pol_loss.item()),
                     "val_loss": float(val_loss.item()),
                     "entropy": float(ent_mean.item())}
    return stats


def _units(result, owner):
    for o in result.get("owners", []):
        if o["owner"] == owner:
            return o["units"]
    return 0


def _unit_value(result, owner):
    """Surviving-army production-cost sum, or None on old result JSONs."""
    for o in result.get("owners", []):
        if o["owner"] == owner:
            return o.get("unit_value")
    return None


def _buildings(result, owner):
    """Surviving building count (the elimination criterion), or None."""
    for o in result.get("owners", []):
        if o["owner"] == owner:
            return o.get("buildings")
    return None


def selftest() -> int:
    """Offline network/contract self-test (no game).  Catches the silent
    failure modes of the v8 architecture: a wrong grid reshape axis trains
    but quietly underperforms, and a broken aux zero-init would corrupt the
    BC warm start."""
    # 1. Contract constants line up with the C++ encoder layout.
    assert N_FEATURES == 772, N_FEATURES
    assert N_ACTIONS == 64, N_ACTIONS
    assert GRID_CHANNELS == 9 and len(_GRID_IDX) == 576 and N_SCALARS == 196
    assert GRID_SLICES == ((86, 470), (568, 632), (636, 700), (700, 764))
    # 2. Reshape mapping: C++ index -> (channel, y, x).  Channel runs are
    # contiguous 64-cell row-major blocks, so v5 channel 0 cell (1,1) is
    # feature 86+9, and the v8 army-memory channel (index 6) cell (5,5) is
    # feature 568+45.
    feat = np.zeros(N_FEATURES, dtype=np.float32)
    feat[86 + 9] = 1.0
    feat[568 + 45] = 2.0
    x = torch.as_tensor(feat).unsqueeze(0)
    grid = x[:, _GRID_IDX].view(-1, GRID_CHANNELS, 8, 8)
    assert float(grid[0, 0, 1, 1]) == 1.0, "v5 grid cell mis-mapped"
    assert float(grid[0, 6, 5, 5]) == 2.0, "v8 army-memory cell mis-mapped"
    assert float(grid.sum()) == 3.0, "grid slices leak scalar features"
    # 3. act(): legality of both heads, conditional target.
    torch.manual_seed(0)
    net = ActorCritic(hidden=64)
    mask = np.zeros(N_ACTIONS, dtype=np.int8)
    spatial = TARGET_ACTION_IDS[0]
    mask[spatial] = 1
    tmask = np.zeros(N_TARGET_CELLS, dtype=np.int8)
    tmask[7] = 1
    action, target, logp, _ = net.act(feat, mask, True, tmask)
    assert action == spatial and target == 7, (action, target)
    mask2 = np.zeros(N_ACTIONS, dtype=np.int8)
    mask2[1] = 1  # non-spatial
    action, target, logp2, _ = net.act(feat, mask2, True, tmask)
    assert action == 1 and target == -1
    assert logp2 == 0.0  # single legal action, no target term
    # 4. Aux zero-init: BC (zero/None aux) and any nonzero aux give identical
    # outputs at initialization, so PPO starts exactly at the BC policy.
    aux = np.random.RandomState(0).rand(AUX_DIM).astype(np.float32)
    with torch.no_grad():
        base_logits, _, base_value = net.forward(x)
        aux_logits, _, aux_value = net.forward(
            x, torch.as_tensor(aux).unsqueeze(0))
    assert torch.allclose(base_logits, aux_logits) and \
        torch.allclose(base_value, aux_value), "aux pathway not zero at init"
    # 5. HistoryState: one-hot layout + delta slots.
    hist = HistoryState()
    a0 = hist.aux(feat)
    assert a0.shape == (AUX_DIM,) and a0.sum() == 0.0
    hist.push(feat, 3)
    feat2 = feat.copy()
    feat2[1] += 0.25
    a1 = hist.aux(feat2)
    assert a1[3] == 1.0 and abs(a1[HISTORY_K * N_ACTIONS] - 0.25) < 1e-6
    # 6. ppo_update smoke on a synthetic batch (both branches of the joint
    # logprob) + checkpoint round-trip through the shape guard.
    n = 32
    rng = np.random.RandomState(1)
    batch = {
        "feat": rng.rand(n, N_FEATURES).astype(np.float32),
        "mask": np.ones((n, N_ACTIONS), dtype=np.float32),
        "tmask": np.ones((n, N_TARGET_CELLS), dtype=np.float32),
        "aux": np.zeros((n, AUX_DIM), dtype=np.float32),
        "action": rng.randint(0, N_ACTIONS, n).astype(np.int64),
        "target": np.where(rng.rand(n) < 0.5,
                           rng.randint(0, N_TARGET_CELLS, n), -1).astype(np.int64),
        "logp": rng.randn(n).astype(np.float32) * 0.1 - 4.0,
        "value": rng.randn(n).astype(np.float32),
    }
    adv = rng.randn(n).astype(np.float32)
    ret = rng.randn(n).astype(np.float32)
    opt = torch.optim.Adam(net.parameters(), lr=1e-4)
    stats = ppo_update(net, opt, batch, adv, ret, epochs=1, minibatch=16)
    assert "pol_loss" in stats and np.isfinite(stats["pol_loss"])
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "ck.pt"
        torch.save(checkpoint_payload(net), path)
        loaded = TorchPolicy(path, deterministic=True)
        act1 = loaded.act(feat, np.ones(N_ACTIONS, dtype=np.int8),
                          np.ones(N_TARGET_CELLS, dtype=np.int8), owner=1)
        assert isinstance(act1, tuple) and len(act1) == 2
    print("ranker_ppo selftest OK: layout/reshape/heads/aux/update/checkpoint")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, default=None,
                        help="deploy dir with ranker_rebuild.exe (required "
                             "unless --selftest)")
    parser.add_argument("--dataset", type=Path, default=None,
                        help="imitation_dataset.npz for BC warm start")
    parser.add_argument("--iters", type=int, default=20)
    parser.add_argument("--games-per-iter", type=int, default=3)
    parser.add_argument("--max-frames", type=int, default=100000)
    parser.add_argument("--port", type=int, default=5600)
    parser.add_argument("--seed", type=int, default=1000,
                        help="base seed; each rollout varies it for diverse data")
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--gamma", type=float, default=DEFAULT_DISCOUNT)
    parser.add_argument("--lam", type=float, default=0.95)
    parser.add_argument("--reward-scale", type=float, default=5.0,
                        help="scale on the game's dense potential reward (kept "
                             "modest: it carries a discount-drift offset, so the "
                             "terminal relative-strength reward should dominate)")
    parser.add_argument("--terminal-weight", type=float, default=3.0,
                        help="weight of the terminal own-minus-opp-units reward "
                             "(the dominant goal signal: out-produce the opp)")
    parser.add_argument("--ent-coef", type=float, default=0.02,
                        help="entropy bonus; higher keeps exploration alive")
    parser.add_argument("--value-warmup", type=int, default=3,
                        help="iterations training only the value head (policy "
                             "frozen at BC) before policy updates begin")
    parser.add_argument("--resume", type=Path, default=None,
                        help="continue training from a .pt checkpoint instead of "
                             "BC-pretraining from scratch")
    parser.add_argument("--bc-epochs", type=int, default=300)
    parser.add_argument("--workers", type=int, default=6,
                        help="parallel rollout games (each its own port + "
                             "-AIOUT dir; big throughput win on many cores)")
    parser.add_argument("--out", type=Path, default=Path("ppo_policy.pt"))
    parser.add_argument("--eval-every", type=int, default=5)
    parser.add_argument("--no-idle", action="store_true",
                        help="training curriculum: forbid no_op while another "
                             "action is legal (rollouts only; eval keeps the "
                             "game's mask)")
    parser.add_argument("--opp-tribe", type=int, default=4,
                        help="-AITRIBE for the built-in opponent: 0=Primitive "
                             "1=Elf 2=Tyrano 3=Demon, 4=rotate by seed "
                             "(default; trains against all four tribes)")
    parser.add_argument("--lr-decay", type=float, default=0.3,
                        help="final lr as a fraction of --lr, annealed "
                             "linearly over --iters (1.0 = constant)")
    parser.add_argument("--selftest", action="store_true",
                        help="run the offline network/contract self-test "
                             "(no game needed) and exit")
    args = parser.parse_args(argv)

    if args.selftest:
        return selftest()
    if args.install_dir is None:
        parser.error("--install-dir is required unless --selftest")

    torch.manual_seed(0)
    # Confine torch to 1 thread per inference so parallel rollout workers do not
    # oversubscribe the CPU (games are the bottleneck, not the tiny MLP).
    torch.set_num_threads(1)
    net = ActorCritic()
    if args.resume is not None and Path(args.resume).exists():
        net.load_state_dict(torch.load(args.resume, map_location="cpu",
                                       weights_only=False)["state_dict"])
        print(f"resumed from {args.resume}")
    elif args.dataset is not None and Path(args.dataset).exists():
        bc_pretrain(net, args.dataset, epochs=args.bc_epochs)
    # Snapshot the BC-initialized policy so training can be measured against it on
    # fixed eval seeds (training-time rollout metrics are too noisy — units swing
    # ±3-5/game from seed/sampling even with a frozen policy).  On --resume we keep
    # the original baseline rather than overwriting it with the resumed policy.
    bc_ckpt = args.out.with_suffix(".bc.pt")
    if args.resume is None:
        torch.save(checkpoint_payload(net), bc_ckpt)
        print(f"BC baseline checkpoint -> {bc_ckpt}")
    opt = torch.optim.Adam(net.parameters(), lr=args.lr)

    net_lock = threading.Lock()
    io_base = Path(args.install_dir) / "rlout"
    best_return = float("-inf")
    best_ckpt = args.out.with_suffix(".best.pt")

    def run_game(index, seed):
        # Serialize the (sub-millisecond) net inference; the ~13 s game I/O runs
        # concurrently across workers.
        class Locked:
            def act(self, feat, mask, deterministic=False, tmask=None,
                    aux=None):
                with net_lock:
                    return net.act(feat, mask, deterministic, tmask, aux)
        # A single failed game (startup crash, IPC accept timeout) must not
        # kill a multi-hour training run — drop the game and keep the batch.
        try:
            return rollout(Locked(), args.install_dir, args.port + index, seed,
                           args.max_frames, out_dir=io_base / f"w{index}",
                           net_offset=index + 1, opp_tribe=args.opp_tribe,
                           no_idle=args.no_idle)
        except Exception as err:  # noqa: BLE001
            print(f"  rollout seed={seed} failed ({type(err).__name__}: {err});"
                  " dropped", flush=True)
            return None

    seed_counter = args.seed
    for it in range(args.iters):
        batches = []
        returns_hist, own_units, opp_units = [], [], []
        seeds = [seed_counter + g for g in range(args.games_per_iter)]
        seed_counter += args.games_per_iter
        workers = max(1, min(args.workers, args.games_per_iter))
        # Each game gets a UNIQUE port + out dir (index g), so overlap is safe
        # regardless of how the pool schedules them; the pool just caps how many
        # run at once.
        with ThreadPoolExecutor(max_workers=workers) as ex:
            rolls = [r for r in ex.map(lambda g: run_game(g, seeds[g]),
                                       range(args.games_per_iter))
                     if r is not None]
        for roll in rolls:
            if len(roll["action"]) == 0:
                print(f"  iter {it}: empty rollout, skipping")
                continue
            shaped = augment_rewards(roll, args.reward_scale,
                                     args.terminal_weight)
            adv, ret = compute_gae(shaped, roll["value"], roll["done"],
                                   args.gamma, args.lam)
            batches.append((roll, adv, ret))
            returns_hist.append(float(shaped.sum()))
            own_units.append(_units(roll["result"], 1))
            opp_units.append(_units(roll["result"], 2))

        if not batches:
            print(f"iter {it}: no data")
            continue
        merged = {k: np.concatenate([b[0][k] for b in batches])
                  for k in ("feat", "mask", "tmask", "aux", "action", "target",
                            "logp", "value")}
        adv = np.concatenate([b[1] for b in batches])
        ret = np.concatenate([b[2] for b in batches])
        # Linear LR anneal over the run (docs/3순위.md §3): large early steps
        # while the value fn calibrates, small late steps so a good policy is
        # not walked off by noise.
        frac = it / max(args.iters - 1, 1)
        lr_now = args.lr * (1.0 - (1.0 - args.lr_decay) * frac)
        for group in opt.param_groups:
            group["lr"] = lr_now
        warming = it < args.value_warmup
        stats = ppo_update(net, opt, merged, adv, ret, ent_coef=args.ent_coef,
                           value_only=warming)

        tag = "warmup" if warming else "train "
        print(f"iter {it:3d} [{tag}] | steps {len(adv):5d} | "
              f"return {np.mean(returns_hist):+.4f} | "
              f"units own {np.mean(own_units):.1f} vs opp {np.mean(opp_units):.1f} | "
              f"pol {stats['pol_loss']:+.3f} val {stats['val_loss']:.3f} "
              f"ent {stats['entropy']:.3f} lr {lr_now:.1e}", flush=True)

        # Best-so-far keeping (rollout-return proxy; noisy but strictly better
        # than nothing - the definitive pick stays post-hoc fixed-seed eval).
        # Never triggered during warmup (policy unchanged there).
        mean_return = float(np.mean(returns_hist))
        if not warming and mean_return > best_return:
            best_return = mean_return
            torch.save(checkpoint_payload(net), best_ckpt)
            print(f"  best-so-far ({mean_return:+.3f}) -> {best_ckpt.name}")

        if (it + 1) % args.eval_every == 0 or it == args.iters - 1:
            # Periodic snapshots keep an iter-stamped copy alongside args.out so
            # a later run never destroys an earlier (possibly better) policy —
            # post-hoc eval on fixed seeds picks the best (learned lesson: the
            # 8.2-unit 30-iter policy was overwritten by a worse continuation).
            torch.save(checkpoint_payload(net), args.out)
            stamped = args.out.with_suffix(f".it{it + 1:03d}.pt")
            torch.save(checkpoint_payload(net), stamped)
            print(f"  checkpoint -> {args.out} (+ {stamped.name})")

    torch.save(checkpoint_payload(net), args.out)
    print(f"final policy -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
