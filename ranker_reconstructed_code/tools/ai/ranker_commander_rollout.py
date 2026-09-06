"""Strict RLO1 interchange and time-aware returns for the in-process commander.

All fields are little endian, packed without padding. A terminal state is a
separate final record. An unfinished/crashed file is never a training episode.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
import zlib

import numpy as np

MAGIC = b"JWRLO001"
SCHEMA_CRC = 0x1F364207
FORMAT_VERSION = 2
VECTOR_SIZE = 528
MAP_SHAPE = (9, 16, 16)
MAP_SIZE = 2304
PRIVILEGED_SIZE = 32
HEAD_SIZES = (42, 16, 4, 8, 16, 3, 3, 3)
HEAD_OFFSETS = tuple(np.cumsum((0,) + HEAD_SIZES[:-1]).tolist())
MASK_SIZE = sum(HEAD_SIZES)
HEADER = struct.Struct("<8s8I")
DECISION, WIN, LOSS, TRUNCATED, INVALID = range(5)
def _record_dtype(compact: bool) -> np.dtype:
    return np.dtype([
    ("frame", "<u4"), ("delta_frame", "<u2"), ("event", "u1"), ("teacher", "u1"),
    ("status", "u1"), ("reserved", "u1"),
    ("weight_version", "<u4"),
    ("vector", "<f2" if compact else "<f4", (VECTOR_SIZE,)), ("map", "u1", (MAP_SIZE,)),
    ("mask_packed" if compact else "mask", "u1", (12 if compact else MASK_SIZE,)),
    ("action", "u1", (8,)),
    ("logp", "<f4", (8,)), ("value", "<f4"),
    ("potential", "<f4", (4,)), ("terminal_reward", "<f4"), ("reserved_reward", "<f4"),
    ("privileged", "<f2" if compact else "<f4", (PRIVILEGED_SIZE,)),
    ("crc32", "<u4"),
], align=False)


WIRE_RECORD_DTYPE = _record_dtype(True)
RECORD_DTYPE = _record_dtype(False)  # decoded compatibility view / fixture input
RECORD_SIZE = WIRE_RECORD_DTYPE.itemsize
assert RECORD_SIZE == 3522


class DecodedRecords:
    """Lazy fields over compact memmap; only requested fields/batches expand.

    Existing training consumers see float32 vector/private and unpacked mask95.
    np.asarray/concatenate materializes a requested batch in RECORD_DTYPE.
    """
    dtype = RECORD_DTYPE

    def __init__(self, raw):
        self.raw = raw

    @property
    def shape(self):
        return self.raw.shape

    @property
    def _mmap(self):
        return self.raw._mmap

    def __len__(self):
        return len(self.raw)

    def __getitem__(self, key):
        if isinstance(key, str):
            if key in ("vector", "privileged"):
                return self.raw[key].astype(np.float32)
            if key == "mask":
                return np.unpackbits(self.raw["mask_packed"], axis=-1, bitorder="little")[..., :MASK_SIZE]
            return self.raw[key]
        raw = self.raw[key]
        if isinstance(raw, np.void):
            return np.asarray(DecodedRecords(np.asarray(raw).reshape(1)))[0]
        return DecodedRecords(raw)

    def __iter__(self):
        for index in range(len(self)):
            yield self[index]

    def __array__(self, dtype=None, copy=None):
        result = np.empty(self.shape, dtype=RECORD_DTYPE)
        for name in RECORD_DTYPE.names:
            result[name] = self[name]
        return result.astype(dtype, copy=False) if dtype is not None else result

    def copy(self):
        return np.asarray(self)


class RolloutError(ValueError):
    """A rollout cannot safely enter this training cohort."""


@dataclass(frozen=True)
class Episode:
    path: Path
    owner: int
    seed: int
    weight_version: int
    records: DecodedRecords

    @property
    def raw_records(self) -> np.ndarray:
        return getattr(self.records, "raw", self.records)

    def close(self):
        mapping = getattr(self.records, "_mmap", None)
        if mapping is not None:
            mapping.close()

    @property
    def decisions(self) -> np.ndarray:
        return self.records[:-1]

    @property
    def terminal(self) -> np.void:
        return self.records[-1]


def _validate_records(records: np.ndarray) -> None:
    if len(records) < 2:
        raise RolloutError("episode requires a decision and terminal state")
    if np.any(records["status"][:-1] != DECISION):
        raise RolloutError("non-decision or invalid record inside episode")
    if int(records[-1]["status"]) not in (WIN, LOSS, TRUNCATED):
        raise RolloutError("episode has no valid terminal state")
    if np.any(records["reserved"] != 0) or np.any(records["teacher"] > 1):
        raise RolloutError("unsupported record flags")
    if np.any(records["teacher"] != records[0]["teacher"]):
        raise RolloutError("teacher flag changed during episode")
    frames = records["frame"].astype(np.int64)
    if np.any(np.diff(frames) <= 0):
        raise RolloutError("decision and terminal frames must increase")
    if frames[-1] > 60000:
        raise RolloutError("episode exceeds the 60000-frame design horizon")
    for name in ("vector", "logp", "value", "potential", "privileged", "terminal_reward", "reserved_reward"):
        if not np.isfinite(records[name]).all():
            raise RolloutError(f"nonfinite {name}")
    elapsed = np.diff(np.concatenate(([0], frames)))
    if np.any(elapsed > 65535) or np.any(records["delta_frame"] != elapsed):
        raise RolloutError("stored delta_frame does not match frame interval")
    expected_reward = terminal_rewards(records["frame"], records["status"])
    if np.any(records["reserved_reward"] != 0) or not np.allclose(
            records["terminal_reward"], expected_reward, rtol=0, atol=2e-7):
        raise RolloutError("invalid terminal/reserved reward component")
    if np.any(records["mask"] > 1):
        raise RolloutError("mask values must be 0 or 1")
    if np.any(records["logp"][:-1] > 1e-5):
        raise RolloutError("positive sampled log probability")
    limits = np.array([0.25, 0.10, 0.05, 0.05], dtype=np.float32)
    if np.any(np.abs(records["potential"]) > limits + 1e-6):
        raise RolloutError("potential components exceed their defined bounds")
    if np.any(records["potential"][:, 1:] < -1e-6):
        raise RolloutError("noncombat potential components must be nonnegative")
    decision = records[:-1]
    for head, (offset, size) in enumerate(zip(HEAD_OFFSETS, HEAD_SIZES)):
        chosen = decision["action"][:, head].astype(np.int64)
        if np.any(chosen >= size):
            raise RolloutError(f"head {head} action out of range")
        if not np.all(decision["mask"][np.arange(len(decision)), offset + chosen]):
            raise RolloutError(f"head {head} sampled a masked action")


def read_rollout(path: str | Path, *, current_version: int | None = None,
                 teacher: bool | None = None) -> Episode:
    path = Path(path)
    size = path.stat().st_size
    with path.open("rb") as stream:
        raw = stream.read(HEADER.size)
    if len(raw) != HEADER.size:
        raise RolloutError("truncated header")
    magic, schema, version, owner, seed, weight, vector, maps, record_size = HEADER.unpack(raw)
    if (magic, schema, version, vector, maps, record_size) != (
            MAGIC, SCHEMA_CRC, FORMAT_VERSION, VECTOR_SIZE, MAP_SIZE, RECORD_SIZE):
        raise RolloutError("RLO1 schema, compact format version 2, or dimensions mismatch")
    if owner >= 8 or seed == 0:
        raise RolloutError("invalid owner or zero policy seed")
    if current_version is not None and not 0 <= current_version - weight <= 1:
        raise RolloutError("policy version must be current or one generation old")
    payload_size = size - HEADER.size
    if payload_size <= 0 or payload_size % RECORD_SIZE:
        raise RolloutError("partial or empty record payload")
    raw_records = np.memmap(path, dtype=WIRE_RECORD_DTYPE, mode="r", offset=HEADER.size,
                        shape=(payload_size // RECORD_SIZE,))
    try:
        for index, record in enumerate(raw_records):
            if zlib.crc32(record.tobytes()[:-4]) != int(record["crc32"]):
                raise RolloutError(f"record {index} CRC32 mismatch")
        if np.any(raw_records["weight_version"] != weight):
            raise RolloutError("record policy version differs from pinned header")
        if np.any(raw_records["mask_packed"][:, -1] & 0x80):
            raise RolloutError("reserved 96th mask bit must be zero")
        records = DecodedRecords(raw_records)
        _validate_records(records)
        if teacher is not None and bool(records[0]["teacher"]) != teacher:
            raise RolloutError("teacher/policy cohort mismatch")
    except BaseException:
        raw_records._mmap.close()
        raise
    return Episode(path, owner, seed, weight, records)


LABEL_MAGIC = b"JWTL0001"
LABEL_RECORD = np.dtype([("mask_packed", "u1", (12,)), ("action", "u1", (8,))], align=False)


def read_teacher_labels(path: str | Path):
    """Return (actions u8[n,8], masks u8[n,95]) from <rollout>.teacher.bin.

    Written under -AIDAGGER: one record per RLO record (terminal = zeros).
    """
    path = Path(path)
    raw = path.read_bytes()
    if len(raw) < len(LABEL_MAGIC) or raw[:len(LABEL_MAGIC)] != LABEL_MAGIC:
        raise RolloutError("teacher label file magic mismatch")
    payload = raw[len(LABEL_MAGIC):]
    if len(payload) % LABEL_RECORD.itemsize:
        raise RolloutError("partial teacher label payload")
    records = np.frombuffer(payload, dtype=LABEL_RECORD)
    masks = np.unpackbits(records["mask_packed"], axis=-1, bitorder="little")[:, :MASK_SIZE]
    return records["action"].copy(), masks.astype(np.uint8)


def relabel_with_teacher(episode: Episode) -> Episode:
    """DAgger: replace the policy's sampled actions/masks by the rule
    commander's labels for the same observations, yielding a teacher-cohort
    episode over the states the policy actually visited."""
    actions, masks = read_teacher_labels(str(episode.path) + ".teacher.bin")
    records = np.asarray(episode.records)
    if len(actions) != len(records):
        raise RolloutError("teacher label count does not match rollout records")
    decisions = len(records) - 1
    records["action"][:decisions] = actions[:decisions]
    records["mask"][:decisions] = masks[:decisions]
    records["teacher"] = 1
    records["logp"][:decisions] = 0.0
    _validate_records(records)
    return Episode(episode.path, episode.owner, episode.seed, episode.weight_version, records)


def terminal_rewards(frames, statuses) -> np.ndarray:
    frames = np.asarray(frames, dtype=np.float32)
    statuses = np.asarray(statuses)
    return np.where(statuses == WIN,
                    np.float32(1) + np.float32(.3) * (np.float32(1) - frames / np.float32(60000)),
                    np.where(statuses == LOSS, np.float32(-1), np.float32(0))).astype(np.float32)


def write_rollout(path: str | Path, records: np.ndarray, *, owner: int = 1,
                  seed: int = 1, weight_version: int = 0) -> Path:
    """Reference writer used by collection tools and binary parity fixtures."""
    records = np.array(records, dtype=RECORD_DTYPE, copy=True)
    if not 0 <= owner < 8 or not 1 <= seed <= 0xFFFFFFFF or not 0 <= weight_version <= 0xFFFFFFFF:
        raise RolloutError("invalid owner or seed")
    intervals = np.diff(np.concatenate(([0], records["frame"].astype(np.int64))))
    if np.any(intervals < 0) or np.any(intervals > 65535):
        raise RolloutError("frame interval outside u16 range")
    records["delta_frame"] = intervals
    records["weight_version"] = weight_version
    records["terminal_reward"] = terminal_rewards(records["frame"], records["status"])
    records["reserved_reward"] = 0
    _validate_records(records)
    wire = np.empty(len(records), dtype=WIRE_RECORD_DTYPE)
    with np.errstate(over="ignore", invalid="ignore"):
        for name in WIRE_RECORD_DTYPE.names:
            if name == "mask_packed":
                wire[name] = np.packbits(records["mask"], axis=-1, bitorder="little")
            else:
                wire[name] = records[name]
    if not np.isfinite(wire["vector"]).all() or not np.isfinite(wire["privileged"]).all():
        raise RolloutError("commander observation exceeds finite binary16 range")
    for index in range(len(wire)):
        wire[index]["crc32"] = zlib.crc32(wire[index].tobytes()[:-4])
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(HEADER.pack(MAGIC, SCHEMA_CRC, FORMAT_VERSION, owner, seed,
                                 weight_version, VECTOR_SIZE, MAP_SIZE, RECORD_SIZE))
        stream.write(wire.tobytes())
    return path


def potential_components(killed: float, lost: float, gathered: float,
                          research: float, hq: float) -> np.ndarray:
    return np.array([0.25 * np.tanh((killed - lost) / 4000.0),
                     0.10 * np.clip(gathered / 30000.0, 0.0, 1.0),
                     0.05 * np.tanh(max(0.0, research) / 8.0),
                     0.05 * np.clip(hq - 1.0, 0.0, 2.0) / 2.0], dtype=np.float32)


def episode_returns(episode: Episode, *, iteration: int = 0,
                    shaping_scale: float = 1.0, gamma: float = 0.997,
                    gae_lambda: float = 0.95) -> dict[str, np.ndarray]:
    """Reward decomposition, elapsed-frame GAE, and BC Monte Carlo targets.

    Truncation bootstraps V(s_T) without a verdict. The potential is zeroed at
    the last record of every episode, truncated or not, so the shaping sum
    telescopes to -Phi(s_0) whatever the path: a policy cannot bank the
    kill/economy potential (up to ~+0.4) by holding a favourable position
    until the frame cap instead of finishing the game. Real terminal states
    also have zero value. There is no event/issue/time bonus.
    """
    records = episode.records
    frames = records["frame"].astype(np.float64)
    elapsed = np.diff(frames) / 32.0
    discounts = gamma ** elapsed
    lambdas = gae_lambda ** elapsed
    potential = records["potential"].astype(np.float64).copy()
    values = records["value"].astype(np.float64).copy()
    status = int(episode.terminal["status"])
    terminal = np.zeros(len(records) - 1, dtype=np.float64)
    potential[-1] = 0.0
    if status in (WIN, LOSS):
        values[-1] = 0.0
        terminal[-1] = float(records["terminal_reward"][-1])
    coefficient = max(0.0, 1.0 - iteration / 300.0) * shaping_scale
    shape = coefficient * (discounts[:, None] * potential[1:] - potential[:-1])
    rewards = terminal + shape.sum(axis=1)
    deltas = rewards + discounts * values[1:] - values[:-1]
    advantages = np.zeros_like(rewards)
    mc_returns = np.zeros_like(rewards)
    advantage = 0.0
    mc_return = values[-1]
    for index in range(len(rewards) - 1, -1, -1):
        advantage = deltas[index] + discounts[index] * lambdas[index] * advantage
        mc_return = rewards[index] + discounts[index] * mc_return
        advantages[index] = advantage
        mc_returns[index] = mc_return
    return {"reward": rewards.astype(np.float32), "terminal": terminal.astype(np.float32),
            "shape": shape.astype(np.float32), "discount": discounts.astype(np.float32),
            "advantage": advantages.astype(np.float32),
            "return": (advantages + values[:-1]).astype(np.float32),
            "mc_return": mc_returns.astype(np.float32)}
