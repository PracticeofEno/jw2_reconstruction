"""Strict RLO1 interchange and time-aware returns for the in-process commander.

All fields are little endian, packed without padding. A terminal state is a
separate final record. An unfinished/crashed file is never a training episode.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import struct
import zlib

import numpy as np

MAGIC = b"JWRLO001"
LEGACY_SCHEMA_CRC = 0x1F364207
LEGACY_VECTOR_SIZE = 528
CONTEXT_SCHEMA_CRC = 0xDA97FD92
CONTEXT_VECTOR_SIZE = 542
LEGACY_MAP_SIZE = 9 * 16 * 16
SCHEMA_CRC = 0x7EEED592
FORMAT_VERSION = 6
RACE_VECTOR_SIZE = 642
VECTOR_SIZE = 1410
MAP_SHAPE = (12, 16, 16)
MAP_SIZE = 3072
PRIVILEGED_SIZE = 32
LEGACY_HEAD_SIZES = (42, 16, 4, 8, 16, 3, 3, 3)
RACE_HEAD_SIZES = (64, 16, 4, 8, 16, 3, 3, 3)
HEAD_SIZES = (96, 16, 4, 8, 16, 3, 3, 3)
HEAD_OFFSETS = tuple(np.cumsum((0,) + HEAD_SIZES[:-1]).tolist())
MASK_SIZE = sum(HEAD_SIZES)
HEADER = struct.Struct("<8s8I")
DECISION, WIN, LOSS, TRUNCATED, INVALID = range(5)
def _record_dtype(compact: bool, vector_size: int = VECTOR_SIZE, map_size: int | None = None) -> np.dtype:
    masks = MASK_SIZE if vector_size == VECTOR_SIZE else 117 if vector_size == RACE_VECTOR_SIZE else 95
    if map_size is None:
        map_size = LEGACY_MAP_SIZE if vector_size in (LEGACY_VECTOR_SIZE, CONTEXT_VECTOR_SIZE) else MAP_SIZE
    return np.dtype([
    ("frame", "<u4"), ("delta_frame", "<u2"), ("event", "u1"), ("teacher", "u1"),
    ("status", "u1"), ("reserved", "u1"),
    ("weight_version", "<u4"),
    ("vector", "<f2" if compact else "<f4", (vector_size,)), ("map", "u1", (map_size,)),
    ("mask_packed" if compact else "mask", "u1", ((masks + 7) // 8 if compact else masks,)),
    ("action", "u1", (8,)),
    ("logp", "<f4", (8,)), ("value", "<f4"),
    ("potential", "<f4", (4,)), ("terminal_reward", "<f4"), ("reserved_reward", "<f4"),
    ("privileged", "<f2" if compact else "<f4", (PRIVILEGED_SIZE,)),
    ("crc32", "<u4"),
], align=False)


WIRE_RECORD_DTYPE = _record_dtype(True)
RECORD_DTYPE = _record_dtype(False)  # decoded compatibility view / fixture input
RECORD_SIZE = WIRE_RECORD_DTYPE.itemsize
assert RECORD_SIZE == 6061


class DecodedRecords:
    """Lazy fields over compact memmap; only requested fields/batches expand.

    Existing training consumers see float32 vector/private and unpacked mask95.
    np.asarray/concatenate materializes a requested batch in RECORD_DTYPE.
    """
    dtype = RECORD_DTYPE

    def __init__(self, raw):
        self.raw = raw
        self.dtype = _record_dtype(False, raw.dtype["vector"].shape[0], raw.dtype["map"].shape[0])

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
                return np.unpackbits(self.raw["mask_packed"], axis=-1, bitorder="little")[..., :self.dtype["mask"].shape[0]]
            return self.raw[key]
        raw = self.raw[key]
        if isinstance(raw, np.void):
            return np.asarray(DecodedRecords(np.asarray(raw).reshape(1)))[0]
        return DecodedRecords(raw)

    def __iter__(self):
        for index in range(len(self)):
            yield self[index]

    def __array__(self, dtype=None, copy=None):
        result = np.empty(self.shape, dtype=self.dtype)
        for name in self.dtype.names:
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
    sizes = HEAD_SIZES if records.dtype["mask"].shape[0] == MASK_SIZE else RACE_HEAD_SIZES if records.dtype["mask"].shape[0] == 117 else LEGACY_HEAD_SIZES
    offsets = np.cumsum((0,) + sizes[:-1]).tolist()
    for head, (offset, size) in enumerate(zip(offsets, sizes)):
        chosen = decision["action"][:, head].astype(np.int64)
        if np.any(chosen >= size):
            raise RolloutError(f"head {head} action out of range")
        if not np.all(decision["mask"][np.arange(len(decision)), offset + chosen]):
            raise RolloutError(f"head {head} sampled a masked action")


def read_rollout(path: str | Path, *, current_version: int | None = None,
                 teacher: bool | None = None, allow_legacy: bool = False) -> Episode:
    path = Path(path)
    size = path.stat().st_size
    with path.open("rb") as stream:
        raw = stream.read(HEADER.size)
    if len(raw) != HEADER.size:
        raise RolloutError("truncated header")
    magic, schema, version, owner, seed, weight, vector, maps, record_size = HEADER.unpack(raw)
    contract = (magic, schema, version, vector, maps, record_size)
    legacy = contract in (
        (MAGIC, LEGACY_SCHEMA_CRC, 2, LEGACY_VECTOR_SIZE, LEGACY_MAP_SIZE, 3522),
        (MAGIC, CONTEXT_SCHEMA_CRC, 3, CONTEXT_VECTOR_SIZE, LEGACY_MAP_SIZE, 3550),
        (MAGIC, 0x53DD6137, 4, 606, MAP_SIZE, 4446),
        (MAGIC, 0xB32DB21E, 5, RACE_VECTOR_SIZE, MAP_SIZE, 4521),
    )
    if legacy and not allow_legacy:
        raise RolloutError("legacy RLO1 requires explicit historical decoding; new observations must be collected for current training")
    if not legacy and contract != (
            MAGIC, SCHEMA_CRC, FORMAT_VERSION, VECTOR_SIZE, MAP_SIZE, RECORD_SIZE):
        raise RolloutError("RLO1 schema, compact format version 6, or dimensions mismatch")
    wire_dtype = _record_dtype(True, vector, maps)
    if owner >= 8 or seed == 0:
        raise RolloutError("invalid owner or zero policy seed")
    if current_version is not None and not 0 <= current_version - weight <= 1:
        raise RolloutError("policy version must be current or one generation old")
    payload_size = size - HEADER.size
    if payload_size <= 0 or payload_size % record_size:
        raise RolloutError("partial or empty record payload")
    raw_records = np.memmap(path, dtype=wire_dtype, mode="r", offset=HEADER.size,
                        shape=(payload_size // record_size,))
    try:
        for index, record in enumerate(raw_records):
            if zlib.crc32(record.tobytes()[:-4]) != int(record["crc32"]):
                raise RolloutError(f"record {index} CRC32 mismatch")
        if np.any(raw_records["weight_version"] != weight):
            raise RolloutError("record policy version differs from pinned header")
        if np.any(raw_records["mask_packed"][:, -1] & (0x80 if vector < RACE_VECTOR_SIZE else 0xe0)):
            raise RolloutError("reserved mask bits must be zero")
        records = DecodedRecords(raw_records)
        _validate_records(records)
        if teacher is not None and bool(records[0]["teacher"]) != teacher:
            raise RolloutError("teacher/policy cohort mismatch")
    except BaseException:
        raw_records._mmap.close()
        raise
    return Episode(path, owner, seed, weight, records)


LABEL_MAGIC = b"JWTL0003"
LABEL_RECORD = np.dtype([("mask_packed", "u1", ((MASK_SIZE + 7) // 8,)), ("action", "u1", (8,))], align=False)
LEGACY_LABEL_MAGIC = b"JWTL0001"
LEGACY_LABEL_RECORD = np.dtype([("mask_packed", "u1", (12,)), ("action", "u1", (8,))], align=False)


def read_teacher_labels(path: str | Path, *, allow_legacy=False):
    """Return actions u8[n,8] and current u8[n,149] (legacy 95/117) masks.

    Written under -AIDAGGER: one record per RLO record (terminal = zeros).
    """
    path = Path(path)
    raw = path.read_bytes()
    legacy = allow_legacy and raw[:8] in (LEGACY_LABEL_MAGIC, b"JWTL0002")
    race_legacy = legacy and raw[:8] == b"JWTL0002"
    if len(raw) < len(LABEL_MAGIC) or (raw[:len(LABEL_MAGIC)] != LABEL_MAGIC and not legacy):
        raise RolloutError("teacher label file magic mismatch")
    payload = raw[len(LABEL_MAGIC):]
    dtype = np.dtype([("mask_packed", "u1", (15,)), ("action", "u1", (8,))]) if race_legacy else LEGACY_LABEL_RECORD if legacy else LABEL_RECORD
    if len(payload) % dtype.itemsize:
        raise RolloutError("partial teacher label payload")
    records = np.frombuffer(payload, dtype=dtype)
    masks = np.unpackbits(records["mask_packed"], axis=-1, bitorder="little")[:, :117 if race_legacy else 95 if legacy else MASK_SIZE]
    return records["action"].copy(), masks.astype(np.uint8)


def relabel_with_teacher(episode: Episode) -> Episode:
    """DAgger: replace the policy's sampled actions/masks by the rule
    commander's labels for the same observations, yielding a teacher-cohort
    episode over the states the policy actually visited."""
    actions, masks = read_teacher_labels(str(episode.path) + ".teacher.bin",
        allow_legacy=episode.records.dtype["mask"].shape[0] != MASK_SIZE)
    # Converted context episodes may already be ordinary ndarrays. Relabeling
    # must leave their executed action history and on-policy probabilities intact.
    records = np.array(episode.records, copy=True)
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
    return _write_rollout(path, records, owner=owner, seed=seed, weight_version=weight_version,
                          schema=SCHEMA_CRC, version=FORMAT_VERSION, vector=VECTOR_SIZE, maps=MAP_SIZE)


def write_context_rollout(path: str | Path, records: np.ndarray, *, owner: int = 1,
                          seed: int = 1, weight_version: int = 0) -> Path:
    """Explicit historical 528-to-542 migration output; not current training data.

    This writer never invents the new execution or map observations. The normal
    reader rejects its result unless historical decoding is explicitly enabled.
    """
    return _write_rollout(path, records, owner=owner, seed=seed, weight_version=weight_version,
                          schema=CONTEXT_SCHEMA_CRC, version=3, vector=CONTEXT_VECTOR_SIZE, maps=LEGACY_MAP_SIZE)


def _write_rollout(path, records, *, owner, seed, weight_version, schema, version, vector, maps):
    records = np.array(records, dtype=_record_dtype(False, vector, maps), copy=True)
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
    wire = np.empty(len(records), dtype=_record_dtype(True, vector, maps))
    with np.errstate(over="ignore", invalid="ignore"):
        for name in wire.dtype.names:
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
        stream.write(HEADER.pack(MAGIC, schema, version, owner, seed,
                                 weight_version, vector, maps, wire.dtype.itemsize))
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
    if not math.isfinite(gamma) or not 0 < gamma <= 1:
        raise ValueError("gamma must be finite and in (0, 1]")
    if not math.isfinite(gae_lambda) or not 0 <= gae_lambda <= 1:
        raise ValueError("gae_lambda must be finite and in [0, 1]")
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
