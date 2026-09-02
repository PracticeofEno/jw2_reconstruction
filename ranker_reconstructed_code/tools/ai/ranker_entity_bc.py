# -*- coding: utf-8 -*-
"""Shadow behavior-cloning + value warmup for the entity policy
(plan sections 12 / 13.1 / 13.2 steps 2-3).

Input: -AISHADOW dataset files (ai_entity_shadow_<owner>.bin, SHD1 records).
Output: a checkpoint whose metadata pins the full act2 contract; any
mismatch at load time is a hard error, never adapted to (plan section 12).

Usage:
    python ranker_entity_bc.py --data shadow_dir_or_files --out entity_bc.pt
        [--epochs 4] [--lr 3e-4] [--value-warmup 2] [--limit N]
    python ranker_entity_bc.py --selftest
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import glob
import os
import random
import struct
import sys
import tempfile
from typing import Dict, List, Tuple

import torch

import ranker_entity_contract as wire
import ranker_entity_ppo as ppo

CHECKPOINT_METADATA = {
    "contract_id": "ENTCMD01",
    "protocol": wire.PROTOCOL,
    "observation_schema_version": 5,
    "global_feature_version": 10,
    "global_feature_count": wire.GLOBAL_COUNT,
    "entity_feature_version": 1,
    "entity_action_version": 1,
    "semantic_action_version": 2,
    "macro_action_count": wire.MACRO_ACTION_COUNT,
    "own_continuous_count": wire.OWN_CONTINUOUS_COUNT,
    "target_continuous_count": wire.TARGET_CONTINUOUS_COUNT,
    "command_count": wire.COMMAND_COUNT,
    "point_count": wire.POINT_COUNT,
    "point_geometry_version": 1,
    "wire_entity_hard_limit": wire.WIRE_ROW_LIMIT,
    "normalization_id": "entnorm1",
    "architecture_id": ppo.ARCHITECTURE_ID,
    "gamma_8": ppo.GAMMA_8,
    "lambda_8": ppo.LAMBDA_8,
    "reward": {"war_scale": 5.0, "loss_weight": 1.0, "econ_scale": 2.0,
               "econ_cap": 8000.0, "terminal_scale": 6.0,
               "approach_weight": 0.0},
    "teacher_point_max_error_px": 64,
}


@dataclass(frozen=True)
class ShadowRecord:
    source: str
    header: wire.Header
    request: Dict
    labels: List

    def __iter__(self):
        # Keep existing BC loops/source callers compatible with 3-tuples.
        return iter((self.header, self.request, self.labels))

    def __getitem__(self, index):
        return (self.header, self.request, self.labels)[index]


def _record_source(record) -> str:
    return record.source if isinstance(record, ShadowRecord) else "<legacy>"


def validate_checkpoint_metadata(metadata: Dict) -> None:
    """Hard-fail on any contract mismatch (plan section 12)."""
    for key, expected in CHECKPOINT_METADATA.items():
        if metadata.get(key) != expected:
            raise RuntimeError(
                "checkpoint contract mismatch: %s=%r expected %r" %
                (key, metadata.get(key), expected))


def save_checkpoint(net: ppo.EntityNet, path: str,
                    extra: Dict = None) -> None:
    payload = {"model": net.state_dict(),
               "metadata": dict(CHECKPOINT_METADATA),
               "hidden": net.hidden}
    if extra:
        payload["extra"] = extra
    absolute = os.path.abspath(path)
    directory = os.path.dirname(absolute)
    os.makedirs(directory, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=".%s." % os.path.basename(absolute), suffix=".tmp",
        dir=directory)
    os.close(descriptor)
    try:
        torch.save(payload, temporary)
        # One learner publishes the new generation atomically.  Readers see
        # either the previous complete checkpoint or the new complete one.
        os.replace(temporary, absolute)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def load_checkpoint_payload(path: str) -> Tuple[ppo.EntityNet, Dict]:
    payload = torch.load(path, map_location="cpu", weights_only=False)
    validate_checkpoint_metadata(payload.get("metadata", {}))
    net = ppo.EntityNet(hidden=payload.get("hidden", 128))
    net.load_state_dict(payload["model"])
    return net, payload


def load_checkpoint(path: str) -> ppo.EntityNet:
    net, _ = load_checkpoint_payload(path)
    return net


def load_shadow_records(paths: List[str], limit: int = 0
                        ) -> List[ShadowRecord]:
    records = []
    for path in paths:
        source = os.path.abspath(path)
        data = open(path, "rb").read()
        for header, request, labels in wire.parse_shadow_records(data):
            records.append(ShadowRecord(source, header, request, labels))
            if limit and len(records) >= limit:
                return records
    return records


def dataset_stats(records) -> Dict:
    keep = issue = excluded = rows = 0
    reasons = {}
    for _, _, labels in records:
        for label in labels:
            rows += 1
            if label.label == wire.SHADOW_KEEP:
                keep += 1
            elif label.label == wire.SHADOW_ISSUE:
                issue += 1
            else:
                excluded += 1
                reasons[label.exclude_reason] = \
                    reasons.get(label.exclude_reason, 0) + 1
    return {"records": len(records), "rows": rows, "keep": keep,
            "issue": issue, "excluded": excluded,
            "exclude_reasons": reasons}


def train_bc(net: ppo.EntityNet, records, epochs: int, lr: float,
             seed: int = 0, log=print) -> float:
    optimizer = torch.optim.Adam(net.parameters(), lr=lr)
    rng = random.Random(seed)
    last_mean = float("nan")
    trainable = [r for r in records
                 if r[0].own_rows > 0 and any(
                     lb.label != wire.SHADOW_EXCLUDED for lb in r[2])]
    if not trainable:
        raise RuntimeError("no trainable shadow rows in the dataset")
    for epoch in range(epochs):
        rng.shuffle(trainable)
        total = 0.0
        count = 0
        for header, request, labels in trainable:
            step = ppo.step_from_request(request, header)
            loss = ppo.bc_loss(net, step, labels)
            if not loss.requires_grad:
                continue
            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
            optimizer.step()
            total += float(loss.detach())
            count += 1
        last_mean = total / max(count, 1)
        log("bc epoch %d: mean nll %.4f over %d records" %
            (epoch + 1, last_mean, count))
    return last_mean


def shadow_trajectory_indices(records) -> List[List[int]]:
    """Contiguous source/episode/owner streams; frame reset starts a run."""
    trajectories = []
    current = []
    current_key = None
    previous_frame = None
    for index, record in enumerate(records):
        header = record[0]
        key = (_record_source(record), header.episode, header.owner)
        if (current and (key != current_key or
                         header.frame <= previous_frame)):
            trajectories.append(current)
            current = []
        current.append(index)
        current_key = key
        previous_frame = header.frame
    if current:
        trajectories.append(current)
    return trajectories


def shadow_rewards(records) -> List[float]:
    """Per-transition war-score reward (plan 10.4, entity head: war only)
    from the records' raw u64 loss material; forward-looking like the act2
    seal semantics."""
    rewards = [0.0] * len(records)
    for trajectory in shadow_trajectory_indices(records):
        for index, next_index in zip(trajectory, trajectory[1:]):
            losses_now = records[index][1]["cumulative_losses"]
            losses_next = records[next_index][1]["cumulative_losses"]
            own_delta = (losses_next[0] - losses_now[0]) + \
                (losses_next[1] - losses_now[1])
            hostile_delta = (losses_next[2] - losses_now[2]) + \
                (losses_next[3] - losses_now[3])
            rewards[index] = 5.0 * (hostile_delta - own_delta) / 1000.0
        # Each trajectory tail stays 0: there is no same-stream next state.
    return rewards


def value_warmup(net: ppo.EntityNet, records, epochs: int, lr: float,
                 log=print) -> float:
    """Actor-frozen value-only warmup (plan 13.2 step 3) on the shadow
    stream's war rewards; the tail bootstraps its own value (truncated)."""
    value_params = list(net.value_head.parameters()) + \
        list(net.global_tower.parameters())
    actor_params = [p for p in net.parameters()
                    if not any(p is q for q in value_params)]
    for param in actor_params:
        param.requires_grad_(False)
    optimizer = torch.optim.Adam(value_params, lr=lr)
    rewards = shadow_rewards(records)
    frames = [header.frame for header, _, _ in records]
    trajectories = shadow_trajectory_indices(records)
    last_loss = float("nan")
    for epoch in range(epochs):
        values = []
        for header, request, _ in records:
            values.append(net(ppo.step_from_request(request, header))
                          ["value"])
        values_tensor = torch.stack(values)
        with torch.no_grad():
            returns = torch.zeros(len(records))
            for trajectory in trajectories:
                tail = trajectory[-1]
                # Unsealed shadow tail bootstraps itself; it is not a
                # transition into the next file/episode/owner.
                next_return = float(values_tensor[tail])
                returns[tail] = next_return
                for position in range(len(trajectory) - 2, -1, -1):
                    index = trajectory[position]
                    next_index = trajectory[position + 1]
                    dt = frames[next_index] - frames[index]
                    gamma_dt = ppo.GAMMA_8 ** (dt / 8.0)
                    next_return = rewards[index] + gamma_dt * next_return
                    returns[index] = next_return
        loss = torch.nn.functional.mse_loss(values_tensor, returns)
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()
        last_loss = float(loss.detach())
        log("value warmup epoch %d: mse %.5f" % (epoch + 1, last_loss))
    for param in actor_params:
        param.requires_grad_(True)
    return last_loss


# ---------------------------------------------------------------------------


def _synthetic_shadow_record(u: int, e: int, frame: int, issue_row: int
                             ) -> bytes:
    body = {
        "global": [0.0] * wire.GLOBAL_COUNT,
        "macro_gate": [0.0, 0.0],
        "macro_mask_words": [1, 0, 0],
        "cumulative_losses": [0, 0, 100 * frame, 0],
        "own_id": [0x1D0 * (i + 1) for i in range(u)],
        "own_generation": [1] * u, "own_control_epoch": [1] * u,
        "own_type_id": [5] * u, "own_movement_class": [0] * u,
        "own_distance_check_mode": [0] * u, "own_role": [0] * u,
        "own_render_class": [1] * u, "own_command_base": [0] * u,
        "own_command_state_high_flags": [0] * u,
        "own_unit_command_flags": [0] * u, "own_movement_state": [0] * u,
        "own_semantic_order": [0] * u, "own_order_status": [0] * u,
        "own_presence_bits": [3] * u, "own_engine_order_match": [0] * u,
        "own_last_attempt_command": [255] * u,
        "own_last_attempt_result": [255] * u,
        "own_last_reject_code": [0] * u,
        "own_active_target_row": [-1] * u,
        "own_attackable_class_mask": [0xFFFFFFFF] * u,
        "own_feature": [[0.1] * wire.OWN_CONTINUOUS_COUNT] * u,
        "command_mask": [0x7F] * u,
        "point_mask": [[0xFFFFFFFF] * 3] * u,
        "target_id": [0x5D0 + i for i in range(e)],
        "target_generation": [1] * e, "target_type_id": [5] * e,
        "target_owner": [1] * e, "target_role": [0] * e,
        "target_render_class": [1] * e, "target_kind_bits": [1] * e,
        "target_feature": [[0.2] * wire.TARGET_CONTINUOUS_COUNT] * e,
        "attack_pair_mask_words": [[(1 << e) - 1] for _ in range(u)],
    }
    payload = wire.pack_act_request(body)
    header = wire.Header(kind=wire.KIND_ACT_REQ, own_rows=u, target_rows=e,
                         frame=frame, payload_bytes=len(payload),
                         payload_crc32=wire.crc32(payload))
    labels = []
    for row in range(u):
        if row == issue_row:
            labels.append(struct.pack("<BBHiif", wire.SHADOW_ISSUE, 1, 0,
                                      row % wire.POINT_COUNT, -1, 1.0))
        else:
            labels.append(struct.pack("<BBHiif", wire.SHADOW_KEEP, 0, 0,
                                      -1, -1, 1.0))
    label_block = struct.pack("<I", u) + b"".join(labels)
    record_body = wire.pack_header(header) + payload + label_block
    return wire.SHADOW_MAGIC + struct.pack("<I", len(record_body)) + \
        record_body


def selftest() -> None:
    torch.manual_seed(0)
    # Build a small synthetic dataset, train, and require the NLL to drop.
    blob = b"".join(_synthetic_shadow_record(3, 2, frame=8 * i,
                                             issue_row=i % 3)
                    for i in range(12))
    records = list(wire.parse_shadow_records(blob))
    assert len(records) == 12
    sourced = [ShadowRecord("worker-a" if index < 6 else "worker-b",
                            header, request, labels)
               for index, (header, request, labels) in enumerate(records)]
    rewards = shadow_rewards(sourced)
    assert len(shadow_trajectory_indices(sourced)) == 2
    assert rewards[4] > 0.0 and rewards[5] == 0.0 and rewards[6] > 0.0, \
        "shadow reward crossed a worker/source boundary"
    stats = dataset_stats(records)
    assert stats["rows"] == 36 and stats["issue"] == 12
    net = ppo.EntityNet(hidden=64)
    first = train_bc(net, records, epochs=1, lr=3e-4, log=lambda *_: None)
    last = train_bc(net, records, epochs=6, lr=3e-4, log=lambda *_: None)
    assert last < first, "BC loss did not decrease (%.4f -> %.4f)" % (
        first, last)
    warm = value_warmup(net, records, epochs=3, lr=1e-3,
                        log=lambda *_: None)
    assert warm == warm and warm < float("inf")
    # Checkpoint contract round trip + hard rejection.
    path = os.path.join(os.environ.get("TEMP", "."),
                        "entity_bc_selftest.pt")
    save_checkpoint(net, path)
    load_checkpoint(path)
    payload = torch.load(path, map_location="cpu", weights_only=False)
    payload["metadata"]["global_feature_version"] = 9
    torch.save(payload, path)
    try:
        load_checkpoint(path)
    except RuntimeError:
        pass
    else:
        raise AssertionError("stale checkpoint contract was accepted")
    os.remove(path)
    print("ranker_entity_bc: selftest passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--data", nargs="*", default=[],
                        help="shadow .bin files or directories")
    parser.add_argument("--out", default="entity_bc.pt")
    parser.add_argument("--init", default="",
                        help="warm-start checkpoint (contract-checked)")
    parser.add_argument("--epochs", type=int, default=4)
    parser.add_argument("--value-warmup", type=int, default=2)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--seed", type=int, default=0)
    arguments = parser.parse_args()
    if arguments.selftest:
        selftest()
        return 0
    paths = []
    for entry in arguments.data:
        if os.path.isdir(entry):
            paths.extend(sorted(glob.glob(
                os.path.join(entry, "ai_entity_shadow_*.bin"))))
        else:
            paths.append(entry)
    if not paths:
        parser.error("--data with at least one file/directory required")
    records = load_shadow_records(paths, arguments.limit)
    print("dataset:", dataset_stats(records), flush=True)
    torch.manual_seed(arguments.seed)
    net = load_checkpoint(arguments.init) if arguments.init else \
        ppo.EntityNet()
    train_bc(net, records, arguments.epochs, arguments.lr,
             seed=arguments.seed)
    if arguments.value_warmup > 0:
        value_warmup(net, records, arguments.value_warmup, arguments.lr)
    save_checkpoint(net, arguments.out,
                    extra={"stats": dataset_stats(records)})
    print("checkpoint saved ->", arguments.out, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
