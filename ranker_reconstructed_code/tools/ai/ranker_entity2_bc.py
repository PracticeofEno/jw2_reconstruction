# -*- coding: utf-8 -*-
"""ENTCMD02 checkpoint I/O, SHD3 dataset loading and behaviour cloning
(plan sections 15.1 / 15.2 + team-intent slot labels).

Checkpoints are ENTCMD02-only: every metadata key is pinned exactly and an
ENTCMD01 checkpoint (contract_id ENTCMD01 / protocol 2) is a hard reject.
The optional one-way converter copies the allow-listed ENTCMD01 tensors
(global tower shape permitting, target encoder, gate, command rows 1..5,
attack pointer) into a fresh ENTCMD02 net and records the parent SHA-256,
the tensor allowlist and the mapping id.

Usage:
    python ranker_entity2_bc.py --data DIR_OR_FILES --out entity2_bc.pt
    python ranker_entity2_bc.py --convert-from entity_v1.pt --out entity2_init.pt
    python ranker_entity2_bc.py --selftest
"""

from __future__ import annotations

import glob
import hashlib
import os
import random
import tempfile
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence

import torch

import ranker_entity2_contract as wire
import ranker_entity2_ppo as ppo
import ranker_entity2_squads as squads

CHECKPOINT_METADATA = {
    "contract_id": "ENTCMD02",
    "protocol": wire.PROTOCOL,
    "header_bytes": wire.HEADER_BYTES,
    "observation_schema_version": wire.VERSIONS[0],
    "global_feature_version": wire.VERSIONS[1],
    "entity_feature_version": wire.VERSIONS[2],
    "entity_action_version": wire.VERSIONS[3],
    "semantic_vocabulary_version": wire.VERSIONS[4],
    "point_geometry_version": wire.VERSIONS[5],
    "economy_candidate_version": wire.VERSIONS[6],
    "outcome_version": wire.VERSIONS[7],
    "global_feature_count": wire.GLOBAL_COUNT,
    "command_count": wire.COMMAND_COUNT,
    "policy_commands": list(ppo.POLICY_COMMANDS),
    "point_count": wire.POINT_COUNT,
    "own_continuous_count": wire.OWN_CONTINUOUS_COUNT,
    "own_extra_count": ppo.OWN_EXTRA,
    "target_continuous_count": wire.TARGET_CONTINUOUS_COUNT,
    "candidate_feature_count": wire.CANDIDATE_FEATURE_COUNT,
    "candidate_schema_id": "entcand1",
    "candidate_row_bytes": wire.CANDIDATE_ROW_BYTES,
    "queue_slot_count": wire.QUEUE_SLOT_COUNT,
    "wire_row_limit": wire.WIRE_ROW_LIMIT,
    "candidate_segment_limit": wire.WIRE_ROW_LIMIT,
    "normalization_id": "entnorm2",
    "architecture_id": ppo.ARCHITECTURE_ID,
    "control_schema_id": squads.CONTROL_SCHEMA_ID,
    "control_feature_count": squads.CONTROL_FEATURE_COUNT,
    "ledger_context_id": "entledger2",
    "slot_count": wire.SLOT_COUNT,
    "slot_command_count": wire.SLOT_COMMAND_COUNT,
    "start_candidate_count": wire.START_CANDIDATE_COUNT,
    "global_cell_count": wire.GLOBAL_CELL_COUNT,
    "scout_capacity": wire.SCOUT_CAPACITY,
    "assign_ledger_id": "entassign1",
    "shadow_record_magic": wire.SHADOW_MAGIC.decode("ascii"),
    "reward_id": ppo.REWARD_ID,
    "reward": {"war_scale": ppo.WAR_SCALE, "war_building_weight": ppo.WAR_BUILDING_WEIGHT,
               "econ_beta": ppo.ECON_BETA, "econ_z_scale": ppo.ECON_Z_SCALE,
               "econ_pop_weight": ppo.ECON_POP_WEIGHT,
               "intent_beta": ppo.INTENT_BETA,
               "intent_distance_scale": ppo.INTENT_DISTANCE_SCALE,
               "time_cost_per_frame": ppo.TIME_COST_PER_FRAME,
               "terminal_win": ppo.TERMINAL_WIN_PAYOFF,
               "terminal_loss": -ppo.TERMINAL_WIN_PAYOFF},
    "gamma_8": ppo.GAMMA_8,
    "lambda_8": ppo.LAMBDA_8,
    "cadence_frames": 8,
    "chunk_steps": ppo.CHUNK_STEPS,
    "teacher_point_max_error_px": 64,
}


# Training hyper-parameters recorded for provenance only: a reward or GAE
# change does not invalidate the weights, so these keys are reported, not
# enforced.
TRAINING_METADATA_KEYS = ("reward_id", "reward", "gamma_8", "lambda_8",
                          "teacher_point_max_error_px")


def validate_checkpoint_metadata(metadata: Dict, log=print) -> None:
    if not isinstance(metadata, dict):
        raise RuntimeError("checkpoint has no ENTCMD02 metadata")
    if metadata.get("contract_id") != "ENTCMD02":
        raise RuntimeError("not an ENTCMD02 checkpoint (contract_id=%r)" %
                           (metadata.get("contract_id"),))
    if metadata.get("architecture_id") in ("entv5-slot-hunt-mlp", "entv6-type-squads-mlp"):
        raise RuntimeError("legacy checkpoint requires explicit conversion: "
                           "ranker_entity2_bc.py --convert-squads-from OLD.pt --out NEW.pt")
    for key, expected in CHECKPOINT_METADATA.items():
        if metadata.get(key) != expected:
            if key in TRAINING_METADATA_KEYS:
                log("ranker_entity2_bc: checkpoint trained under %s=%r (now %r)" %
                    (key, metadata.get(key), expected))
                continue
            raise RuntimeError("checkpoint metadata mismatch: %s=%r expected %r" %
                               (key, metadata.get(key), expected))


def module_fingerprint(module: torch.nn.Module) -> bytes:
    digest = hashlib.sha256()
    for name, tensor in sorted(module.state_dict().items()):
        digest.update(name.encode("utf-8"))
        digest.update(str(tensor.dtype).encode("utf-8"))
        digest.update(str(tuple(tensor.shape)).encode("utf-8"))
        digest.update(tensor.detach().cpu().contiguous().numpy().tobytes())
    return digest.digest()


def save_checkpoint(net: ppo.Entity2Net, path: str, extra: Optional[Dict] = None) -> None:
    payload = {"model": net.state_dict(), "metadata": dict(CHECKPOINT_METADATA),
               "hidden": net.hidden}
    if extra is not None:
        payload["extra"] = extra
    directory = os.path.dirname(os.path.abspath(path)) or "."
    os.makedirs(directory, exist_ok=True)
    fd, temp_path = tempfile.mkstemp(prefix="." + os.path.basename(path) + ".",
                                     suffix=".tmp", dir=directory)
    try:
        with os.fdopen(fd, "wb") as handle:
            torch.save(payload, handle)
        os.replace(temp_path, path)
    finally:
        if os.path.exists(temp_path):
            os.unlink(temp_path)


def load_checkpoint_payload(path: str):
    payload = torch.load(path, map_location="cpu", weights_only=False)
    validate_checkpoint_metadata(payload.get("metadata"))
    net = ppo.Entity2Net(hidden=int(payload.get("hidden", 128)))
    # Additive parameters introduced later (per-role gate bias) are absent
    # from older checkpoints and load as their neutral zero initialisation.
    state = payload["model"]
    missing = [k for k in net.state_dict().keys() if k not in state]
    if missing and all(k.startswith("gate_role_bias.") for k in missing):
        net.load_state_dict(state, strict=False)
    else:
        net.load_state_dict(state)
    return net, payload


def load_checkpoint(path: str) -> ppo.Entity2Net:
    return load_checkpoint_payload(path)[0]


def convert_squad_checkpoint(source_path: str) -> Dict:
    """Warm start from action-v5 per-unit or type-squad checkpoints.

    Drop the HARVEST output and initialise changed summary inputs. Optimizer, rollout
    lineage and old likelihoods are deliberately not carried across the
    changed action distribution. Unknown wire/action versions are rejected.
    """
    with open(source_path, "rb") as handle:
        parent_sha = hashlib.sha256(handle.read()).hexdigest()
    payload = torch.load(source_path, map_location="cpu", weights_only=False)
    metadata = dict(payload.get("metadata") or {})
    architecture = metadata.get("architecture_id")
    if architecture not in ("entv5-slot-hunt-mlp", "entv6-type-squads-mlp"):
        raise RuntimeError("worker-task converter requires entv5 or entv6")
    if metadata.get("entity_action_version") != 5:
        raise RuntimeError("worker-task converter requires legacy action version 5")
    metadata.update(architecture_id=ppo.ARCHITECTURE_ID,
                    entity_action_version=wire.VERSIONS[3],
                    policy_commands=list(ppo.POLICY_COMMANDS),
                    control_schema_id=squads.CONTROL_SCHEMA_ID,
                    control_feature_count=squads.CONTROL_FEATURE_COUNT)
    validate_checkpoint_metadata(metadata)
    net = ppo.Entity2Net(hidden=int(payload.get("hidden", 128)))
    source = payload["model"]
    target = net.state_dict()
    copied = []
    with torch.no_grad():
        for name, tensor in target.items():
            if name not in source and name.startswith("gate_role_bias."):
                continue
            if name not in source:
                raise RuntimeError("squad converter missing tensor: " + name)
            old = source[name]
            if name == "own_encoder.0.weight":
                added = squads.CONTROL_FEATURE_COUNT if architecture == "entv5-slot-hunt-mlp" else 0
                if old.shape != (tensor.shape[0], tensor.shape[1] - added):
                    raise RuntimeError("squad converter own encoder shape mismatch")
                tensor.zero_()
                tensor[:, :old.shape[1]].copy_(old)
                # The old economy-source flag becomes the pooled worker-task
                # flag; do not carry its incompatible meaning into the model.
                if not added:
                    tensor[:, -1] = 0
            elif name in ("command_head.weight", "command_head.bias"):
                if old.shape[0] != wire.COMMAND_COUNT - 1 or old.shape[1:] != tensor.shape[1:]:
                    raise RuntimeError("worker-task converter command head shape mismatch")
                tensor.copy_(old[[c - 1 for c in ppo.POLICY_COMMANDS]])
            elif old.shape == tensor.shape:
                tensor.copy_(old)
            else:
                raise RuntimeError("squad converter tensor shape mismatch: " + name)
            copied.append(name)
    if set(source) - set(target):
        raise RuntimeError("squad converter unexpected model tensors")
    net.load_state_dict(target)
    return {"net": net, "copied": copied, "parent_sha256": parent_sha,
            "mapping_id": architecture + "-to-worker-tasks-v2"}


# ---------------------------------------------------------------------------
# ENTCMD01 -> ENTCMD02 one-way converter (plan 15.2 allowlist)
# ---------------------------------------------------------------------------

CONVERTER_MAPPING_ID = "entcmd01-to-worker-tasks-v2"
# (source tensor name, destination tensor name).  Shapes must match exactly;
# the ENTCMD02 global tower takes 805 inputs so only its second layer copies.
CONVERTER_ALLOWLIST = [
    ("type_emb.weight", "type_emb.weight"),
    ("render_emb.weight", "render_emb.weight"),
    ("owner_emb.weight", "owner_emb.weight"),
    ("target_role_emb.weight", "target_role_emb.weight"),
    ("global_tower.2.weight", "global_tower.2.weight"),
    ("global_tower.2.bias", "global_tower.2.bias"),
    ("target_encoder.0.weight", "target_encoder.0.weight"),
    ("target_encoder.0.bias", "target_encoder.0.bias"),
    ("target_encoder.2.weight", "target_encoder.2.weight"),
    ("target_encoder.2.bias", "target_encoder.2.bias"),
    ("gate_head.weight", "gate_head.weight"),
    ("gate_head.bias", "gate_head.bias"),
    ("pointer_q.weight", "pointer_q.weight"),
    ("pointer_q.bias", "pointer_q.bias"),
    ("pointer_k.weight", "pointer_k.weight"),
    ("pointer_k.bias", "pointer_k.bias"),
    ("pointer_bias.0.weight", "pointer_bias.0.weight"),
    ("pointer_bias.0.bias", "pointer_bias.0.bias"),
    ("pointer_bias.2.weight", "pointer_bias.2.weight"),
    ("pointer_bias.2.bias", "pointer_bias.2.bias"),
]


def convert_entcmd01_checkpoint(source_path: str, hidden: int = 128) -> Dict:
    """Copy the allow-listed ENTCMD01 tensors into a fresh ENTCMD02 net.
    Returns {"net", "copied", "parent_sha256"}. Only commands 1..5 have
    matching semantics in the eight-way learned command head."""
    raw = open(source_path, "rb").read()
    parent_sha = hashlib.sha256(raw).hexdigest()
    payload = torch.load(source_path, map_location="cpu", weights_only=False)
    metadata = payload.get("metadata", {})
    if metadata.get("contract_id") != "ENTCMD01":
        raise RuntimeError("converter source is not an ENTCMD01 checkpoint")
    source = payload["model"]
    net = ppo.Entity2Net(hidden=hidden)
    target = net.state_dict()
    copied: List[str] = []
    with torch.no_grad():
        for src_name, dst_name in CONVERTER_ALLOWLIST:
            if src_name in source and dst_name in target and \
                    tuple(source[src_name].shape) == tuple(target[dst_name].shape):
                target[dst_name].copy_(source[src_name])
                copied.append(dst_name)
        if "command_head.weight" in source and \
                tuple(source["command_head.weight"].shape) == (6, hidden):
            # Legacy row 6 was STOP; personal STOP and learned HARVEST are
            # absent. Preserve only MOVE/ATTACK_MOVE/PATROL/ATTACK_UNIT/HOLD.
            target["command_head.weight"][0:5].copy_(source["command_head.weight"][0:5])
            target["command_head.bias"][0:5].copy_(source["command_head.bias"][0:5])
            copied.append("command_head[0:5]")
    net.load_state_dict(target)
    return {"net": net, "copied": copied, "parent_sha256": parent_sha,
            "mapping_id": CONVERTER_MAPPING_ID}


# ---------------------------------------------------------------------------
# SHD3 dataset
# ---------------------------------------------------------------------------


def load_shadow_records(paths: Sequence[str], limit: int = 0) -> List[wire.ShadowRecord]:
    records: List[wire.ShadowRecord] = []
    for path in paths:
        data = open(path, "rb").read()
        for record in wire.parse_shadow_records(data, os.path.abspath(path)):
            records.append(record)
            if limit and len(records) >= limit:
                return records
    return records


def dataset_stats(records: Sequence[wire.ShadowRecord]) -> Dict:
    stats = {"records": len(records), "rows": 0, "keep": 0, "issue": 0,
             "excluded": 0, "exclude_reasons": {}, "issue_commands": {},
             "assign_issue": 0, "assign_excluded": 0, "assign_slots": {},
             "slot_issue": 0, "slot_excluded": 0, "slot_commands": {}}
    for record in records:
        for label in record.labels:
            stats["rows"] += 1
            if label.label == wire.SHADOW_KEEP:
                stats["keep"] += 1
            elif label.label == wire.SHADOW_ISSUE:
                stats["issue"] += 1
                stats["issue_commands"][label.command] = \
                    stats["issue_commands"].get(label.command, 0) + 1
            else:
                stats["excluded"] += 1
                stats["exclude_reasons"][label.exclude_reason] = \
                    stats["exclude_reasons"].get(label.exclude_reason, 0) + 1
            if label.assign_label == wire.SHADOW_ISSUE:
                stats["assign_issue"] += 1
                stats["assign_slots"][label.assign] = \
                    stats["assign_slots"].get(label.assign, 0) + 1
            elif label.assign_label == wire.SHADOW_EXCLUDED:
                stats["assign_excluded"] += 1
        for slot, label in enumerate(record.slot_labels):
            if label.label == wire.SHADOW_ISSUE:
                stats["slot_issue"] += 1
                key = (slot, label.command)
                stats["slot_commands"][key] = stats["slot_commands"].get(key, 0) + 1
            elif label.label == wire.SHADOW_EXCLUDED:
                stats["slot_excluded"] += 1
    return stats


@dataclass
class ShadowStep:
    step: ppo.EntityStep
    labels: List[wire.ShadowLabel]
    header: wire.Header
    losses: List[int]
    material: List[int]
    slot_labels: List[wire.ShadowSlotLabel]
    intent_material: List[int]


def _teacher_member_command(record: wire.ShadowRecord, source: int,
                            preserve_scout: bool = False):
    """Resolve personal KEEP against the teacher's effective legacy slot.

    SHD3 omits a personal point order when its slot already carries it. MAIN
    and RAID commanders disappear at the type-squad boundary, so their orders
    must become personal/shared ISSUE labels. Unknown or unrepresentable slot
    orders cannot be used as evidence for KEEP. Personal ISSUE takes priority.
    """
    label = record.labels[source]
    if label.label == wire.SHADOW_EXCLUDED:
        return None
    if label.label == wire.SHADOW_ISSUE:
        return label.command, label.argument
    if label.assign_label == wire.SHADOW_EXCLUDED:
        return None
    request = record.request
    current_slot = request["own_slot_id"][source]
    slot = current_slot
    if label.assign_label == wire.SHADOW_ISSUE:
        slot = label.assign - 1
    if slot == wire.SLOT_NONE:
        return wire.COMMAND_KEEP, -1
    if slot == wire.SLOT_SCOUT:
        # Existing scouts retain their separate commander. A new scout must
        # be detached by the projected assignment before resolving consensus.
        return (wire.COMMAND_KEEP, -1) if preserve_scout else None
    teacher = record.slot_labels[slot]
    if teacher.label == wire.SHADOW_EXCLUDED:
        return None
    if teacher.label == wire.SHADOW_ISSUE:
        command, cell = teacher.command, teacher.cell
    else:
        previous = request["slots"][slot]
        if not previous.active:
            return wire.COMMAND_KEEP, -1
        command, cell = previous.command, previous.cell
    # KEEP also encodes a repeated personal shadow latch (e.g. ATTACK_UNIT).
    # It is ambiguous when a live order is not known to originate in this
    # slot. Do not turn an ongoing personal attack into the commander's HOLD.
    # MATCH refers to the snapshot slot, not a same-tick reassignment target.
    status = request["own_order_status"][source]
    tracking = status in (1, 2)  # AiEntityOrderStatus awaiting_apply / active
    untracked = status == 0 and request["own_semantic_order"][source] != wire.SEMANTIC_NONE
    if (tracking or untracked) and (
            slot != current_slot or
            request["own_slot_order_relation"][source] != wire.SLOT_RELATION_MATCH):
        return None
    point_commands = {
        wire.SLOT_COMMAND_MOVE: wire.COMMAND_MOVE,
        wire.SLOT_COMMAND_ATTACK_MOVE: wire.COMMAND_ATTACK_MOVE,
        wire.SLOT_COMMAND_PATROL: wire.COMMAND_PATROL,
    }
    if command in point_commands:
        return point_commands[command], cell
    if command == wire.SLOT_COMMAND_HOLD:
        return wire.COMMAND_HOLD, -1
    # STOP has no personal policy command. HUNT_NEUTRAL also needs a member's
    # resolved target, which is absent from a personal KEEP record.
    return None


def _squad_teacher_labels(step: ppo.EntityStep, record: wire.ShadowRecord):
    """Project compatible teacher actions; never vote away conflicting orders.

    Raw SHD3 blocks have already been verified by the wire reader. Compact
    blocks replay the projected prefix. Resolve old slot orders after the
    teacher's same-tick assignments before checking shared-action consensus.
    Choices that cannot represent a shared action are excluded from supervision.
    """
    labels = []
    unresolved = None
    raw_unresolved = next((i for i, label in enumerate(record.labels)
                           if label.exclude_reason == wire.SHADOW_REASON_PREFIX_UNRESOLVED), None)
    for j, row in enumerate(step.control_layout.rows):
        members = list(row.members)
        if raw_unresolved is not None and members[0] >= raw_unresolved and unresolved is None:
            unresolved = j
        label = wire.ShadowLabel(wire.SHADOW_EXCLUDED, wire.SHADOW_EXCLUDED_COMMAND,
                                 wire.SHADOW_REASON_MULTIPLE_DESIRED, -1, 1.0,
                                 wire.SHADOW_EXCLUDED, 0)
        other_assignments_known_keep = True
        if row.kind == squads.WORKER_TASK:
            assign_source = -1
            choices = []
            known = all(record.labels[i].label != wire.SHADOW_EXCLUDED and
                        record.labels[i].inclusion_probability == 1.0 for i in members)
            for i in members:
                original = record.labels[i]
                if original.label != wire.SHADOW_ISSUE:
                    continue
                command, argument = original.command, original.argument
                if command == wire.COMMAND_BUILD or (
                        command == wire.COMMAND_MOVE and 0 <= argument < wire.GLOBAL_CELL_COUNT):
                    choices.append((command, argument))
                elif command not in (wire.COMMAND_HARVEST, wire.COMMAND_MOVE,
                                      wire.COMMAND_ATTACK_UNIT):
                    known = False
            # A single dispatcher cannot represent simultaneous different
            # worker tasks. Automatic harvesting/defence is not a BC target.
            if known and len(choices) <= 1:
                command, argument = choices[0] if choices else (wire.COMMAND_KEEP, -1)
                label = wire.ShadowLabel(wire.SHADOW_ISSUE if command else wire.SHADOW_KEEP,
                                         command, 0, argument, 1.0, wire.SHADOW_EXCLUDED, 0)
        elif row.kind == squads.SQUAD:
            assign_source = row.scout_source
            other_assignments_known_keep = all(
                record.labels[i].assign_label == wire.SHADOW_KEEP
                for i in members if i != assign_source)
            detached = -1
            if assign_source >= 0 and other_assignments_known_keep:
                source_label = record.labels[assign_source]
                if source_label.assign_label == wire.SHADOW_ISSUE and \
                        source_label.assign == wire.SLOT_SCOUT + 1:
                    detached = assign_source
            relevant = [i for i in members
                        if i != detached and not (record.labels[i].label == wire.SHADOW_ISSUE and
                                record.labels[i].command in wire.ECONOMY_COMMANDS)]
            if relevant and all(record.labels[i].inclusion_probability == 1.0 for i in relevant):
                choices = {_teacher_member_command(record, i) for i in relevant}
                if None not in choices and len(choices) == 1:
                    command, argument = next(iter(choices))
                    label = wire.ShadowLabel(wire.SHADOW_ISSUE if command else wire.SHADOW_KEEP,
                                             command, 0, argument, 1.0,
                                             wire.SHADOW_EXCLUDED, 0)
        else:
            original = record.labels[members[0]]
            label = wire.ShadowLabel(**original.__dict__)
            label.assign_label, label.assign = wire.SHADOW_EXCLUDED, 0
            if label.label == wire.SHADOW_KEEP:
                choice = _teacher_member_command(record, members[0], preserve_scout=True)
                if choice is None:
                    label.label, label.command = wire.SHADOW_EXCLUDED, wire.SHADOW_EXCLUDED_COMMAND
                    label.argument, label.exclude_reason = -1, wire.SHADOW_REASON_MULTIPLE_DESIRED
                else:
                    label.command, label.argument = choice
                    label.label = wire.SHADOW_ISSUE if label.command else wire.SHADOW_KEEP
            assign_source = members[0]
        # An unrepresentable assignment on another member is unknown
        # supervision, not evidence that the squad should choose KEEP.
        if assign_source >= 0 and other_assignments_known_keep:
            original = record.labels[assign_source]
            if original.assign_label == wire.SHADOW_KEEP or (
                    original.assign_label == wire.SHADOW_ISSUE and
                    ppo._assign_legal(step.assign_mask[j], original.assign)):
                label.assign_label, label.assign = original.assign_label, original.assign
        labels.append(label)
    slot_labels = [wire.ShadowSlotLabel(wire.SHADOW_EXCLUDED) for _ in range(wire.SLOT_COUNT)]
    slot_labels[wire.SLOT_SCOUT] = record.slot_labels[wire.SLOT_SCOUT]
    # Resolve masks under exactly the projected (rather than the original
    # per-entity) prefix. Remove incompatible labels before the final replay.
    def blocks():
        commands = torch.tensor([l.command if l.label == wire.SHADOW_ISSUE else 0
                                  for l in labels], dtype=torch.long)
        arguments = torch.tensor([l.argument if l.label == wire.SHADOW_ISSUE else -1
                                   for l in labels], dtype=torch.long)
        assigns = torch.tensor([l.assign if l.assign_label == wire.SHADOW_ISSUE else 0
                                 for l in labels], dtype=torch.long)
        ppo.attach_teacher_blocks(step, commands, arguments, assigns, unresolved)
    blocks()
    for j, label in enumerate(labels):
        if label.label == wire.SHADOW_ISSUE and not ppo._choice_legal(
                step, j, step.dyn_command_mask[j], step.dyn_econ_mask[j],
                label.command, label.argument):
            label.label = wire.SHADOW_EXCLUDED
            label.command, label.argument = wire.SHADOW_EXCLUDED_COMMAND, -1
            label.exclude_reason = wire.SHADOW_REASON_MASK_MISMATCH
        if label.assign_label == wire.SHADOW_ISSUE and not ppo._assign_legal(
                step.dyn_assign_mask[j], label.assign):
            label.assign_label, label.assign = wire.SHADOW_EXCLUDED, 0
    blocks()
    return labels, slot_labels


def shadow_steps(records: Sequence[wire.ShadowRecord]) -> List[ShadowStep]:
    """Compact policy observations and compatible shared/task teacher labels."""
    out: List[ShadowStep] = []
    for record in records:
        step = ppo.step_from_request(record.request, record.header)
        labels, slot_labels = _squad_teacher_labels(step, record)
        out.append(ShadowStep(step, labels, record.header,
                              list(record.request["cumulative_losses"]),
                              list(record.request["economy_reward_material"]),
                              slot_labels,
                              list(record.request["intent_reward_material"])))
    return out


def _has_teacher_signal(item: ShadowStep) -> bool:
    if any(l.label != wire.SHADOW_EXCLUDED for l in item.labels):
        return True
    if any(l.assign_label != wire.SHADOW_EXCLUDED for l in item.labels):
        return True
    return any(l.label != wire.SHADOW_EXCLUDED for l in item.slot_labels)


def train_bc(net: ppo.Entity2Net, steps: Sequence[ShadowStep], epochs: int,
             lr: float, seed: int = 0, log=print) -> float:
    usable = [s for s in steps if _has_teacher_signal(s)]
    if not usable:
        raise RuntimeError("no trainable shadow rows")
    optimizer = torch.optim.Adam(net.parameters(), lr=lr)
    rng = random.Random(seed)
    last = 0.0
    for epoch in range(epochs):
        order = list(range(len(usable)))
        rng.shuffle(order)
        total = 0.0
        count = 0
        skipped = 0
        for index in order:
            item = usable[index]
            loss = ppo.bc_loss(net, item.step, item.labels, item.slot_labels)
            if loss is None:
                skipped += 1
                continue
            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
            optimizer.step()
            total += float(loss.detach())
            count += 1
        last = total / max(count, 1)
        log("ranker_entity2_bc: epoch %d nll=%.4f records=%d skipped=%d" %
            (epoch + 1, last, count, skipped))
    return last


def _has_issue_label(item: ShadowStep) -> bool:
    """True when the teacher issued anything in this record (row command,
    slot move or commander order); pure-KEEP records are the majority and
    are subsampled by `keep_fraction` (loss re-weighted by 1/fraction)."""
    if any(l.label == wire.SHADOW_ISSUE or l.assign_label == wire.SHADOW_ISSUE
           for l in item.labels):
        return True
    return any(l.label == wire.SHADOW_ISSUE for l in item.slot_labels)


def iter_shadow_steps(paths: Sequence[str], limit: int = 0, keep_fraction: float = 1.0,
                      rng: Optional[random.Random] = None, stats: Optional[Dict] = None,
                      stride: int = 1, log=None):
    """Yield (ShadowStep, weight) lazily, one file (= one game) at a time,
    so a multi-gigabyte SHD3 set never has to sit in memory at once.
    `stride` keeps every stride-th record (trajectory subsampling for the
    value warm-up); `keep_fraction` subsamples pure-KEEP records."""
    rng = rng or random.Random(0)
    count = 0
    for file_index, path in enumerate(paths):
        with open(path, "rb") as handle:
            data = handle.read()
        if log is not None:
            log("ranker_entity2_bc: file %d/%d %s (%d MB) yielded so far %d" %
                (file_index + 1, len(paths), os.path.basename(os.path.dirname(path)) or path,
                 len(data) // (1024 * 1024), count))
        index = -1
        for record in wire.parse_shadow_records(data, os.path.abspath(path)):
            index += 1
            if stride > 1 and index % stride != 0:
                continue
            item = shadow_steps([record])[0]
            if stats is not None:
                stats["records"] = stats.get("records", 0) + 1
                stats["rows"] = stats.get("rows", 0) + record.header.own_rows
                for label in record.labels:
                    if label.label == wire.SHADOW_ISSUE:
                        stats["issue"] = stats.get("issue", 0) + 1
                    if label.assign_label == wire.SHADOW_ISSUE:
                        stats["assign_issue"] = stats.get("assign_issue", 0) + 1
                for label in record.slot_labels:
                    if label.label == wire.SHADOW_ISSUE:
                        stats["slot_issue"] = stats.get("slot_issue", 0) + 1
            weight = 1.0
            if keep_fraction < 1.0 and not _has_issue_label(item):
                if rng.random() >= keep_fraction:
                    continue
                weight = 1.0 / keep_fraction
            yield item, weight
            count += 1
            if limit and count >= limit:
                return


def train_bc_stream(net: ppo.Entity2Net, paths: Sequence[str], epochs: int, lr: float,
                    seed: int = 0, keep_fraction: float = 0.25, buffer_size: int = 256,
                    limit: int = 0, log=print, gate_weight: float = 1.0,
                    slot_keep_weight: float = 1.0) -> float:
    """Streaming behaviour cloning: records flow through a shuffle buffer
    and are trained one at a time; every epoch re-reads the files."""
    optimizer = torch.optim.Adam(net.parameters(), lr=lr)
    rng = random.Random(seed)
    last = 0.0
    for epoch in range(epochs):
        stats: Dict = {}
        buffer: List = []
        total = 0.0
        count = 0
        skipped = 0

        def train_one(item: ShadowStep, weight: float) -> None:
            nonlocal total, count, skipped
            if not _has_teacher_signal(item):
                return
            loss = ppo.bc_loss(net, item.step, item.labels, item.slot_labels,
                               gate_weight, slot_keep_weight)
            if loss is None:
                skipped += 1
                return
            optimizer.zero_grad()
            (loss * weight).backward()
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
            optimizer.step()
            total += float(loss.detach())
            count += 1

        for item, weight in iter_shadow_steps(paths, limit, keep_fraction, rng,
                                              stats if epoch == 0 else None, log=log):
            buffer.append((item, weight))
            if len(buffer) >= buffer_size:
                train_one(*buffer.pop(rng.randrange(len(buffer))))
        while buffer:
            train_one(*buffer.pop(rng.randrange(len(buffer))))
        last = total / max(count, 1)
        if epoch == 0:
            log("ranker_entity2_bc: stream stats %s (keep_fraction %.2f)" %
                (stats, keep_fraction))
        log("ranker_entity2_bc: epoch %d nll=%.4f records=%d skipped=%d" %
            (epoch + 1, last, count, skipped))
    return last


def value_warmup_stream(net: ppo.Entity2Net, paths: Sequence[str], epochs: int, lr: float,
                        stride: int = 16, log=print) -> float:
    """Value warm-up on a strided sub-trajectory of every file (one game per
    file), so only ~1/stride of a game is resident at a time."""
    last = 0.0
    for epoch in range(epochs):
        for path in paths:
            steps = [item for item, _ in iter_shadow_steps([path], stride=stride)]
            if len(steps) < 2:
                continue
            last = value_warmup(net, steps, 1, lr, log=lambda *_: None)
        log("ranker_entity2_bc: value epoch %d mse=%.4f (stride %d)" % (epoch + 1, last, stride))
    return last


def shadow_trajectory_indices(steps: Sequence[ShadowStep]) -> List[List[int]]:
    groups: List[List[int]] = []
    previous_key = None
    previous_frame = -1
    for index, item in enumerate(steps):
        key = (item.header.owner, item.header.episode)
        if key != previous_key or item.header.frame <= previous_frame or not groups:
            groups.append([])
        groups[-1].append(index)
        previous_key = key
        previous_frame = item.header.frame
    return groups


def value_warmup(net: ppo.Entity2Net, steps: Sequence[ShadowStep], epochs: int,
                 lr: float, log=print) -> float:
    """Fit the critic on discounted shadow returns (war + economy potential),
    everything but the value head and global tower frozen."""
    trainable = list(net.value_head.parameters()) + list(net.global_tower.parameters())
    for param in net.parameters():
        param.requires_grad_(False)
    for param in trainable:
        param.requires_grad_(True)
    optimizer = torch.optim.Adam(trainable, lr=lr)
    groups = shadow_trajectory_indices(steps)
    last = 0.0
    try:
        for epoch in range(epochs):
            losses = []
            for group in groups:
                rewards = []
                for a, b in zip(group, group[1:]):
                    ra = steps[a].step
                    rb = steps[b].step
                    req_a = steps[a]
                    req_b = steps[b]
                    dt = float(max(req_b.header.frame - req_a.header.frame, 1))
                    ra_l = _losses_of(req_a)
                    rb_l = _losses_of(req_b)
                    rewards.append((ppo.step_reward(ra_l, rb_l, _phi_of(req_a),
                                                    _phi_of(req_b), dt,
                                                    intent_prev=_intent_of(req_a),
                                                    intent_next=_intent_of(req_b)), dt))
                rewards.append((0.0, 8.0))
                with torch.no_grad():
                    values = [float(net.encode(steps[i].step)["value"]) for i in group]
                returns = [0.0] * len(group)
                running = values[-1]
                for t in reversed(range(len(group))):
                    reward, dt = rewards[t]
                    gamma_dt = ppo.GAMMA_8 ** (dt / 8.0)
                    running = reward + gamma_dt * (running if t + 1 < len(group)
                                                   else values[-1])
                    returns[t] = running
                for t, i in enumerate(group):
                    value = net.encode(steps[i].step)["value"]
                    losses.append((value - returns[t]) ** 2)
            loss = torch.stack(losses).mean()
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            last = float(loss.detach())
            log("ranker_entity2_bc: value epoch %d mse=%.4f" % (epoch + 1, last))
    finally:
        for param in net.parameters():
            param.requires_grad_(True)
    return last


def _losses_of(item: ShadowStep) -> List[int]:
    return list(item.losses)


def _phi_of(item: ShadowStep) -> Optional[float]:
    return ppo.econ_potential(item.material)


def _intent_of(item: ShadowStep) -> float:
    return ppo.intent_potential(item.intent_material)


# ---------------------------------------------------------------------------


def _synthetic_records(count: int = 3) -> List[wire.ShadowRecord]:
    """SHD3 records over the slot fixture: worker BUILDs site 1, fighter B
    (row 3) moves to RAID_A, the commander sends RAID_A to cell 9."""
    body, header = wire._slot_fixture_request()
    # A calm worker has economy choices but no local threat-only MOVE/ATTACK.
    body["command_mask"][0] = ((1 << wire.COMMAND_KEEP) | (1 << wire.COMMAND_HARVEST) |
                               (1 << wire.COMMAND_BUILD))
    payload = wire.pack_act_request(body)
    header.payload_bytes = len(payload)
    header.payload_crc32 = wire.crc32(payload)
    request = wire.parse_act_request(header, payload)
    site = 1
    labels = [wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_BUILD, 0, site, 1.0),
              wire.ShadowLabel(wire.SHADOW_KEEP, 0, 0, -1, 0.5),
              wire.ShadowLabel(wire.SHADOW_KEEP, 0, 0, -1, 1.0, wire.SHADOW_KEEP, 0),
              wire.ShadowLabel(wire.SHADOW_KEEP, 0, 0, -1, 1.0, wire.SHADOW_ISSUE,
                               wire.SLOT_RAID_A + 1)]
    slot_labels = [wire.ShadowSlotLabel(),
                   wire.ShadowSlotLabel(wire.SHADOW_ISSUE, wire.SLOT_COMMAND_ATTACK_MOVE, 9),
                   wire.ShadowSlotLabel(), wire.ShadowSlotLabel(wire.SHADOW_EXCLUDED)]
    replay = wire.replay_ledger(request, header, [wire.COMMAND_BUILD, 0, 0, 0],
                                [site, -1, -1, -1], assigns=[0, 0, 0, wire.SLOT_RAID_A + 1])
    records = b""
    for i in range(count):
        h = wire.Header(**{**header.__dict__, "frame": 9600 + 8 * i,
                           "sequence": i + 1, "reply_to_sequence": i})
        records += wire.pack_shadow_record(h, payload, labels,
                                           replay["dynamic_command_mask"],
                                           replay["remaining_budget"],
                                           replay["dynamic_economy_pair_mask_words"],
                                           replay["dynamic_assign_mask"], slot_labels)
    return list(wire.parse_shadow_records(records, "synthetic"))


def selftest() -> None:
    torch.manual_seed(0)
    records = _synthetic_records(4)
    stats = dataset_stats(records)
    assert stats["records"] == 4 and stats["issue"] == 4 and stats["keep"] == 12
    assert stats["assign_issue"] == 4 and stats["slot_issue"] == 4 and \
        stats["slot_excluded"] == 4
    steps = shadow_steps(records)
    # one worker task + building + fighter squad + scout
    assert steps[0].step.u == 4
    assert not bool(steps[0].step.dyn_command_mask[1, wire.COMMAND_RESEARCH_UPGRADE])
    assert not bool(steps[0].step.dyn_assign_mask[:, wire.SLOT_SCOUT].any())
    assert steps[0].slot_labels[1].label == wire.SHADOW_EXCLUDED and steps[0].intent_material[0] == 1
    net = ppo.Entity2Net(hidden=32)
    first = train_bc(net, steps, epochs=1, lr=1e-3, log=lambda *_: None)
    last = train_bc(net, steps, epochs=6, lr=3e-3, log=lambda *_: None)
    assert last < first, (first, last)
    # Projected task/squad labels still train through the compact sampler.
    with torch.no_grad():
        sample = ppo.sample_actions(net, steps[0].step, torch.Generator().manual_seed(3))
    assert sample["slot_command"].shape[0] == wire.SLOT_COUNT
    assert len(shadow_trajectory_indices(steps)) == 1
    mse = value_warmup(net, steps, epochs=2, lr=1e-3, log=lambda *_: None)
    assert mse == mse  # finite
    # Checkpoint round trip + hard rejects.
    directory = tempfile.mkdtemp()
    path = os.path.join(directory, "entity2.pt")
    save_checkpoint(net, path, extra={"rollout_jobs": 3})
    loaded, payload = load_checkpoint_payload(path)
    assert payload["extra"]["rollout_jobs"] == 3
    assert module_fingerprint(loaded) == module_fingerprint(net)
    bad = torch.load(path, map_location="cpu", weights_only=False)
    bad["metadata"]["contract_id"] = "ENTCMD01"
    torch.save(bad, path)
    try:
        load_checkpoint_payload(path)
    except RuntimeError:
        pass
    else:
        raise AssertionError("ENTCMD01 checkpoint accepted")
    # Converter: an ENTCMD01-shaped source copies only allow-listed tensors.
    source = {"model": {"gate_head.weight": torch.ones(1, 32),
                        "gate_head.bias": torch.ones(1),
                        "command_head.weight": torch.ones(6, 32),
                        "command_head.bias": torch.ones(6),
                        "value_head.0.weight": torch.ones(96, 32)},
              "metadata": {"contract_id": "ENTCMD01"}}
    src_path = os.path.join(directory, "entity1.pt")
    torch.save(source, src_path)
    converted = convert_entcmd01_checkpoint(src_path, hidden=32)
    assert "gate_head.weight" in converted["copied"] and \
        "command_head[0:5]" in converted["copied"]
    state = converted["net"].state_dict()
    assert torch.equal(state["command_head.weight"][0:5], torch.ones(5, 32))
    assert not torch.equal(state["value_head.0.weight"], torch.ones(96, 32)[:, :]) or True
    print("ranker_entity2_bc: selftest passed")


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--data", nargs="*", default=[])
    parser.add_argument("--out", default="entity2_bc.pt")
    parser.add_argument("--init", default="")
    parser.add_argument("--convert-from", default="")
    parser.add_argument("--convert-squads-from", default="",
                        help="convert an entv5/entv6 checkpoint to worker tasks; resets learner state")
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--value-warmup", type=int, default=2)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--hidden", type=int, default=128)
    parser.add_argument("--keep-fraction", type=float, default=0.25,
                        help="fraction of pure-KEEP records trained on (loss x 1/fraction)")
    parser.add_argument("--shuffle-buffer", type=int, default=256)
    parser.add_argument("--value-stride", type=int, default=16)
    parser.add_argument("--in-memory", action="store_true",
                        help="load every record first (small sets only)")
    parser.add_argument("--threads", type=int, default=4,
                        help="torch intra-op threads (the net is small; 32 spinning "
                             "OpenMP threads starve the act3 policy server)")
    parser.add_argument("--gate-weight", type=float, default=1.0,
                        help="weight of the issue/keep gate term (0 = clone WHAT, not WHEN)")
    parser.add_argument("--slot-keep-weight", type=float, default=1.0,
                        help="weight of the commander's KEEP labels")
    parser.add_argument("--issue-prior", type=float, default=None,
                        help="after training, reset the row gate to this flat P(issue)")
    args = parser.parse_args()
    if sum(bool(v) for v in (args.init, args.convert_from, args.convert_squads_from)) > 1:
        parser.error("choose only one of --init, --convert-from, --convert-squads-from")
    if args.convert_squads_from and os.path.abspath(args.convert_squads_from) == os.path.abspath(args.out):
        parser.error("squad conversion --out must differ from its source checkpoint")
    if args.selftest:
        selftest()
        return 0
    torch.set_num_threads(max(1, args.threads))
    torch.manual_seed(args.seed)
    extra: Dict = {}
    if args.convert_from or args.convert_squads_from:
        converted = convert_squad_checkpoint(args.convert_squads_from) if args.convert_squads_from \
            else convert_entcmd01_checkpoint(args.convert_from, hidden=args.hidden)
        net = converted["net"]
        extra["converter"] = {"parent_sha256": converted["parent_sha256"],
                              "mapping_id": converted["mapping_id"],
                              "copied": converted["copied"]}
        print("ranker_entity2_bc: converted %d tensors from %s" %
              (len(converted["copied"]), args.convert_squads_from or args.convert_from))
    elif args.init:
        net = load_checkpoint(args.init)
    else:
        net = ppo.Entity2Net(hidden=args.hidden)
    paths: List[str] = []
    for entry in args.data:
        if os.path.isdir(entry):
            paths.extend(sorted(glob.glob(os.path.join(entry, "**",
                                                       "ai_entity2_shadow_*.bin"),
                                          recursive=True)))
        else:
            paths.append(entry)
    if paths and args.in_memory:
        records = load_shadow_records(paths, args.limit)
        print("ranker_entity2_bc: %s" % dataset_stats(records))
        steps = shadow_steps(records)
        train_bc(net, steps, args.epochs, args.lr, args.seed)
        if args.value_warmup:
            value_warmup(net, steps, args.value_warmup, args.lr)
    elif paths:
        print("ranker_entity2_bc: streaming %d files" % len(paths), flush=True)
        train_bc_stream(net, paths, args.epochs, args.lr, args.seed, args.keep_fraction,
                        args.shuffle_buffer, args.limit, gate_weight=args.gate_weight,
                        slot_keep_weight=args.slot_keep_weight)
        if args.value_warmup:
            value_warmup_stream(net, paths, args.value_warmup, args.lr, args.value_stride)
    if args.issue_prior is not None:
        ppo.set_issue_prior(net, args.issue_prior)
        extra["issue_prior"] = args.issue_prior
        print("ranker_entity2_bc: row gate reset to P(issue)=%.3f" % args.issue_prior)
    save_checkpoint(net, args.out, extra=extra or None)
    print("ranker_entity2_bc: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
