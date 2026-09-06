"""Commander observation contract, autoregressive policy, and safe C++ weight IO.

All maps are channel-major (9, 16, 16), converted from rollout uint8 using /255.
The design's enumerated fields resolve its dimension arithmetic: G70 + T96 +
S63 + A128 + E152 + static19 = 528. Privileged features enter only the critic.
Checkpoints use this same bounded, checksummed tensor format; no pickle is read.
"""
from __future__ import annotations

from collections import OrderedDict
import os
from pathlib import Path
import struct
import tempfile
import zlib

import numpy as np
import torch
from torch import nn

VECTOR_SIZE = 528
MAP_SHAPE = (9, 16, 16)
MAP_SIZE = 2304
PRIVILEGED_SIZE = 32
HEAD_SIZES = (42, 16, 4, 8, 16, 3, 3, 3)
HEAD_OFFSETS = (0, 42, 58, 62, 70, 86, 89, 92)
LOGIT_COUNT = sum(HEAD_SIZES)
ARCHITECTURE = "jw2-commander-conv-ar-v1"
SCHEMA_TEXT = (
    "JW2_COMMANDER_1|G70,T96,S3x21,A16x8,E12x11+4x5,STATIC19|"
    "M9x16x16:NCHW:u8/255|P32:critic_only|"
    "H1:42,H1b:16,H2:4,H3:8,H4:16,H5:3,H6:3,H7:3|"
    "FC528x256x256,CONV9x16k3s1p1,CONV16x32k3s2p1,FC2048x128,"
    "FC384x256,AR8,VALUE288x64x1:relu"
)
SCHEMA_CRC = zlib.crc32(SCHEMA_TEXT.encode("ascii"))
assert SCHEMA_CRC == 0x1F364207
MAGIC = b"JW2CMD01"
FORMAT_VERSION = 1
HEADER = struct.Struct("<8sIIQ8I")
MAX_WEIGHT_BYTES = 4 * 1024 * 1024


def tensor_shapes() -> OrderedDict[str, tuple[int, ...]]:
    """Canonical tensor sequence, identical to TensorSpecs in the C++ loader."""
    result = OrderedDict()

    def layer(name, shape):
        result[name + ".weight"] = shape
        result[name + ".bias"] = (shape[0],)

    layer("vector1", (256, VECTOR_SIZE))
    layer("vector2", (256, 256))
    layer("conv1", (16, 9, 3, 3))
    layer("conv2", (32, 16, 3, 3))
    layer("map_fc", (128, 2048))
    layer("trunk", (256, 384))
    for head, count in enumerate(HEAD_SIZES):
        layer(f"heads.{head}", (count, 256 + 8 * head))
    for head, count in enumerate(HEAD_SIZES[:-1]):
        result[f"embeddings.{head}.weight"] = (count, 8)
    layer("value1", (64, 288))
    layer("value2", (1, 64))
    return result


class CommanderPolicy(nn.Module):
    def __init__(self, weight_version: int = 0):
        super().__init__()
        self.weight_version = int(weight_version)
        self.vector1 = nn.Linear(VECTOR_SIZE, 256)
        self.vector2 = nn.Linear(256, 256)
        self.conv1 = nn.Conv2d(9, 16, 3, padding=1)
        self.conv2 = nn.Conv2d(16, 32, 3, stride=2, padding=1)
        self.map_fc = nn.Linear(2048, 128)
        self.trunk = nn.Linear(384, 256)
        self.heads = nn.ModuleList(nn.Linear(256 + 8 * h, count) for h, count in enumerate(HEAD_SIZES))
        self.embeddings = nn.ModuleList(nn.Embedding(count, 8) for count in HEAD_SIZES[:-1])
        self.value1 = nn.Linear(288, 64)
        self.value2 = nn.Linear(64, 1)

    def _features(self, vector, maps, privileged):
        if vector.ndim != 2 or vector.shape[1] != VECTOR_SIZE:
            raise ValueError("commander vector must have shape [B,528]")
        batch = vector.shape[0]
        if maps.shape not in ((batch, MAP_SIZE), (batch, *MAP_SHAPE)):
            raise ValueError("commander map must have shape [B,9,16,16] or [B,2304]")
        if not torch.isfinite(vector).all() or not torch.isfinite(maps).all():
            raise ValueError("non-finite commander actor input")
        if privileged is None:
            privileged = vector.new_zeros((batch, PRIVILEGED_SIZE))
        if privileged.shape != (batch, PRIVILEGED_SIZE) or not torch.isfinite(privileged).all():
            raise ValueError("invalid commander privileged vector")
        vec = self.vector2(self.vector1(vector).relu()).relu()
        grid = self.conv2(self.conv1(maps.reshape(batch, *MAP_SHAPE)).relu()).relu()
        grid = self.map_fc(grid.flatten(1)).relu()
        trunk = self.trunk(torch.cat((vec, grid), dim=1)).relu()
        value = self.value2(self.value1(torch.cat((trunk, privileged), dim=1)).relu()).squeeze(-1)
        return trunk, value

    def _decide(self, vector, maps, masks, privileged, actions, deterministic, head_mask_callback, generator):
        trunk, value = self._features(vector, maps, privileged)
        batch = vector.shape[0]
        if masks.shape != (batch, LOGIT_COUNT) or not ((masks == 0) | (masks == 1)).all():
            raise ValueError("commander masks must be binary [B,95]")
        if actions is not None:
            if actions.shape != (batch, 8) or actions.dtype not in (torch.int64, torch.int32, torch.uint8):
                raise ValueError("commander actions must be integer [B,8]")
            actions = actions.long()
        prefix = torch.zeros((batch, 8), dtype=torch.long, device=vector.device)
        used_masks = masks.bool().clone()
        conditioned = [trunk]
        logits, logps, entropies = [], [], []
        for head, (offset, count) in enumerate(zip(HEAD_OFFSETS, HEAD_SIZES)):
            if head_mask_callback is not None:
                adjusted = used_masks.clone()
                head_mask_callback(head, prefix.clone(), adjusted)
                used_masks[:, offset:offset + count] = adjusted[:, offset:offset + count]
            if actions is None:
                irrelevant = ((prefix[:, 0] != 12) & (prefix[:, 0] != 14)) if head == 1 else (
                    (prefix[:, 2] == 0) if 3 <= head <= 5 else None)
                if irrelevant is not None:
                    used_masks[irrelevant, offset:offset + count] = False
                    used_masks[irrelevant, offset] = True
            mask = used_masks[:, offset:offset + count].clone()
            if not mask.any(dim=1).all():
                raise ValueError(f"empty commander mask for head {head}")
            raw = self.heads[head](torch.cat(conditioned, dim=1))
            if not torch.isfinite(raw).all():
                raise ValueError("non-finite commander logits")
            masked = raw.masked_fill(~mask, -torch.inf)
            log_prob = masked.log_softmax(dim=-1)
            if actions is not None:
                selected = actions[:, head]
                if ((selected < 0) | (selected >= count)).any() or not mask.gather(1, selected[:, None]).all():
                    raise ValueError(f"illegal commander action for head {head}")
            elif deterministic:
                selected = masked.argmax(dim=-1)
            else:
                selected = torch.multinomial(log_prob.exp(), 1, generator=generator).squeeze(-1)
            prefix[:, head] = selected
            # Clone prevents later prefix writes from invalidating embedding backward.
            if head < 7:
                conditioned.append(self.embeddings[head](selected.clone()))
            singleton = mask.sum(dim=-1) == 1
            logps.append(torch.where(singleton, 0.0, log_prob.gather(1, selected[:, None]).squeeze(-1)))
            finite_log_prob = log_prob.masked_fill(~mask, 0.0)
            entropies.append(torch.where(singleton, 0.0, -(log_prob.exp() * finite_log_prob).sum(-1)))
            logits.append(raw)
        if not torch.isfinite(value).all():
            raise ValueError("non-finite commander value")
        return {"action": prefix, "mask": used_masks, "logp": torch.stack(logps, 1),
                "entropy": torch.stack(entropies, 1), "logits": torch.cat(logits, 1), "value": value}

    def evaluate(self, vector, maps, actions, masks, privileged=None):
        """Evaluate recorded prefixes using the exact conditional masks in RLO1."""
        return self._decide(vector, maps, masks, privileged, actions, False, None, None)

    def sample(self, vector, maps, masks, privileged=None, deterministic=False,
               head_mask_callback=None, generator=None):
        return self._decide(vector, maps, masks, privileged, None, deterministic, head_mask_callback, generator)

    def forward(self, vector, maps, actions, masks, privileged=None):
        return self.evaluate(vector, maps, actions, masks, privileged)

    def export(self, path, version=None):
        export_weights(self, path, self.weight_version if version is None else version)

    @classmethod
    def load(cls, path):
        return load_weights(path)


def export_weights(policy: CommanderPolicy, path, version: int | None = None):
    """Write a complete checksummed version, then atomically replace destination."""
    version = policy.weight_version if version is None else int(version)
    if not 0 <= version <= 0xFFFFFFFF:
        raise ValueError("weight version must fit u32")
    state = policy.state_dict()
    specs = tensor_shapes()
    if set(state) != set(specs):
        raise ValueError("commander tensor names mismatch")
    payload = bytearray()
    for name, shape in specs.items():
        values = state[name].detach().to(device="cpu", dtype=torch.float32).contiguous().numpy()
        if tuple(values.shape) != shape or not np.isfinite(values).all():
            raise ValueError(f"invalid commander tensor {name}")
        encoded = name.encode("ascii")
        raw = values.astype("<f4", copy=False).tobytes(order="C")
        payload += struct.pack("<HH", len(encoded), len(shape))
        payload += struct.pack("<" + "I" * len(shape), *shape)
        payload += struct.pack("<I", len(raw)) + encoded + raw
    header = HEADER.pack(MAGIC, FORMAT_VERSION, version, SCHEMA_CRC, VECTOR_SIZE,
                         MAP_SIZE, PRIVILEGED_SIZE, 8, LOGIT_COUNT, len(specs), len(payload), zlib.crc32(payload))
    if len(header) + len(payload) > MAX_WEIGHT_BYTES:
        raise ValueError("commander weights exceed format bound")
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix=destination.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(header)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def load_weights(path) -> CommanderPolicy:
    path = Path(path)
    with path.open("rb") as stream:
        # A bounded read also protects against a file growing after stat().
        raw = stream.read(MAX_WEIGHT_BYTES + 1)
    if not HEADER.size <= len(raw) <= MAX_WEIGHT_BYTES:
        raise ValueError("invalid commander weight file size")
    magic, fmt, version, schema, vector, maps, private, heads, logits, count, size, crc = HEADER.unpack_from(raw)
    specs = tensor_shapes()
    if (magic, fmt, schema, vector, maps, private, heads, logits, count) != (
            MAGIC, FORMAT_VERSION, SCHEMA_CRC, VECTOR_SIZE, MAP_SIZE, PRIVILEGED_SIZE, 8, LOGIT_COUNT, len(specs)):
        raise ValueError("commander weight schema mismatch")
    payload = memoryview(raw)[HEADER.size:]
    if len(payload) != size or zlib.crc32(payload) != crc:
        raise ValueError("commander weight size/CRC mismatch")
    position = 0
    state = OrderedDict()
    for name, shape in specs.items():
        if position + 4 > size:
            raise ValueError("truncated commander tensor")
        name_len, rank = struct.unpack_from("<HH", payload, position)
        position += 4
        if name_len != len(name) or rank != len(shape) or position + 4 * rank + 4 + name_len > size:
            raise ValueError("commander tensor metadata mismatch")
        dimensions = struct.unpack_from("<" + "I" * rank, payload, position)
        position += 4 * rank
        byte_count, = struct.unpack_from("<I", payload, position)
        position += 4
        tensor_name = bytes(payload[position:position + name_len])
        position += name_len
        if dimensions != shape or tensor_name != name.encode("ascii") or byte_count != int(np.prod(shape)) * 4:
            raise ValueError("commander tensor name/shape/size mismatch")
        if position + byte_count > size:
            raise ValueError("truncated commander tensor values")
        values = np.frombuffer(payload[position:position + byte_count], dtype="<f4").reshape(shape).copy()
        position += byte_count
        if not np.isfinite(values).all():
            raise ValueError("non-finite commander weights")
        state[name] = torch.from_numpy(values)
    if position != size:
        raise ValueError("trailing commander weight data")
    policy = CommanderPolicy(version)
    policy.load_state_dict(state, strict=True)
    return policy


# Checkpoints deliberately use the deployment tensor format, never torch pickle.
save_checkpoint = export_weights
load_checkpoint = load_weights


class CommanderPcg32:
    """Bit-exact reference for the independent C++ rollout generator."""
    def __init__(self, seed=0, owner=0, weight_version=0):
        self.state = 0
        self.next()
        self.state = (self.state + seed * 7919 + owner * 31 + weight_version) & ((1 << 64) - 1)
        self.next()

    def next(self):
        old = self.state
        self.state = (old * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
        shifted = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rotation = old >> 59
        return ((shifted >> rotation) | (shifted << ((-rotation) & 31))) & 0xFFFFFFFF

    def uniform(self):
        return self.next() / 4294967296.0
