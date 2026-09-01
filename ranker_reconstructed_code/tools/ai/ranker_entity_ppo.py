# -*- coding: utf-8 -*-
"""Entity-command policy/critic + PPO/BC (plan sections 10 / 16.C).

v1 architecture ("entv1-mlp"): DeepSets-style shared own/target encoders over
the act2 entity rows, a KEEP/ISSUE Bernoulli gate, a 6-way non-KEEP command
head, a command-conditioned 96-token point head and a dot-product target
pointer with a relative-geometry bias MLP, plus a centralized team value on
the pooled state.  The macro tower stays the legacy 802-feature policy and is
NOT part of this module (separate optimizer per plan 10.1).

Losses follow plan 10.2/10.3 exactly:
  - per-entity conditional log-prob (gate + command + point/target);
  - timestep-nested parameter-sharing MAPPO ratio (mean over entities within
    a timestep, then mean over timesteps; the team advantage broadcasts);
  - dt-aware discounting: gamma_dt = gamma_8 ** (dt / 8), same for lambda.

`python ranker_entity_ppo.py --selftest` runs the contract checks from plan
section 16.C (mask membership, log-prob recompute, padding invariance,
U=0/E=0 finiteness, rejected-row exclusion, finite PPO update).
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import torch
import torch.nn as nn
import torch.nn.functional as F

import ranker_entity_contract as wire

ARCHITECTURE_ID = "entv1-mlp"
GAMMA_8 = 0.9998
LAMBDA_8 = 0.95

COMMAND_KEEP = 0
_POINT_COMMANDS = (1, 2, 3)
COMMAND_ATTACK_UNIT = 4

# Categorical vocabulary sizes (UNK bucket appended where the wire allows
# out-of-range values; see kAiEntity*Limit in ranker_ai_entity_control.h).
TYPE_VOCAB = 0xAA + 1
MOVEMENT_CLASS_VOCAB = 5 + 1
DCM_VOCAB = 2 + 1
RENDER_VOCAB = 32 + 1
COMMAND_BASE_VOCAB = 138 + 1
MOVESTATE_VOCAB = 8 + 1
SEMANTIC_VOCAB = 8
STATUS_VOCAB = 7
MATCH_VOCAB = 4
LAST_CMD_VOCAB = 8          # 0..6 + NONE
OWN_ROLE_VOCAB = 2
TARGET_ROLE_VOCAB = 3
OWNER_VOCAB = 9             # 0..7 players + neutral(8)


def _clampi(values: torch.Tensor, vocab: int) -> torch.Tensor:
    """Map out-of-range ids to the UNK bucket (vocab-1)."""
    return torch.where(values < vocab - 1, values,
                       torch.full_like(values, vocab - 1))


@dataclass
class EntityStep:
    """One sealed transition of one owner (ragged; U/E may be zero)."""
    global_feat: torch.Tensor            # [802]
    own_cat: torch.Tensor                # [U, 12] int64 (see OWN_CAT_FIELDS)
    own_feat: torch.Tensor               # [U, 33]
    command_mask: torch.Tensor           # [U, 7] bool
    point_mask: torch.Tensor             # [U, 96] bool
    target_cat: torch.Tensor             # [E, 4] int64
    target_feat: torch.Tensor            # [E, 14]
    pair_mask: torch.Tensor              # [U, E] bool
    command: torch.Tensor                # [U] sampled external command
    point: torch.Tensor                  # [U] token or -1
    target: torch.Tensor                 # [U] row or -1
    trainable: torch.Tensor              # [U] bool (OUTCOME result 0..2)
    old_logp: torch.Tensor               # [U]
    reward: float = 0.0
    dt: float = 8.0
    terminal: bool = False               # terminated (bootstrap 0)
    truncated: bool = False              # time-limit (bootstrap V(final))


OWN_CAT_FIELDS = ("type", "movement_class", "dcm", "render", "command_base",
                  "movestate", "semantic", "status", "match", "last_cmd",
                  "role", "presence")
TARGET_CAT_FIELDS = ("type", "owner", "role", "render")


def step_from_request(request: Dict, header: wire.Header) -> Dict:
    """Tensors from a parsed ACT_REQ (shared by rollout and BC loaders)."""
    u = header.own_rows
    e = header.target_rows
    own_cat = torch.zeros((u, len(OWN_CAT_FIELDS)), dtype=torch.long)
    for row in range(u):
        own_cat[row, 0] = min(request["own_type_id"][row], TYPE_VOCAB - 1)
        own_cat[row, 1] = min(request["own_movement_class"][row],
                              MOVEMENT_CLASS_VOCAB - 1)
        own_cat[row, 2] = min(request["own_distance_check_mode"][row],
                              DCM_VOCAB - 1)
        own_cat[row, 3] = min(request["own_render_class"][row],
                              RENDER_VOCAB - 1)
        own_cat[row, 4] = min(request["own_command_base"][row],
                              COMMAND_BASE_VOCAB - 1)
        own_cat[row, 5] = min(request["own_movement_state"][row],
                              MOVESTATE_VOCAB - 1)
        own_cat[row, 6] = min(request["own_semantic_order"][row],
                              SEMANTIC_VOCAB - 1)
        own_cat[row, 7] = min(request["own_order_status"][row],
                              STATUS_VOCAB - 1)
        own_cat[row, 8] = min(request["own_engine_order_match"][row],
                              MATCH_VOCAB - 1)
        last_cmd = request["own_last_attempt_command"][row]
        own_cat[row, 9] = 7 if last_cmd == 255 else min(last_cmd, 6)
        own_cat[row, 10] = min(request["own_role"][row], OWN_ROLE_VOCAB - 1)
        own_cat[row, 11] = request["own_presence_bits"][row]
    target_cat = torch.zeros((e, len(TARGET_CAT_FIELDS)), dtype=torch.long)
    for row in range(e):
        target_cat[row, 0] = min(request["target_type_id"][row],
                                 TYPE_VOCAB - 1)
        target_cat[row, 1] = min(request["target_owner"][row],
                                 OWNER_VOCAB - 1)
        target_cat[row, 2] = min(request["target_role"][row],
                                 TARGET_ROLE_VOCAB - 1)
        target_cat[row, 3] = min(request["target_render_class"][row],
                                 RENDER_VOCAB - 1)
    command_mask = torch.zeros((u, wire.COMMAND_COUNT), dtype=torch.bool)
    point_mask = torch.zeros((u, wire.POINT_COUNT), dtype=torch.bool)
    pair_mask = torch.zeros((u, max(e, 0)), dtype=torch.bool)
    for row in range(u):
        for c in range(wire.COMMAND_COUNT):
            command_mask[row, c] = bool(
                (request["command_mask"][row] >> c) & 1)
        words = request["point_mask"][row]
        for t in range(wire.POINT_COUNT):
            point_mask[row, t] = bool((words[t >> 5] >> (t & 31)) & 1)
        pair_words = request["attack_pair_mask_words"][row]
        for t in range(e):
            pair_mask[row, t] = bool((pair_words[t >> 5] >> (t & 31)) & 1)
    return {
        "global_feat": torch.tensor(request["global"], dtype=torch.float32),
        "own_cat": own_cat,
        "own_feat": torch.tensor(request["own_feature"],
                                 dtype=torch.float32).reshape(u, -1)
        if u else torch.zeros((0, wire.OWN_CONTINUOUS_COUNT)),
        "target_cat": target_cat,
        "target_feat": torch.tensor(request["target_feature"],
                                    dtype=torch.float32).reshape(e, -1)
        if e else torch.zeros((0, wire.TARGET_CONTINUOUS_COUNT)),
        "command_mask": command_mask,
        "point_mask": point_mask,
        "pair_mask": pair_mask,
    }


class EntityNet(nn.Module):
    def __init__(self, hidden: int = 128, issue_prior: float = None):
        """issue_prior: calibrated KEEP-gate initialization (plan 10.1's
        BC-free alternative) — the gate head's bias starts at
        logit(issue_prior) so a fresh network already keeps orders at the
        teacher-like rate instead of thrashing every unit's 7-way command.
        None leaves the default (~0.5 ISSUE) initialization."""
        super().__init__()
        self.hidden = hidden
        self.type_emb = nn.Embedding(TYPE_VOCAB, 16)
        self.mclass_emb = nn.Embedding(MOVEMENT_CLASS_VOCAB, 4)
        self.dcm_emb = nn.Embedding(DCM_VOCAB, 2)
        self.render_emb = nn.Embedding(RENDER_VOCAB, 4)
        self.cmdbase_emb = nn.Embedding(COMMAND_BASE_VOCAB, 8)
        self.movestate_emb = nn.Embedding(MOVESTATE_VOCAB, 4)
        self.semantic_emb = nn.Embedding(SEMANTIC_VOCAB, 4)
        self.status_emb = nn.Embedding(STATUS_VOCAB, 4)
        self.match_emb = nn.Embedding(MATCH_VOCAB, 2)
        self.lastcmd_emb = nn.Embedding(LAST_CMD_VOCAB, 4)
        self.ownrole_emb = nn.Embedding(OWN_ROLE_VOCAB, 2)
        self.targetrole_emb = nn.Embedding(TARGET_ROLE_VOCAB, 2)
        self.owner_emb = nn.Embedding(OWNER_VOCAB, 4)

        own_cat_dim = 16 + 4 + 2 + 4 + 8 + 4 + 4 + 4 + 2 + 4 + 2 + 8
        target_cat_dim = 16 + 4 + 2 + 4
        self.global_tower = nn.Sequential(
            nn.Linear(wire.GLOBAL_COUNT, hidden), nn.ReLU(),
            nn.Linear(hidden, hidden), nn.ReLU())
        self.own_encoder = nn.Sequential(
            nn.Linear(own_cat_dim + wire.OWN_CONTINUOUS_COUNT, hidden),
            nn.ReLU(), nn.Linear(hidden, hidden), nn.ReLU())
        self.target_encoder = nn.Sequential(
            nn.Linear(target_cat_dim + wire.TARGET_CONTINUOUS_COUNT, hidden),
            nn.ReLU(), nn.Linear(hidden, hidden), nn.ReLU())
        self.context = nn.Sequential(
            nn.Linear(hidden * 4, hidden), nn.ReLU(),
            nn.Linear(hidden, hidden), nn.ReLU())
        self.gate_head = nn.Linear(hidden, 1)
        if issue_prior is not None:
            prior = min(max(float(issue_prior), 1e-4), 1.0 - 1e-4)
            with torch.no_grad():
                self.gate_head.bias.fill_(
                    math.log(prior / (1.0 - prior)))
        self.command_head = nn.Linear(hidden, 6)   # non-KEEP commands 1..6
        self.point_head = nn.Linear(hidden, wire.POINT_COUNT)
        self.pointer_q = nn.Linear(hidden, hidden)
        self.pointer_k = nn.Linear(hidden, hidden)
        # Relative-geometry bias: guarantees pair dx/dy/distance reach the
        # score (plan 10.1).
        self.pointer_bias = nn.Sequential(
            nn.Linear(3, 32), nn.ReLU(), nn.Linear(32, 1))
        self.value_head = nn.Sequential(
            nn.Linear(hidden * 3, hidden), nn.ReLU(), nn.Linear(hidden, 1))

    def _encode_own(self, own_cat: torch.Tensor,
                    own_feat: torch.Tensor) -> torch.Tensor:
        parts = [
            self.type_emb(own_cat[:, 0]), self.mclass_emb(own_cat[:, 1]),
            self.dcm_emb(own_cat[:, 2]), self.render_emb(own_cat[:, 3]),
            self.cmdbase_emb(own_cat[:, 4]),
            self.movestate_emb(own_cat[:, 5]),
            self.semantic_emb(own_cat[:, 6]), self.status_emb(own_cat[:, 7]),
            self.match_emb(own_cat[:, 8]), self.lastcmd_emb(own_cat[:, 9]),
            self.ownrole_emb(own_cat[:, 10]),
        ]
        presence = own_cat[:, 11]
        bits = torch.stack([(presence >> b) & 1 for b in range(8)],
                           dim=-1).float()
        parts.append(bits)
        parts.append(own_feat)
        return self.own_encoder(torch.cat(parts, dim=-1))

    def _encode_target(self, target_cat: torch.Tensor,
                       target_feat: torch.Tensor) -> torch.Tensor:
        parts = [
            self.type_emb(target_cat[:, 0]),
            self.owner_emb(target_cat[:, 1]),
            self.targetrole_emb(target_cat[:, 2]),
            self.render_emb(target_cat[:, 3]),
            target_feat,
        ]
        return self.target_encoder(torch.cat(parts, dim=-1))

    def forward(self, step: Dict) -> Dict:
        """Heads + value for one ragged timestep (no batching across steps:
        wall-clock is dominated by the environment; correctness first)."""
        device = step["global_feat"].device
        g = self.global_tower(step["global_feat"].unsqueeze(0)).squeeze(0)
        u = step["own_cat"].shape[0]
        e = step["target_cat"].shape[0]
        h_own = self._encode_own(step["own_cat"], step["own_feat"]) \
            if u else torch.zeros((0, self.hidden), device=device)
        h_target = self._encode_target(step["target_cat"],
                                       step["target_feat"]) \
            if e else torch.zeros((0, self.hidden), device=device)
        pool_own = h_own.mean(dim=0) if u else torch.zeros(self.hidden,
                                                           device=device)
        pool_target = h_target.mean(dim=0) if e else torch.zeros(
            self.hidden, device=device)
        value = self.value_head(
            torch.cat([g, pool_own, pool_target], dim=-1)).squeeze(-1)
        result = {"value": value, "u": u, "e": e}
        if u == 0:
            return result
        context_in = torch.cat([
            h_own,
            g.unsqueeze(0).expand(u, -1),
            pool_own.unsqueeze(0).expand(u, -1),
            pool_target.unsqueeze(0).expand(u, -1)], dim=-1)
        z = self.context(context_in)
        result["gate_logit"] = self.gate_head(z).squeeze(-1)       # [U]
        result["command_logits"] = self.command_head(z)            # [U, 6]
        result["point_logits"] = self.point_head(z)                # [U, 96]
        if e:
            q = self.pointer_q(z)                                  # [U, H]
            k = self.pointer_k(h_target)                           # [E, H]
            scores = q @ k.t() / math.sqrt(self.hidden)            # [U, E]
            own_xy = step["own_feat"][:, 0:2]
            target_xy = step["target_feat"][:, 0:2]
            rel = target_xy.unsqueeze(0) - own_xy.unsqueeze(1)     # [U, E, 2]
            dist = rel.norm(dim=-1, keepdim=True)
            bias = self.pointer_bias(
                torch.cat([rel, dist], dim=-1)).squeeze(-1)        # [U, E]
            result["target_logits"] = scores + bias
        return result


NEG_INF = -1e9


def _masked_log_softmax(logits: torch.Tensor,
                        mask: torch.Tensor) -> torch.Tensor:
    masked = logits.masked_fill(~mask, NEG_INF)
    return F.log_softmax(masked, dim=-1)


def entity_log_prob(net_out: Dict, step: Dict, command: torch.Tensor,
                    point: torch.Tensor, target: torch.Tensor
                    ) -> torch.Tensor:
    """Per-entity conditional log-prob (plan 10.2).  Inputs are the SAMPLED
    actions; masks are the ROLLOUT-TIME masks stored with the step (live
    masks are never recomputed at update time)."""
    u = net_out["u"]
    if u == 0:
        return torch.zeros(0)
    gate_logit = net_out["gate_logit"]
    issue = command != COMMAND_KEEP
    # KEEP legal always; ISSUE possible iff any non-KEEP command legal.
    can_issue = step["command_mask"][:, 1:].any(dim=-1)
    gate_logp_issue = F.logsigmoid(gate_logit)
    gate_logp_keep = F.logsigmoid(-gate_logit)
    # When ISSUE is impossible the gate is forced KEEP with prob 1.
    logp = torch.where(
        issue, gate_logp_issue,
        torch.where(can_issue, gate_logp_keep,
                    torch.zeros_like(gate_logp_keep)))
    # Non-KEEP command head (indices 1..6 -> 0..5), masked.
    command_mask = step["command_mask"][:, 1:]
    command_logp_all = _masked_log_softmax(
        net_out["command_logits"],
        command_mask | ~can_issue.unsqueeze(-1))   # avoid all-masked NaN
    command_index = (command - 1).clamp(min=0)
    command_logp = command_logp_all.gather(
        1, command_index.unsqueeze(-1)).squeeze(-1)
    logp = logp + torch.where(issue, command_logp,
                              torch.zeros_like(command_logp))
    # Point head, command-conditioned.
    is_point = issue & ((command == 1) | (command == 2) | (command == 3))
    if is_point.any():
        point_logp_all = _masked_log_softmax(
            net_out["point_logits"],
            step["point_mask"] | ~is_point.unsqueeze(-1))
        point_logp = point_logp_all.gather(
            1, point.clamp(min=0).unsqueeze(-1)).squeeze(-1)
        logp = logp + torch.where(is_point, point_logp,
                                  torch.zeros_like(point_logp))
    # Target pointer (never evaluated when E == 0: no all--inf softmax).
    is_attack = issue & (command == COMMAND_ATTACK_UNIT)
    if is_attack.any() and net_out["e"] > 0:
        target_logp_all = _masked_log_softmax(
            net_out["target_logits"],
            step["pair_mask"] | ~is_attack.unsqueeze(-1))
        target_logp = target_logp_all.gather(
            1, target.clamp(min=0).unsqueeze(-1)).squeeze(-1)
        logp = logp + torch.where(is_attack, target_logp,
                                  torch.zeros_like(target_logp))
    return logp


@torch.no_grad()
def sample_actions(net: EntityNet, step: Dict,
                   rng: Optional[torch.Generator] = None) -> Dict:
    """Mask-legal sampling; returns commands/points/targets + logp."""
    out = net(step)
    u = out["u"]
    if u == 0:
        return {"command": torch.zeros(0, dtype=torch.long),
                "point": torch.full((0,), -1, dtype=torch.long),
                "target": torch.full((0,), -1, dtype=torch.long),
                "logp": torch.zeros(0), "value": out["value"]}
    can_issue = step["command_mask"][:, 1:].any(dim=-1)
    issue = (torch.rand(u, generator=rng) <
             torch.sigmoid(out["gate_logit"])) & can_issue
    command_logits = out["command_logits"].masked_fill(
        ~step["command_mask"][:, 1:], NEG_INF)
    command = torch.zeros(u, dtype=torch.long)
    point = torch.full((u,), -1, dtype=torch.long)
    target = torch.full((u,), -1, dtype=torch.long)
    for row in range(u):
        if not bool(issue[row]):
            continue
        probs = F.softmax(command_logits[row], dim=-1)
        picked = int(torch.multinomial(probs, 1, generator=rng)) + 1
        command[row] = picked
        if picked in _POINT_COMMANDS:
            point_probs = F.softmax(out["point_logits"][row].masked_fill(
                ~step["point_mask"][row], NEG_INF), dim=-1)
            point[row] = int(torch.multinomial(point_probs, 1,
                                               generator=rng))
        elif picked == COMMAND_ATTACK_UNIT and out["e"] > 0:
            pair_probs = F.softmax(out["target_logits"][row].masked_fill(
                ~step["pair_mask"][row], NEG_INF), dim=-1)
            target[row] = int(torch.multinomial(pair_probs, 1,
                                                generator=rng))
    logp = entity_log_prob(out, step, command, point, target)
    return {"command": command, "point": point, "target": target,
            "logp": logp, "value": out["value"]}


def compute_gae(steps: List[EntityStep], values: torch.Tensor,
                final_value: float) -> torch.Tensor:
    """dt-aware GAE (plan 10.3): stored forward-looking transition_dt."""
    advantages = torch.zeros(len(steps))
    gae = 0.0
    next_value = final_value
    for index in range(len(steps) - 1, -1, -1):
        step = steps[index]
        gamma_dt = GAMMA_8 ** (step.dt / 8.0)
        lambda_dt = LAMBDA_8 ** (step.dt / 8.0)
        nonterminal = 0.0 if step.terminal else 1.0
        delta = step.reward + gamma_dt * next_value * nonterminal - \
            float(values[index])
        gae = delta + gamma_dt * lambda_dt * nonterminal * gae
        advantages[index] = gae
        next_value = float(values[index])
    return advantages


def ppo_update(net: EntityNet, optimizer: torch.optim.Optimizer,
               steps: List[EntityStep], final_value: float = 0.0,
               clip: float = 0.2, value_coef: float = 0.5,
               entropy_coef: float = 0.003) -> Dict[str, float]:
    """One epoch of the timestep-nested parameter-sharing MAPPO update
    (plan 10.2): entities average INSIDE a timestep, timesteps average
    equally, the centralized value trains once per timestep."""
    outs = []
    values = []
    for step in steps:
        out = net(_step_tensors(step))
        outs.append(out)
        values.append(out["value"])
    values_tensor = torch.stack(values)
    with torch.no_grad():
        advantages = compute_gae(steps, values_tensor.detach(), final_value)
        if advantages.numel() > 1:
            advantages = (advantages - advantages.mean()) / \
                (advantages.std() + 1e-8)
        returns = advantages + values_tensor.detach()

    actor_terms = []
    entropy_terms = []
    for index, step in enumerate(steps):
        out = outs[index]
        if out["u"] == 0:
            continue
        tensors = _step_tensors(step)
        logp = entity_log_prob(out, tensors, step.command, step.point,
                               step.target)
        trainable = step.trainable
        if trainable.sum() == 0:
            continue   # U_trainable == 0: skip the actor term
        # Rejected rows must not even enter the exp: a mismatched old_logp
        # (sampled action != executed action) would produce inf ratios whose
        # gradients survive the later mask multiplication as NaN.
        logp_delta = torch.where(trainable, logp - step.old_logp,
                                 torch.zeros_like(logp))
        ratio = torch.exp(logp_delta)
        advantage = advantages[index]
        surrogate = torch.min(
            ratio * advantage,
            torch.clamp(ratio, 1 - clip, 1 + clip) * advantage)
        # Rejected rows leave the actor loss; the timestep mean divides by
        # the trainable count only.
        actor_terms.append(
            (surrogate * trainable.float()).sum() / trainable.sum())
        gate_probs = torch.sigmoid(out["gate_logit"])
        gate_entropy = -(gate_probs * torch.log(gate_probs + 1e-8) +
                         (1 - gate_probs) *
                         torch.log(1 - gate_probs + 1e-8))
        entropy_terms.append(gate_entropy.mean())

    actor_loss = -torch.stack(actor_terms).mean() if actor_terms else \
        torch.zeros(())
    entropy = torch.stack(entropy_terms).mean() if entropy_terms else \
        torch.zeros(())
    value_loss = F.mse_loss(values_tensor, returns)
    loss = actor_loss + value_coef * value_loss - entropy_coef * entropy
    optimizer.zero_grad()
    loss.backward()
    nn.utils.clip_grad_norm_(net.parameters(), 1.0)
    optimizer.step()
    return {"loss": float(loss.detach()),
            "actor": float(actor_loss.detach()),
            "value": float(value_loss.detach()),
            "entropy": float(entropy.detach())}


def ppo_update_batched(net: EntityNet, optimizer: torch.optim.Optimizer,
                       steps: List[EntityStep], final_value: float = 0.0,
                       clip: float = 0.2, value_coef: float = 0.5,
                       entropy_coef: float = 0.003,
                       max_train_steps: int = 1024,
                       minibatch: int = 64,
                       generator: Optional[torch.Generator] = None
                       ) -> Dict[str, float]:
    """Cost-bounded episode update: values for GAE come from ONE no-grad
    pass over the whole episode (correct full-sequence advantages), then
    the actor/critic train on at most max_train_steps uniformly sampled
    timesteps.  A 7200-step full-length match no longer stalls the serve
    loop for minutes — the wall-clock is bounded by max_train_steps."""
    with torch.no_grad():
        values = torch.stack(
            [net(_step_tensors(step))["value"] for step in steps])
        advantages = compute_gae(steps, values, final_value)
        if advantages.numel() > 1:
            advantages = (advantages - advantages.mean()) / \
                (advantages.std() + 1e-8)
        returns = advantages + values

    count = len(steps)
    if count > max_train_steps:
        order = torch.randperm(count, generator=generator)[:max_train_steps]
        indices = order.tolist()
    else:
        indices = list(range(count))
    stats = {"loss": 0.0, "actor": 0.0, "value": 0.0, "entropy": 0.0}
    batches = 0
    for start in range(0, len(indices), minibatch):
        batch = indices[start:start + minibatch]
        actor_terms = []
        entropy_terms = []
        value_terms = []
        for index in batch:
            step = steps[index]
            tensors = _step_tensors(step)
            out = net(tensors)
            value_terms.append(
                (out["value"] - returns[index]) ** 2)
            if out["u"] == 0:
                continue
            trainable = step.trainable
            if trainable.sum() == 0:
                continue
            logp = entity_log_prob(out, tensors, step.command, step.point,
                                   step.target)
            logp_delta = torch.where(trainable, logp - step.old_logp,
                                     torch.zeros_like(logp))
            ratio = torch.exp(logp_delta)
            advantage = advantages[index]
            surrogate = torch.min(
                ratio * advantage,
                torch.clamp(ratio, 1 - clip, 1 + clip) * advantage)
            actor_terms.append(
                (surrogate * trainable.float()).sum() / trainable.sum())
            gate_probs = torch.sigmoid(out["gate_logit"])
            gate_entropy = -(gate_probs * torch.log(gate_probs + 1e-8) +
                             (1 - gate_probs) *
                             torch.log(1 - gate_probs + 1e-8))
            entropy_terms.append(gate_entropy.mean())
        if not value_terms:
            continue
        actor_loss = -torch.stack(actor_terms).mean() if actor_terms else \
            torch.zeros(())
        entropy = torch.stack(entropy_terms).mean() if entropy_terms else \
            torch.zeros(())
        value_loss = torch.stack(value_terms).mean()
        loss = actor_loss + value_coef * value_loss - entropy_coef * entropy
        optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(net.parameters(), 1.0)
        optimizer.step()
        stats["loss"] += float(loss.detach())
        stats["actor"] += float(actor_loss.detach())
        stats["value"] += float(value_loss.detach())
        stats["entropy"] += float(entropy.detach())
        batches += 1
    if batches:
        for key in stats:
            stats[key] /= batches
    stats["trained_steps"] = float(len(indices))
    stats["episode_steps"] = float(count)
    return stats


def _step_tensors(step: EntityStep) -> Dict:
    return {
        "global_feat": step.global_feat,
        "own_cat": step.own_cat,
        "own_feat": step.own_feat,
        "target_cat": step.target_cat,
        "target_feat": step.target_feat,
        "command_mask": step.command_mask,
        "point_mask": step.point_mask,
        "pair_mask": step.pair_mask,
    }


def bc_loss(net: EntityNet, step: Dict, labels: List[wire.ShadowLabel]
            ) -> torch.Tensor:
    """Weighted shadow-BC negative log-likelihood; excluded rows drop out
    and KEEP subsampling reweights by 1/inclusion_probability (plan 13.1)."""
    out = net(step)
    u = out["u"]
    total = torch.zeros(())
    weight_sum = 0.0
    command = torch.zeros(u, dtype=torch.long)
    point = torch.full((u,), -1, dtype=torch.long)
    target = torch.full((u,), -1, dtype=torch.long)
    weights = torch.zeros(u)
    for row, label in enumerate(labels[:u]):
        if label.label == wire.SHADOW_EXCLUDED:
            continue
        command[row] = label.command if label.label == wire.SHADOW_ISSUE \
            else COMMAND_KEEP
        point[row] = label.point
        target[row] = label.target
        weights[row] = 1.0 / max(label.inclusion_probability, 1e-6)
    if u and weights.sum() > 0:
        logp = entity_log_prob(out, step, command, point, target)
        total = -(logp * weights).sum() / weights.sum()
        weight_sum = float(weights.sum())
    return total if weight_sum else torch.zeros(())


# ---------------------------------------------------------------------------


def _synthetic_step(u: int, e: int, seed: int = 0) -> EntityStep:
    g = torch.Generator().manual_seed(seed)
    step = EntityStep(
        global_feat=torch.rand(wire.GLOBAL_COUNT, generator=g),
        own_cat=torch.zeros((u, len(OWN_CAT_FIELDS)), dtype=torch.long),
        own_feat=torch.rand((u, wire.OWN_CONTINUOUS_COUNT), generator=g),
        command_mask=torch.zeros((u, wire.COMMAND_COUNT), dtype=torch.bool),
        point_mask=torch.zeros((u, wire.POINT_COUNT), dtype=torch.bool),
        target_cat=torch.zeros((e, len(TARGET_CAT_FIELDS)),
                               dtype=torch.long),
        target_feat=torch.rand((e, wire.TARGET_CONTINUOUS_COUNT),
                               generator=g),
        pair_mask=torch.zeros((u, e), dtype=torch.bool),
        command=torch.zeros(u, dtype=torch.long),
        point=torch.full((u,), -1, dtype=torch.long),
        target=torch.full((u,), -1, dtype=torch.long),
        trainable=torch.ones(u, dtype=torch.bool),
        old_logp=torch.zeros(u),
    )
    for row in range(u):
        step.command_mask[row, 0] = True
        step.command_mask[row, 1] = True
        step.command_mask[row, 5] = True
        step.command_mask[row, 6] = True
        step.point_mask[row, row % wire.POINT_COUNT] = True
        step.point_mask[row, (row * 7 + 3) % wire.POINT_COUNT] = True
        if e:
            step.command_mask[row, 4] = True
            step.pair_mask[row, row % e] = True
    return step


def selftest() -> None:
    torch.manual_seed(0)
    net = EntityNet(hidden=64)

    # 1. Mask membership + logp recompute (store then re-evaluate).
    step = _synthetic_step(5, 3, seed=1)
    tensors = _step_tensors(step)
    g = torch.Generator().manual_seed(42)
    sample = sample_actions(net, tensors, g)
    for row in range(5):
        c = int(sample["command"][row])
        assert step.command_mask[row, c]
        if c in _POINT_COMMANDS:
            assert step.point_mask[row, int(sample["point"][row])]
        if c == COMMAND_ATTACK_UNIT:
            assert step.pair_mask[row, int(sample["target"][row])]
    out = net(tensors)
    logp = entity_log_prob(out, tensors, sample["command"], sample["point"],
                           sample["target"])
    assert torch.allclose(logp, sample["logp"], atol=1e-5), \
        "stored log-prob does not recompute"
    assert torch.isfinite(logp).all()

    # 2. U=0 / E=0 stay finite (no all--inf softmax on the pointer).
    empty = _step_tensors(_synthetic_step(0, 0))
    out_empty = net(empty)
    assert torch.isfinite(out_empty["value"]).all()
    no_targets = _step_tensors(_synthetic_step(4, 0, seed=2))
    sample2 = sample_actions(net, no_targets, g)
    assert torch.isfinite(sample2["logp"]).all()
    assert (sample2["command"] != COMMAND_ATTACK_UNIT).all(), \
        "E=0 sampled ATTACK_UNIT"

    # 3. Row-order invariance of valid-row outputs: permuting TARGET rows
    # permutes the pointer logits identically (padding/ordering does not
    # change a valid row's result).
    step3 = _synthetic_step(3, 4, seed=3)
    t3 = _step_tensors(step3)
    out3 = net(t3)
    perm = torch.tensor([2, 0, 3, 1])
    t3p = dict(t3)
    t3p["target_cat"] = t3["target_cat"][perm]
    t3p["target_feat"] = t3["target_feat"][perm]
    t3p["pair_mask"] = t3["pair_mask"][:, perm]
    out3p = net(t3p)
    assert torch.allclose(out3["target_logits"][:, perm],
                          out3p["target_logits"], atol=1e-5), \
        "target permutation broke pointer equivariance"

    # 4. dt-aware GAE: terminal bootstraps 0, truncated bootstraps V(final);
    # everything finite.
    steps = [_synthetic_step(3, 2, seed=i) for i in range(4)]
    for index, s in enumerate(steps):
        s.reward = 0.5 - 0.1 * index
        s.dt = 8.0 if index < 3 else 3.0   # terminal partial dt
    steps[-1].terminal = True
    values = torch.tensor([0.1, 0.2, 0.3, 0.4])
    adv_terminal = compute_gae(steps, values, final_value=123.0)
    assert torch.isfinite(adv_terminal).all()
    steps[-1].terminal = False
    steps[-1].truncated = True
    adv_truncated = compute_gae(steps, values, final_value=0.7)
    assert torch.isfinite(adv_truncated).all()
    assert not torch.allclose(adv_terminal[-1], adv_truncated[-1]), \
        "terminal and truncated bootstraps were identical"

    # 5. Rejected rows leave the actor loss but keep the others: a huge
    # old_logp on an untrainable row must not blow up the update.
    for s in steps:
        sampled = sample_actions(net, _step_tensors(s), g)
        s.command = sampled["command"]
        s.point = sampled["point"]
        s.target = sampled["target"]
        s.old_logp = sampled["logp"]
    steps[0].trainable[1] = False
    steps[0].old_logp[1] = -1e6   # would explode the ratio if included
    optimizer = torch.optim.Adam(net.parameters(), lr=1e-4)
    stats = ppo_update(net, optimizer, steps)
    assert all(math.isfinite(v) for v in stats.values()), stats

    # 5b. Batched update: bounded sampling, finite, trainable exclusion.
    big = [_synthetic_step(3, 2, seed=100 + i) for i in range(20)]
    for s5 in big:
        sampled = sample_actions(net, _step_tensors(s5), g)
        s5.command = sampled["command"]
        s5.point = sampled["point"]
        s5.target = sampled["target"]
        s5.old_logp = sampled["logp"]
        s5.reward = 0.1
    big[3].trainable[:] = False
    big[3].old_logp[:] = -1e6
    gen = torch.Generator().manual_seed(7)
    stats_batched = ppo_update_batched(net, optimizer, big,
                                       max_train_steps=8, minibatch=4,
                                       generator=gen)
    assert stats_batched["trained_steps"] == 8.0
    assert stats_batched["episode_steps"] == 20.0
    assert all(math.isfinite(v) for v in stats_batched.values()), \
        stats_batched

    # 6. BC: excluded labels drop; inclusion probability reweights.
    bc_step = _step_tensors(_synthetic_step(3, 2, seed=9))
    labels = [
        wire.ShadowLabel(wire.SHADOW_ISSUE, 1, 0, 0, -1, 1.0),
        wire.ShadowLabel(wire.SHADOW_EXCLUDED, 0,
                         wire.SHADOW_REASON_POINT_ERROR, -1, -1, 1.0),
        wire.ShadowLabel(wire.SHADOW_KEEP, 0, 0, -1, -1, 0.5),
    ]
    loss = bc_loss(net, bc_step, labels)
    assert torch.isfinite(loss)
    loss.backward()

    # 7. step_from_request round trip from the wire fixtures.
    u, e = 2, 2
    request = {
        "global": [0.0] * wire.GLOBAL_COUNT,
        "own_type_id": [5, 6], "own_movement_class": [0, 3],
        "own_distance_check_mode": [0, 0], "own_render_class": [1, 40],
        "own_command_base": [0, 200], "own_movement_state": [0, 9],
        "own_semantic_order": [0, 1], "own_order_status": [0, 2],
        "own_engine_order_match": [0, 3],
        "own_last_attempt_command": [255, 3], "own_role": [0, 1],
        "own_presence_bits": [3, 0xff],
        "own_feature": [[0.0] * wire.OWN_CONTINUOUS_COUNT] * u,
        "command_mask": [0x7f, 0x61],
        "point_mask": [[0xffffffff] * 3] * u,
        "target_type_id": [5, 5], "target_owner": [1, 8],
        "target_role": [0, 2], "target_render_class": [1, 33],
        "target_feature": [[0.0] * wire.TARGET_CONTINUOUS_COUNT] * e,
        "attack_pair_mask_words": [[3], [1]],
    }
    header = wire.Header(own_rows=u, target_rows=e)
    tensors7 = step_from_request(request, header)
    assert tensors7["own_cat"][1, 4] == COMMAND_BASE_VOCAB - 1   # UNK bucket
    assert tensors7["target_cat"][1, 3] == RENDER_VOCAB - 1
    assert bool(tensors7["command_mask"][0, 4]) and \
        not bool(tensors7["command_mask"][1, 4])
    out7 = net(tensors7)
    assert torch.isfinite(out7["value"]).all()

    print("ranker_entity_ppo: selftest passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    arguments = parser.parse_args()
    if arguments.selftest:
        selftest()
        return 0
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
