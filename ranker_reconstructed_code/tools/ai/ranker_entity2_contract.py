# -*- coding: utf-8 -*-
"""act3 / ENTCMD02 entity-command wire contract (protocol 3, feature v3).

Python mirror of include/ranker_ai_entity_economy.h /
src/ranker_ai_entity_economy.cpp (plan:
docs/AI_PLAY_ENTCMD02_DIRECT_ECONOMY_PLAN.md sections 6, 7, 10, 12, 15 and
the team-intent slot extension docs/AI_PLAY_INTENT_SLOT_DESIGN_EASY.md).
The C++ side is normative; the golden anchors in `selftest()` pin both
implementations to the same bytes.

Feature v3 adds to every ACT_REQ: four slot blocks (MAIN/RAID_A/RAID_B/
SCOUT), up to eight start candidates (maps carry 2..8; -1 = absent), the
slot command/cell masks, intent reward material, and per own row the slot
id, slot-order relation and assign mask.  The reply carries per-row assign
and the commander's (command, cell) per slot; the outcome carries per-slot
results and the assign trainable bits.

ENTCMD02 is a separate contract from ENTCMD01 (RAI2/act2): any RAI2 frame,
SHD1/SHD2 record or ENTCMD01 checkpoint is a hard `WireError` here.

Framing rules (plan 12.5): any header/size/version/CRC mismatch is a
`WireError` — the connection must be closed.
"""

from __future__ import annotations

import math
import struct
import zlib
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

HEADER_BYTES = 128
MAGIC = b"RAI3"
CONTRACT_ID = b"ENTCMD02"
PROTOCOL = 3
# observation / global-feature / entity-feature / entity-action / wire
# semantic vocabulary / point-geometry / economy-candidate / outcome versions,
# in header order (plan 12; feature/action/outcome 3 = slot extension).
VERSIONS = (5, 10, 3, 6, 3, 1, 1, 3)
LEGACY_SHADOW_VERSIONS = (5, 10, 3, 5, 3, 1, 1, 3)
GLOBAL_COUNT = 802
# Action v4: the personal command vocabulary has no STOP (10 entries).
COMMAND_COUNT = 10
POINT_COUNT = 96
POINT_MASK_WORDS = 3
GLOBAL_CELL_COUNT = 64
OWN_CONTINUOUS_COUNT = 33
TARGET_CONTINUOUS_COUNT = 14
CANDIDATE_FEATURE_COUNT = 8
QUEUE_SLOT_COUNT = 5
WIRE_ROW_LIMIT = 2048          # U, E and each of R/B/P/Q
CANDIDATE_LIMIT = 4 * WIRE_ROW_LIMIT
MAX_PAYLOAD_BYTES = 16 * 1024 * 1024
OWN_PREFIX_BYTES = 207
OWN_APPENDIX_BYTES = 42        # 36 economy + 6 slot (u8, u8, u32)
QUEUE_SLOT_BYTES = 16
TARGET_ROW_BYTES = 76
CANDIDATE_ROW_BYTES = 64
SLOT_COUNT = 4
SLOT_BLOCK_BYTES = 36
START_CANDIDATE_COUNT = 8
START_CANDIDATE_BYTES = 8
INTENT_PREFIX_BYTES = SLOT_COUNT * SLOT_BLOCK_BYTES + \
    START_CANDIDATE_COUNT * START_CANDIDATE_BYTES + 16 + 32 + 32
FIXED_PREFIX_BYTES = 3336 + INTENT_PREFIX_BYTES     # 3624

KIND_HELLO = 1
KIND_ACK = 2
KIND_ACT_REQ = 3
KIND_ACT_REPLY = 4
KIND_OUTCOME = 5
KIND_TERMINAL = 6
KIND_ERROR = 7

FLAG_TERMINATED = 1 << 0
FLAG_TRUNCATED = 1 << 1
_KNOWN_FLAGS = FLAG_TERMINATED | FLAG_TRUNCATED

# Policy command vocabulary (action v4, AiEntity2PolicyCommand).  STOP is an
# engine-only command (slot STOP fan-out, watchdog recovery) and appears in
# the observation's semantic order vocabulary only.
COMMAND_KEEP = 0
COMMAND_MOVE = 1
COMMAND_ATTACK_MOVE = 2
COMMAND_PATROL = 3
COMMAND_ATTACK_UNIT = 4
COMMAND_HOLD = 5
COMMAND_HARVEST = 6
COMMAND_BUILD = 7
COMMAND_PRODUCE_UNIT = 8
COMMAND_RESEARCH_UPGRADE = 9
POINT_COMMANDS = (COMMAND_MOVE, COMMAND_ATTACK_MOVE, COMMAND_PATROL)
ECONOMY_COMMANDS = (COMMAND_HARVEST, COMMAND_BUILD, COMMAND_PRODUCE_UNIT,
                    COMMAND_RESEARCH_UPGRADE)
NO_ARGUMENT_COMMANDS = (COMMAND_KEEP, COMMAND_HOLD)
COMMAND_MASK_BITS = (1 << COMMAND_COUNT) - 1

# Own role vocabulary (plan 5).
ROLE_MELEE = 0
ROLE_RANGED = 1
ROLE_WORKER = 2
ROLE_BUILDING = 3
ROLE_TRANSPORT = 4
ROLE_OTHER = 5
ROLE_COUNT = 6

# Team-intent slots.
SLOT_MAIN = 0
SLOT_RAID_A = 1
SLOT_RAID_B = 2
SLOT_SCOUT = 3
SLOT_NONE = 0xFF
SCOUT_CAPACITY = 1
SLOT_COMMAND_KEEP = 0
SLOT_COMMAND_MOVE = 1
SLOT_COMMAND_ATTACK_MOVE = 2
SLOT_COMMAND_PATROL = 3
SLOT_COMMAND_HUNT_NEUTRAL = 4   # persistent: members hunt visible neutral monsters
SLOT_COMMAND_HOLD = 5
SLOT_COMMAND_STOP = 6           # one-shot: order cleared, members stopped
SLOT_COMMAND_COUNT = 7          # action v5: no CLEAR
SLOT_POINT_COMMANDS = (SLOT_COMMAND_MOVE, SLOT_COMMAND_ATTACK_MOVE,
                       SLOT_COMMAND_PATROL)
SLOT_RELATION_NONE = 0
SLOT_RELATION_MATCH = 1
SLOT_RELATION_DIFFERS = 2
SLOT_RELATION_JUST_ASSIGNED = 3

# Candidate kinds (plan 7); the command each kind answers.
CAND_RESOURCE = 0
CAND_BUILD_SITE = 1
CAND_PRODUCE_UNIT = 2
CAND_RESEARCH_UPGRADE = 3
CAND_KIND_COUNT = 4
COMMAND_OF_KIND = {
    CAND_RESOURCE: COMMAND_HARVEST,
    CAND_BUILD_SITE: COMMAND_BUILD,
    CAND_PRODUCE_UNIT: COMMAND_PRODUCE_UNIT,
    CAND_RESEARCH_UPGRADE: COMMAND_RESEARCH_UPGRADE,
}
KIND_OF_COMMAND = {v: k for k, v in COMMAND_OF_KIND.items()}

CAND_FLAG_EXPLORED = 1 << 0
CAND_FLAG_VISIBLE = 1 << 1
CAND_FLAG_EXPANSION_SITE = 1 << 2
CAND_FLAG_ACTIVE_OR_RESERVED = 1 << 3
CAND_FLAG_ANY_SOURCE_AVAILABLE = 1 << 4
CAND_FLAG_REMEMBERED = 1 << 5
_KNOWN_CAND_FLAGS = 0x3F

# capability_bits / source_state_bits (plan 6).
CAP_MOVE = 1 << 0
CAP_ATTACK = 1 << 1
CAP_PATROL = 1 << 2
CAP_HOLD = 1 << 3
CAP_HARVEST = 1 << 4
CAP_BUILD = 1 << 5
CAP_PRODUCE = 1 << 6
CAP_RESEARCH = 1 << 7
_KNOWN_CAP_BITS = 0xFF
STATE_COMPLETED = 1 << 0
STATE_UNDER_CONSTRUCTION = 1 << 1
STATE_CARGO_NONZERO = 1 << 2
STATE_QUEUE_FULL = 1 << 3
STATE_ACTIVE_ECONOMY_ORDER = 1 << 4
STATE_OUTSTANDING_RESERVATION = 1 << 5
_KNOWN_STATE_BITS = 0x3F
TYPE_SENTINEL = 0xFFFFFFFF

# Effective queue slot vocab.
QUEUE_KIND_EMPTY = 0
QUEUE_KIND_PRODUCE = 1
QUEUE_KIND_RESEARCH = 2
QUEUE_STATUS_EMPTY = 0
QUEUE_STATUS_ENGINE_ACTIVE = 1
QUEUE_STATUS_ENGINE_DEFERRED = 2
QUEUE_STATUS_AWAITING_APPLY = 3
QUEUE_ORIGIN_UNKNOWN = 0xFFFF

# Wire semantic order v3 (plan 11.4).
SEMANTIC_NONE = 0
SEMANTIC_EXTERNAL_UNKNOWN = 1
SEMANTIC_MOVE = 2
SEMANTIC_ATTACK_MOVE = 3
SEMANTIC_PATROL = 4
SEMANTIC_ATTACK_UNIT = 5
SEMANTIC_HOLD = 6
SEMANTIC_STOP = 7
SEMANTIC_HARVEST = 8
SEMANTIC_BUILD = 9
SEMANTIC_PRODUCE_UNIT = 10
SEMANTIC_RESEARCH_UPGRADE = 11
SEMANTIC_COUNT = 12

# OUTCOME result / reject enums (plan 12.4 + slot extension).
RESULT_KEPT = 0
RESULT_DEDUPED = 1
RESULT_PUBLISHED = 2
RESULT_REJECTED_MASK = 3
RESULT_REJECTED_STALE = 4
RESULT_PLANNER_FAILED = 5
RESULT_ENCODE_FAILED = 6
RESULT_REJECTED_CONFLICT = 7
RESULT_TRANSACTION_ABORTED = 8
RESULT_CONTROLLER_FAILED = 9
RESULT_COUNT = 10

REJECT_NONE = 0
REJECT_OUT_OF_RANGE = 1
REJECT_MASKED = 2
REJECT_STALE_SOURCE = 3
REJECT_STALE_TARGET = 4
REJECT_STALE_CANDIDATE = 5
REJECT_OWNERSHIP = 6
REJECT_INACTIVE = 7
REJECT_VISIBILITY = 8
REJECT_HOSTILITY = 9
REJECT_CAPABILITY = 10
REJECT_RENDER_CLASS = 11
REJECT_TERRAIN = 12
REJECT_POINT = 13
REJECT_DEPLETED = 14
REJECT_PLACEMENT = 15
REJECT_PREREQUISITE = 16
REJECT_RESOURCE_CONFLICT = 17
REJECT_POPULATION_CONFLICT = 18
REJECT_QUEUE_CONFLICT = 19
REJECT_SITE_CONFLICT = 20
REJECT_RESEARCH_CONFLICT = 21
REJECT_CANDIDATE_KIND = 22
REJECT_PLANNER = 23
REJECT_ENCODE = 24
REJECT_TRANSPORT_CAPACITY = 25
REJECT_HANDLER_REJECTED = 26
REJECT_INTERNAL_ERROR = 27
REJECT_SLOT_CONFLICT = 28
REJECT_SLOT_COMMAND = 29
REJECT_COUNT = 30

TERMINAL_ONGOING = 0
TERMINAL_WIN = 1
TERMINAL_LOSS = 2
TERMINAL_DRAW = 3

TILE_PX = 32


class WireError(RuntimeError):
    """Hard framing/contract violation: close the connection."""


def crc32(data: bytes) -> int:
    """IEEE reflected CRC32 (poly 0xedb88320, init/xor-out 0xffffffff)."""
    return zlib.crc32(data) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Header (plan 12.1)
# ---------------------------------------------------------------------------


@dataclass
class Header:
    kind: int = 0
    flags: int = 0
    payload_bytes: int = 0
    owner: int = 0
    episode: int = 0
    frame: int = 0
    sequence: int = 0
    reply_to_sequence: int = 0
    own_rows: int = 0
    target_rows: int = 0
    resource_rows: int = 0
    build_rows: int = 0
    produce_rows: int = 0
    research_rows: int = 0
    payload_crc32: int = 0
    policy_version: int = 0

    @property
    def candidate_rows(self) -> int:
        return (self.resource_rows + self.build_rows + self.produce_rows +
                self.research_rows)


_HEADER_STRUCT = struct.Struct(
    "<4sHHHHI8s8H" + "5I" + "6I" + "3I" + "I" + "I" + "6I")
assert _HEADER_STRUCT.size == HEADER_BYTES


def pack_header(header: Header) -> bytes:
    return _HEADER_STRUCT.pack(
        MAGIC, HEADER_BYTES, PROTOCOL, header.kind, header.flags,
        header.payload_bytes, CONTRACT_ID, *VERSIONS,
        header.owner, header.episode, header.frame, header.sequence,
        header.reply_to_sequence,
        header.own_rows, header.target_rows, header.resource_rows,
        header.build_rows, header.produce_rows, header.research_rows,
        GLOBAL_COUNT, COMMAND_COUNT, POINT_COUNT,
        header.payload_crc32, header.policy_version,
        0, 0, 0, 0, 0, 0)


def parse_header(data: bytes, *, legacy_shadow: bool = False) -> Header:
    if len(data) < HEADER_BYTES:
        raise WireError("short header")
    fields = _HEADER_STRUCT.unpack(data[:HEADER_BYTES])
    (magic, header_bytes, protocol, kind, flags, payload_bytes, contract,
     v0, v1, v2, v3, v4, v5, v6, v7,
     owner, episode, frame, sequence, reply_to,
     u, e, r, b, p, q,
     global_count, command_count, point_count,
     payload_crc, policy_version,
     z0, z1, z2, z3, z4, z5) = fields
    if magic != MAGIC:
        raise WireError("bad magic")
    if header_bytes != HEADER_BYTES or protocol != PROTOCOL:
        raise WireError("bad header size / protocol")
    if not KIND_HELLO <= kind <= KIND_ERROR:
        raise WireError("unknown frame kind")
    if flags & ~_KNOWN_FLAGS:
        raise WireError("undefined flags set")
    if (flags & FLAG_TERMINATED) and (flags & FLAG_TRUNCATED):
        raise WireError("terminated and truncated both set")
    if payload_bytes > MAX_PAYLOAD_BYTES:
        raise WireError("payload above limit")
    if contract != CONTRACT_ID:
        raise WireError("bad contract id")
    versions = (v0, v1, v2, v3, v4, v5, v6, v7)
    if versions != VERSIONS and not (legacy_shadow and versions == LEGACY_SHADOW_VERSIONS):
        raise WireError("contract version mismatch")
    if (global_count, command_count, point_count) != (
            GLOBAL_COUNT, COMMAND_COUNT, POINT_COUNT):
        raise WireError("fixed count mismatch")
    for count in (u, e, r, b, p, q):
        if count > WIRE_ROW_LIMIT:
            raise WireError("row count above wire hard limit")
    if (z0, z1, z2, z3, z4, z5) != (0, 0, 0, 0, 0, 0):
        raise WireError("reserved field nonzero")
    return Header(kind, flags, payload_bytes, owner, episode, frame, sequence,
                  reply_to, u, e, r, b, p, q, payload_crc, policy_version)


def act_request_payload_bytes(u: int, e: int, c: int,
                              terminal: bool = False) -> int:
    if u > WIRE_ROW_LIMIT or e > WIRE_ROW_LIMIT or c > CANDIDATE_LIMIT:
        raise WireError("row count above wire hard limit")
    pair_words = (e + 31) // 32
    econ_words = (c + 31) // 32
    size = (FIXED_PREFIX_BYTES +
            (OWN_PREFIX_BYTES + OWN_APPENDIX_BYTES +
             QUEUE_SLOT_COUNT * QUEUE_SLOT_BYTES) * u +
            TARGET_ROW_BYTES * e + CANDIDATE_ROW_BYTES * c +
            4 * u * (pair_words + econ_words))
    return size + (4 if terminal else 0)


def expand_bitmask(words: Sequence[int], count: int) -> List[bool]:
    """LSB-first bit expansion; the count-exceeding high bits must be zero."""
    bits = [bool((words[i >> 5] >> (i & 31)) & 1) for i in range(count)]
    total_bits = len(words) * 32
    for i in range(count, total_bits):
        if (words[i >> 5] >> (i & 31)) & 1:
            raise WireError("bitset high bits above count are nonzero")
    return bits


def pack_bitmask(bits: Sequence[bool]) -> List[int]:
    words = [0] * ((len(bits) + 31) // 32)
    for i, bit in enumerate(bits):
        if bit:
            words[i >> 5] |= 1 << (i & 31)
    return words


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def take(self, fmt: str) -> tuple:
        s = struct.Struct(fmt)
        if self.offset + s.size > len(self.data):
            raise WireError("payload truncated")
        values = s.unpack_from(self.data, self.offset)
        self.offset += s.size
        return values

    def array(self, fmt_char: str, count: int) -> List:
        return list(self.take("<%d%s" % (count, fmt_char))) if count else []

    def done(self):
        if self.offset != len(self.data):
            raise WireError("payload has trailing bytes")


# SoA field order of the ACT_REQ body (plan 12.2); (name, struct char, per-row
# element count).  Serialization is field-major.
OWN_PREFIX_FIELDS = (
    ("own_id", "I", 1), ("own_generation", "I", 1),
    ("own_control_epoch", "I", 1), ("own_type_id", "H", 1),
    ("own_movement_class", "I", 1), ("own_distance_check_mode", "I", 1),
    ("own_role", "B", 1), ("own_render_class", "I", 1),
    ("own_command_base", "I", 1), ("own_command_state_high_flags", "I", 1),
    ("own_unit_command_flags", "I", 1), ("own_movement_state", "I", 1),
    ("own_semantic_order", "B", 1), ("own_order_status", "B", 1),
    ("own_presence_bits", "B", 1), ("own_engine_order_match", "B", 1),
    ("own_last_attempt_command", "B", 1), ("own_last_attempt_result", "B", 1),
    ("own_last_reject_code", "H", 1), ("own_active_target_row", "i", 1),
    ("own_attackable_class_mask", "I", 1),
    ("own_feature", "f", OWN_CONTINUOUS_COUNT),
    ("command_mask", "I", 1), ("point_mask", "I", POINT_MASK_WORDS),
)
OWN_APPENDIX_FIELDS = (
    ("own_capability_bits", "I", 1),
    ("own_queued_production_type_id", "I", 1),
    ("own_production_variant", "I", 1),
    ("own_deferred_command_count", "I", 1),
    ("own_walking_build_type_id", "I", 1),
    ("own_active_economy_candidate_row", "i", 1),
    ("own_source_state_bits", "I", 1),
    ("own_cargo_ratio", "f", 1),
    ("own_queue_fill_ratio", "f", 1),
    ("own_slot_id", "B", 1),
    ("own_slot_order_relation", "B", 1),
    ("own_assign_mask", "I", 1),
)
TARGET_FIELDS = (
    ("target_id", "I", 1), ("target_generation", "I", 1),
    ("target_type_id", "H", 1), ("target_owner", "B", 1),
    ("target_role", "B", 1), ("target_render_class", "I", 1),
    ("target_kind_bits", "I", 1),
    ("target_feature", "f", TARGET_CONTINUOUS_COUNT),
)
_QUEUE_SLOT_STRUCT = struct.Struct("<BBHIII")
assert _QUEUE_SLOT_STRUCT.size == QUEUE_SLOT_BYTES
_CANDIDATE_STRUCT = struct.Struct("<QBBHiiIII8f")
assert _CANDIDATE_STRUCT.size == CANDIDATE_ROW_BYTES
_SLOT_BLOCK_STRUCT = struct.Struct("<IiiBBHiIIII")
assert _SLOT_BLOCK_STRUCT.size == SLOT_BLOCK_BYTES
_START_CANDIDATE_STRUCT = struct.Struct("<iBBH")
assert _START_CANDIDATE_STRUCT.size == START_CANDIDATE_BYTES


@dataclass
class QueueSlot:
    kind: int = 0
    status: int = 0
    origin_channel: int = QUEUE_ORIGIN_UNKNOWN
    object_id: int = 0
    origin_sequence: int = 0
    queue_ordinal: int = 0


@dataclass
class SlotBlock:
    member_count: int = 0
    centroid_x: int = -1
    centroid_y: int = -1
    command: int = 0
    active: int = 0
    reserved: int = 0
    cell: int = -1
    age_frames: int = 0
    pursuing: int = 0
    terminal: int = 0
    differing: int = 0


@dataclass
class StartCandidate:
    cell: int = -1
    explored: int = 0
    is_own: int = 0


@dataclass
class Candidate:
    key: int
    kind: int
    flags: int
    object_id: int
    x: int
    y: int
    raw0: int
    raw1: int
    raw2: int
    feature: List[float] = field(default_factory=lambda: [0.0] * 8)

    # BUILD packed word helpers (plan 7).
    @property
    def width(self) -> int:
        return self.raw2 & 0xFF

    @property
    def height(self) -> int:
        return (self.raw2 >> 8) & 0xFF

    @property
    def placement_class(self) -> int:
        return (self.raw2 >> 16) & 0xFF

    def footprint_rect(self) -> Tuple[int, int, int, int]:
        """Tile rectangle (x0, y0, x1, y1), half-open, of a BUILD site."""
        tx = self.x >> 5
        ty = self.y >> 5
        return (tx, ty, tx + self.width, ty + self.height)


def _finite(values: Sequence[float], what: str) -> None:
    for value in values:
        if not math.isfinite(value):
            raise WireError("non-finite float in " + what)


def parse_act_request(header: Header, payload: bytes,
                      terminal: bool = False) -> Dict:
    u = header.own_rows
    e = header.target_rows
    c = header.candidate_rows
    expected = act_request_payload_bytes(u, e, c, terminal)
    if len(payload) != expected:
        raise WireError("ACT_REQ payload size mismatch")
    if crc32(payload) != header.payload_crc32:
        raise WireError("payload CRC mismatch")
    r = _Reader(payload)
    out: Dict = {}
    if terminal:
        out["terminal_outcome"] = r.take("<I")[0]
        if out["terminal_outcome"] > TERMINAL_DRAW:
            raise WireError("terminal outcome out of range")
    out["global"] = r.array("f", GLOBAL_COUNT)
    _finite(out["global"], "global features")
    (out["spendable_primary"], out["spendable_secondary"],
     out["spendable_population"], budget_zero) = r.take("<4I")
    if budget_zero != 0:
        raise WireError("budget reserved word nonzero")
    out["cumulative_losses"] = r.array("Q", 4)
    out["economy_reward_material"] = r.array("Q", 10)
    # ---- intent prefix (feature v3) ----
    out["slots"] = [SlotBlock(*r.take("<IiiBBHiIIII")) for _ in range(SLOT_COUNT)]
    out["start_candidates"] = [StartCandidate(*r.take("<iBBH")[:3])
                               for _ in range(START_CANDIDATE_COUNT)]
    out["slot_command_mask"] = r.array("I", SLOT_COUNT)
    words = r.array("I", 2 * SLOT_COUNT)
    out["slot_cell_mask_words"] = [words[2 * s:2 * s + 2] for s in range(SLOT_COUNT)]
    out["intent_reward_material"] = r.array("Q", 4)
    for name, fmt, per_row in OWN_PREFIX_FIELDS:
        values = r.array(fmt, u * per_row)
        if per_row > 1:
            values = [values[i * per_row:(i + 1) * per_row] for i in range(u)]
        out[name] = values
    for name, fmt, per_row in OWN_APPENDIX_FIELDS:
        out[name] = r.array(fmt, u)
    slots: List[List[QueueSlot]] = []
    for _ in range(u):
        row_slots = []
        for _ in range(QUEUE_SLOT_COUNT):
            row_slots.append(QueueSlot(*r.take("<BBHIII")))
        slots.append(row_slots)
    out["queue_slots"] = slots
    for name, fmt, per_row in TARGET_FIELDS:
        values = r.array(fmt, e * per_row)
        if per_row > 1:
            values = [values[i * per_row:(i + 1) * per_row] for i in range(e)]
        out[name] = values
    candidates: List[Candidate] = []
    for _ in range(c):
        values = r.take("<QBBHiiIII8f")
        candidates.append(Candidate(*values[:9], list(values[9:])))
    out["candidates"] = candidates
    pair_words = (e + 31) // 32
    flat = r.array("I", u * pair_words)
    out["attack_pair_mask_words"] = [
        flat[i * pair_words:(i + 1) * pair_words] for i in range(u)]
    econ_words = (c + 31) // 32
    flat = r.array("I", u * econ_words)
    out["economy_pair_mask_words"] = [
        flat[i * econ_words:(i + 1) * econ_words] for i in range(u)]
    r.done()
    _validate_request_semantics(header, out)
    return out


def scout_free_at_snapshot(req: Dict) -> int:
    """SCOUT capacity left at snapshot time (mirror of the C++ field, which
    is not sent: it is the capacity minus the SCOUT slot's member count)."""
    return max(SCOUT_CAPACITY - req["slots"][SLOT_SCOUT].member_count, 0)


def _validate_request_semantics(header: Header, req: Dict) -> None:
    """Contract checks beyond byte framing (plan 6/7/12.2 + slots)."""
    u = header.own_rows
    e = header.target_rows
    c = header.candidate_rows
    for slot in req["slots"]:
        if slot.command >= SLOT_COMMAND_COUNT or slot.active > 1 or \
                slot.reserved != 0 or not -1 <= slot.cell < GLOBAL_CELL_COUNT:
            raise WireError("slot block out of range")
    for cand in req["start_candidates"]:
        if not -1 <= cand.cell < GLOBAL_CELL_COUNT or cand.explored > 1 or \
                cand.is_own > 1:
            raise WireError("start candidate out of range")
    for mask in req["slot_command_mask"]:
        if mask & ~((1 << SLOT_COMMAND_COUNT) - 1) or (mask & 1) == 0:
            raise WireError("slot command mask out of range")
    for i in range(u):
        _finite(req["own_feature"][i], "own features")
        if req["command_mask"][i] & ~COMMAND_MASK_BITS:
            raise WireError("command mask high bits nonzero")
        if (req["command_mask"][i] & 1) == 0:
            raise WireError("KEEP bit cleared in command mask")
        if req["own_role"][i] >= ROLE_COUNT:
            raise WireError("own role out of vocabulary")
        if req["own_semantic_order"][i] >= SEMANTIC_COUNT:
            raise WireError("semantic order out of vocabulary")
        if req["own_capability_bits"][i] & ~_KNOWN_CAP_BITS:
            raise WireError("capability high bits nonzero")
        if req["own_source_state_bits"][i] & ~_KNOWN_STATE_BITS:
            raise WireError("source state high bits nonzero")
        row = req["own_active_economy_candidate_row"][i]
        if row < -1 or row >= c:
            raise WireError("active economy candidate row out of range")
        if req["own_deferred_command_count"][i] > 4:
            raise WireError("deferred command count above 4")
        slot_id = req["own_slot_id"][i]
        if slot_id >= SLOT_COUNT and slot_id != SLOT_NONE:
            raise WireError("slot id out of range")
        if req["own_slot_order_relation"][i] > SLOT_RELATION_JUST_ASSIGNED:
            raise WireError("slot relation out of range")
        if req["own_assign_mask"][i] & ~((1 << SLOT_COUNT) - 1):
            raise WireError("assign mask high bits nonzero")
        if slot_id == SLOT_NONE and req["own_assign_mask"][i] != 0:
            raise WireError("assign mask on a slotless row")
        for slot in req["queue_slots"][i]:
            if slot.kind > QUEUE_KIND_RESEARCH or \
                    slot.status > QUEUE_STATUS_AWAITING_APPLY:
                raise WireError("queue slot enum out of range")
            if (slot.kind == QUEUE_KIND_EMPTY) != \
                    (slot.status == QUEUE_STATUS_EMPTY):
                raise WireError("queue slot kind/status disagree")
            if slot.kind == QUEUE_KIND_EMPTY and (
                    slot.origin_channel != 0 or slot.object_id != 0 or
                    slot.origin_sequence != 0 or slot.queue_ordinal != 0):
                raise WireError("empty queue slot carries values")
            if slot.queue_ordinal > 4:
                raise WireError("queue ordinal above 4")
        expand_bitmask(req["attack_pair_mask_words"][i], e)
        expand_bitmask(req["economy_pair_mask_words"][i], c)
    for i in range(e):
        _finite(req["target_feature"][i], "target features")
    counts = (header.resource_rows, header.build_rows, header.produce_rows,
              header.research_rows)
    seen = set()
    offset = 0
    for kind, count in enumerate(counts):
        previous_key = None
        for index in range(offset, offset + count):
            cand = req["candidates"][index]
            if cand.kind != kind:
                raise WireError("candidate segment kind mismatch")
            if cand.flags & ~_KNOWN_CAND_FLAGS:
                raise WireError("candidate flag high bits nonzero")
            _finite(cand.feature, "candidate features")
            if (kind, cand.key) in seen:
                raise WireError("duplicate (kind,key) candidate")
            seen.add((kind, cand.key))
            if kind == CAND_BUILD_SITE and (cand.raw2 >> 24) != 0:
                raise WireError("BUILD packed word high bits nonzero")
            if kind in (CAND_PRODUCE_UNIT, CAND_RESEARCH_UPGRADE) and (
                    cand.x != 0 or cand.y != 0):
                raise WireError("non-site candidate carries coordinates")
            if previous_key is not None and cand.key <= previous_key:
                raise WireError("candidate segment not in canonical key order")
            previous_key = cand.key
        offset += count
    if offset != c:
        raise WireError("candidate count does not match segments")


def pack_act_request(body: Dict, terminal_outcome: Optional[int] = None) -> bytes:
    """Inverse of parse_act_request (fixtures / mock servers)."""
    u = len(body["own_id"])
    e = len(body["target_id"])
    candidates: List[Candidate] = body["candidates"]
    c = len(candidates)
    parts: List[bytes] = []
    if terminal_outcome is not None:
        parts.append(struct.pack("<I", terminal_outcome))
    parts.append(struct.pack("<%df" % GLOBAL_COUNT, *body["global"]))
    parts.append(struct.pack("<4I", body["spendable_primary"],
                             body["spendable_secondary"],
                             body["spendable_population"], 0))
    parts.append(struct.pack("<4Q", *body["cumulative_losses"]))
    parts.append(struct.pack("<10Q", *body["economy_reward_material"]))
    for slot in body["slots"]:
        parts.append(_SLOT_BLOCK_STRUCT.pack(
            slot.member_count, slot.centroid_x, slot.centroid_y, slot.command,
            slot.active, 0, slot.cell, slot.age_frames, slot.pursuing,
            slot.terminal, slot.differing))
    for cand in body["start_candidates"]:
        parts.append(_START_CANDIDATE_STRUCT.pack(cand.cell, cand.explored,
                                                  cand.is_own, 0))
    parts.append(struct.pack("<%dI" % SLOT_COUNT, *body["slot_command_mask"]))
    flat = [w for words in body["slot_cell_mask_words"] for w in words]
    parts.append(struct.pack("<%dI" % (2 * SLOT_COUNT), *flat))
    parts.append(struct.pack("<4Q", *body["intent_reward_material"]))
    for name, fmt, per_row in OWN_PREFIX_FIELDS:
        values = body[name]
        if per_row > 1:
            values = [x for row in values for x in row]
        parts.append(struct.pack("<%d%s" % (u * per_row, fmt), *values))
    for name, fmt, per_row in OWN_APPENDIX_FIELDS:
        parts.append(struct.pack("<%d%s" % (u, fmt), *body[name]))
    for row_slots in body["queue_slots"]:
        if len(row_slots) != QUEUE_SLOT_COUNT:
            raise WireError("queue slot count per row must be 5")
        for slot in row_slots:
            parts.append(_QUEUE_SLOT_STRUCT.pack(
                slot.kind, slot.status, slot.origin_channel, slot.object_id,
                slot.origin_sequence, slot.queue_ordinal))
    for name, fmt, per_row in TARGET_FIELDS:
        values = body[name]
        if per_row > 1:
            values = [x for row in values for x in row]
        parts.append(struct.pack("<%d%s" % (e * per_row, fmt), *values))
    for cand in candidates:
        parts.append(_CANDIDATE_STRUCT.pack(
            cand.key, cand.kind, cand.flags, cand.object_id, cand.x, cand.y,
            cand.raw0, cand.raw1, cand.raw2, *cand.feature))
    pair_words = (e + 31) // 32
    flat = [x for row in body["attack_pair_mask_words"] for x in row]
    parts.append(struct.pack("<%dI" % (u * pair_words), *flat))
    econ_words = (c + 31) // 32
    flat = [x for row in body["economy_pair_mask_words"] for x in row]
    parts.append(struct.pack("<%dI" % (u * econ_words), *flat))
    payload = b"".join(parts)
    expected = act_request_payload_bytes(u, e, c, terminal_outcome is not None)
    if len(payload) != expected:
        raise WireError("packed ACT_REQ size drifted from the formula")
    return payload


# ---------------------------------------------------------------------------
# ACT_REPLY / OUTCOME (plan 12.3, 12.4 + slot extension)
# ---------------------------------------------------------------------------


def argument_domain_ok(command: int, argument: int, header: Header) -> bool:
    """Hard argument domain per command (framing-level, plan 12.3)."""
    if command in NO_ARGUMENT_COMMANDS:
        return argument == -1
    if command in POINT_COMMANDS:
        return 0 <= argument < POINT_COUNT
    if command == COMMAND_ATTACK_UNIT:
        return 0 <= argument < header.target_rows
    if command in ECONOMY_COMMANDS:
        return 0 <= argument < header.candidate_rows
    return False


def slot_cell_domain_ok(command: int, cell: int) -> bool:
    if command in SLOT_POINT_COMMANDS:
        return 0 <= cell < GLOBAL_CELL_COUNT
    return cell == -1


def pack_reply(commands: Sequence[int], arguments: Sequence[int],
               header: Header, assigns: Optional[Sequence[int]] = None,
               slot_commands: Optional[Sequence[int]] = None,
               slot_cells: Optional[Sequence[int]] = None) -> bytes:
    u = len(commands)
    if len(arguments) != u or u != header.own_rows:
        raise WireError("reply arrays must share the own-row length")
    assigns = list(assigns) if assigns is not None else [0] * u
    slot_commands = list(slot_commands) if slot_commands is not None else [0] * SLOT_COUNT
    slot_cells = list(slot_cells) if slot_cells is not None else [-1] * SLOT_COUNT
    if len(assigns) != u or len(slot_commands) != SLOT_COUNT or \
            len(slot_cells) != SLOT_COUNT:
        raise WireError("reply slot arrays malformed")
    for command, argument in zip(commands, arguments):
        if not 0 <= command < COMMAND_COUNT:
            raise WireError("entity command out of range")
        if not argument_domain_ok(command, argument, header):
            raise WireError("argument outside the command domain")
    for assign in assigns:
        if not 0 <= assign <= SLOT_COUNT:
            raise WireError("assign out of range")
    for command, cell in zip(slot_commands, slot_cells):
        if not 0 <= command < SLOT_COMMAND_COUNT:
            raise WireError("slot command out of range")
        if not slot_cell_domain_ok(command, cell):
            raise WireError("slot cell outside the command domain")
    return (struct.pack("<%dB" % u, *commands) +
            struct.pack("<%di" % u, *arguments) +
            struct.pack("<%dB" % u, *assigns) +
            struct.pack("<%dB" % SLOT_COUNT, *slot_commands) +
            struct.pack("<%di" % SLOT_COUNT, *slot_cells))


def parse_reply(header: Header, payload: bytes) -> Dict:
    u = header.own_rows
    if len(payload) != 6 * u + 4 + 4 * SLOT_COUNT:
        raise WireError("reply payload size mismatch")
    r = _Reader(payload)
    commands = r.array("B", u)
    arguments = r.array("i", u)
    assigns = r.array("B", u)
    slot_commands = r.array("B", SLOT_COUNT)
    slot_cells = r.array("i", SLOT_COUNT)
    r.done()
    for command, argument in zip(commands, arguments):
        if not 0 <= command < COMMAND_COUNT:
            raise WireError("entity command out of range")
        if not argument_domain_ok(command, argument, header):
            raise WireError("argument outside the command domain")
    for assign in assigns:
        if assign > SLOT_COUNT:
            raise WireError("assign out of range")
    for command, cell in zip(slot_commands, slot_cells):
        if command >= SLOT_COMMAND_COUNT:
            raise WireError("slot command out of range")
        if not slot_cell_domain_ok(command, cell):
            raise WireError("slot cell outside the command domain")
    return {"command": commands, "argument": arguments, "assign": assigns,
            "slot_command": slot_commands, "slot_cell": slot_cells}


def pack_outcome(results: Sequence[int], rejects: Sequence[int],
                 trainable: Sequence[bool],
                 slot_results: Optional[Sequence[int]] = None,
                 slot_rejects: Optional[Sequence[int]] = None,
                 slot_trainable_bits: int = 0,
                 assign_trainable: Optional[Sequence[bool]] = None) -> bytes:
    u = len(results)
    if len(rejects) != u or len(trainable) != u:
        raise WireError("outcome arrays must share the own-row length")
    slot_results = list(slot_results) if slot_results is not None else [0] * SLOT_COUNT
    slot_rejects = list(slot_rejects) if slot_rejects is not None else [0] * SLOT_COUNT
    assign_trainable = list(assign_trainable) if assign_trainable is not None \
        else [False] * u
    words = pack_bitmask(trainable)
    assign_words = pack_bitmask(assign_trainable)
    return (struct.pack("<%dH" % u, *results) +
            struct.pack("<%dH" % u, *rejects) +
            struct.pack("<%dI" % len(words), *words) +
            struct.pack("<%dH" % SLOT_COUNT, *slot_results) +
            struct.pack("<%dH" % SLOT_COUNT, *slot_rejects) +
            struct.pack("<I", slot_trainable_bits) +
            struct.pack("<%dI" % len(assign_words), *assign_words))


def parse_outcome(header: Header, payload: bytes) -> Dict:
    u = header.own_rows
    mask_words = (u + 31) // 32
    expected = 4 * u + 4 * mask_words + 4 * SLOT_COUNT + 4 + 4 * mask_words
    if len(payload) != expected:
        raise WireError("outcome payload size mismatch")
    r = _Reader(payload)
    result = r.array("H", u)
    reject = r.array("H", u)
    trainable_words = r.array("I", mask_words)
    slot_result = r.array("H", SLOT_COUNT)
    slot_reject = r.array("H", SLOT_COUNT)
    (slot_trainable_bits,) = r.take("<I")
    assign_words = r.array("I", mask_words)
    r.done()
    trainable = expand_bitmask(trainable_words, u) if u else []
    assign_trainable = expand_bitmask(assign_words, u) if u else []
    for index in range(u):
        if result[index] >= RESULT_COUNT or reject[index] >= REJECT_COUNT:
            raise WireError("outcome enum out of range")
        success = result[index] <= RESULT_PUBLISHED
        if success and reject[index] != REJECT_NONE:
            raise WireError("success result carries a reject code")
        if not success and reject[index] == REJECT_NONE:
            raise WireError("failure result without a reject code")
        if trainable[index] and not success:
            raise WireError("trainable bit set on a failed row")
    for s in range(SLOT_COUNT):
        if slot_result[s] >= RESULT_COUNT or slot_reject[s] >= REJECT_COUNT:
            raise WireError("slot outcome enum out of range")
        if (slot_result[s] <= RESULT_PUBLISHED) != (slot_reject[s] == REJECT_NONE):
            raise WireError("slot result/reject disagree")
    if slot_trainable_bits & ~((1 << SLOT_COUNT) - 1):
        raise WireError("slot trainable high bits nonzero")
    return {"result": result, "reject_code": reject, "trainable": trainable,
            "slot_result": slot_result, "slot_reject_code": slot_reject,
            "slot_trainable": [bool((slot_trainable_bits >> s) & 1)
                               for s in range(SLOT_COUNT)],
            "assign_trainable": assign_trainable}


# ---------------------------------------------------------------------------
# HELLO / ACK / ERROR (plan 12.1)
# ---------------------------------------------------------------------------


@dataclass
class HelloOwnerRecord:
    owner: int
    frozen_hostile_owner_mask: int
    requested_policy_version: int = 0xFFFFFFFF
    requested_checkpoint_sha256: bytes = b"\x00" * 32


@dataclass
class HelloBody:
    max_payload_bytes: int = MAX_PAYLOAD_BYTES
    reply_timeout_ms: int = 5000
    run_mode: int = 0
    controlled_owner_mask: int = 0
    owners: List[HelloOwnerRecord] = field(default_factory=list)


def pack_hello(body: HelloBody) -> bytes:
    parts = [struct.pack("<4I", body.max_payload_bytes, body.reply_timeout_ms,
                         body.run_mode, body.controlled_owner_mask)]
    for record in body.owners:
        if len(record.requested_checkpoint_sha256) != 32:
            raise WireError("checkpoint fingerprint must be 32 bytes")
        parts.append(struct.pack(
            "<4I32s", record.owner, record.frozen_hostile_owner_mask,
            record.requested_policy_version, 0,
            record.requested_checkpoint_sha256))
    return b"".join(parts)


def parse_hello(payload: bytes) -> HelloBody:
    if len(payload) < 16 or (len(payload) - 16) % 48 != 0:
        raise WireError("hello payload size mismatch")
    r = _Reader(payload)
    max_payload, timeout_ms, run_mode, owner_mask = r.take("<4I")
    if run_mode > 1:
        raise WireError("hello run mode out of range")
    record_count = (len(payload) - 16) // 48
    if record_count != bin(owner_mask).count("1"):
        raise WireError("hello owner record count mismatch")
    owners: List[HelloOwnerRecord] = []
    previous = -1
    for _ in range(record_count):
        owner, hostile_mask, policy_v, zero, sha = r.take("<4I32s")
        if zero != 0:
            raise WireError("hello record reserved word nonzero")
        if owner <= previous:
            raise WireError("hello owner records not ascending")
        if owner >= 32 or not (owner_mask >> owner) & 1:
            raise WireError("hello owner not in controlled mask")
        previous = owner
        owners.append(HelloOwnerRecord(owner, hostile_mask, policy_v, sha))
    r.done()
    return HelloBody(max_payload, timeout_ms, run_mode, owner_mask, owners)


def parse_error(payload: bytes) -> Dict:
    if len(payload) < 4:
        raise WireError("error payload too short")
    code, length = struct.unpack_from("<HH", payload)
    if length > 1024 or 4 + length != len(payload):
        raise WireError("error payload size mismatch")
    return {"code": code, "message": payload[4:4 + length].decode("utf-8")}


def frame_bytes(header: Header, payload: bytes) -> bytes:
    """Finalize a frame: stamp size + CRC and concatenate."""
    header.payload_bytes = len(payload)
    header.payload_crc32 = crc32(payload)
    return pack_header(header) + payload


# ---------------------------------------------------------------------------
# Same-tick economy ledger (plan 10) + assign ledger (SCOUT capacity).  Both
# peers replay exactly this: the C++ receiver, the SHD3 writer and the PPO
# recompute must agree byte-for-byte on the dynamic masks and the remaining
# budget before every row.
# ---------------------------------------------------------------------------


def _rects_overlap(a: Tuple[int, int, int, int],
                   b: Tuple[int, int, int, int]) -> bool:
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


class EconomyLedger:
    """Owner-scoped sequential reservation state for one decision."""

    def __init__(self, request: Dict, header: Header):
        self.candidates: List[Candidate] = request["candidates"]
        self.c = header.candidate_rows
        self.remaining_primary = int(request["spendable_primary"])
        self.remaining_secondary = int(request["spendable_secondary"])
        self.remaining_population = int(request["spendable_population"])
        self.reserved_sites: List[Tuple[int, int, int, int]] = []
        self.reserved_research: set = set()
        self.reserved_sites_keys: set = set()

    def budget(self) -> Tuple[int, int, int]:
        return (self.remaining_primary, self.remaining_secondary,
                self.remaining_population)

    def candidate_available(self, index: int) -> bool:
        cand = self.candidates[index]
        if cand.kind == CAND_RESOURCE:
            return True
        if cand.raw0 > self.remaining_primary or \
                cand.raw1 > self.remaining_secondary:
            return False
        if cand.kind == CAND_BUILD_SITE:
            if (cand.kind, cand.key) in self.reserved_sites_keys:
                return False
            rect = cand.footprint_rect()
            return not any(_rects_overlap(rect, r)
                           for r in self.reserved_sites)
        if cand.kind == CAND_PRODUCE_UNIT:
            return cand.raw2 <= self.remaining_population
        if cand.kind == CAND_RESEARCH_UPGRADE:
            return cand.object_id not in self.reserved_research
        return False

    def dynamic_masks(self, base_command_mask: int,
                      base_pair_words: Sequence[int]) -> Tuple[int, List[int]]:
        """(dynamic command mask, dynamic economy pair words) for one row."""
        econ_words = (self.c + 31) // 32
        dyn_words = [0] * econ_words
        kinds_present = [False] * CAND_KIND_COUNT
        for index in range(self.c):
            if (base_pair_words[index >> 5] >> (index & 31)) & 1 == 0:
                continue
            if not self.candidate_available(index):
                continue
            dyn_words[index >> 5] |= 1 << (index & 31)
            kinds_present[self.candidates[index].kind] = True
        command_mask = base_command_mask & ((1 << COMMAND_HARVEST) - 1)
        for kind, command in COMMAND_OF_KIND.items():
            if (base_command_mask >> command) & 1 and kinds_present[kind]:
                command_mask |= 1 << command
        return command_mask, dyn_words

    def reserve(self, command: int, argument: int) -> None:
        """Apply a legal (already dynamic-mask-checked) economy choice."""
        if command not in ECONOMY_COMMANDS:
            return
        cand = self.candidates[argument]
        if cand.kind == CAND_RESOURCE:
            return
        self.remaining_primary -= cand.raw0
        self.remaining_secondary -= cand.raw1
        if cand.kind == CAND_BUILD_SITE:
            self.reserved_sites.append(cand.footprint_rect())
            self.reserved_sites_keys.add((cand.kind, cand.key))
        elif cand.kind == CAND_PRODUCE_UNIT:
            self.remaining_population -= cand.raw2
        elif cand.kind == CAND_RESEARCH_UPGRADE:
            self.reserved_research.add(cand.object_id)


class AssignLedger:
    """SCOUT-capacity ledger: once an earlier row of the tick took the last
    SCOUT seat, the SCOUT bit closes for every later row (leaving never
    reopens it within the tick)."""

    def __init__(self, scout_free: int):
        self.scout_free = scout_free
        self.scout_taken = 0

    def dynamic_mask(self, base_assign_mask: int) -> int:
        mask = base_assign_mask & ((1 << SLOT_COUNT) - 1)
        if self.scout_taken >= self.scout_free:
            mask &= ~(1 << SLOT_SCOUT)
        return mask

    def apply(self, assign: int) -> None:
        if assign == SLOT_SCOUT + 1:
            self.scout_taken += 1


def command_legal(command: int, argument: int, dyn_command_mask: int,
                  dyn_pair_words: Sequence[int], point_mask_words: Sequence[int],
                  attack_pair_words: Sequence[int],
                  candidates: Sequence[Candidate]) -> bool:
    """Full legality of one (command, argument) under stored masks."""
    if (dyn_command_mask >> command) & 1 == 0:
        return False
    if command in NO_ARGUMENT_COMMANDS:
        return argument == -1
    if command in POINT_COMMANDS:
        return 0 <= argument < POINT_COUNT and \
            (point_mask_words[argument >> 5] >> (argument & 31)) & 1 == 1
    if command == COMMAND_ATTACK_UNIT:
        return argument >= 0 and (argument >> 5) < len(attack_pair_words) and \
            (attack_pair_words[argument >> 5] >> (argument & 31)) & 1 == 1
    if command in ECONOMY_COMMANDS:
        if not (0 <= argument < len(candidates)):
            return False
        if KIND_OF_COMMAND[command] != candidates[argument].kind:
            return False
        return (dyn_pair_words[argument >> 5] >> (argument & 31)) & 1 == 1
    return False


def slot_choice_legal(request: Dict, slot: int, command: int, cell: int) -> bool:
    """Commander legality (mirror of AiEntity2SlotChoiceLegal)."""
    if not 0 <= slot < SLOT_COUNT or not 0 <= command < SLOT_COMMAND_COUNT:
        return False
    if (request["slot_command_mask"][slot] >> command) & 1 == 0:
        return False
    if command in SLOT_POINT_COMMANDS:
        if not 0 <= cell < GLOBAL_CELL_COUNT:
            return False
        words = request["slot_cell_mask_words"][slot]
        return (words[cell >> 5] >> (cell & 31)) & 1 == 1
    return cell == -1


def replay_ledger(request: Dict, header: Header, commands: Sequence[int],
                  arguments: Sequence[int],
                  unresolved_from: Optional[int] = None,
                  assigns: Optional[Sequence[int]] = None) -> Dict:
    """Replay the canonical economy + assign prefix.

    Returns dynamic command masks, remaining budget before each row, the
    dynamic economy pair words, the dynamic assign masks and per-row assign
    legality.  `unresolved_from` (SHD3 PREFIX_UNRESOLVED) zeroes the economy
    mask of every row at or after that index and stops reserving.  A choice
    illegal under its own dynamic mask consumes nothing (the receiver rejects
    that row).
    """
    u = header.own_rows
    ledger = EconomyLedger(request, header)
    assign_ledger = AssignLedger(scout_free_at_snapshot(request))
    dyn_commands: List[int] = []
    budgets: List[Tuple[int, int, int]] = []
    dyn_pairs: List[List[int]] = []
    dyn_assigns: List[int] = []
    assign_legal: List[bool] = []
    econ_words = (header.candidate_rows + 31) // 32
    for i in range(u):
        assign_mask = assign_ledger.dynamic_mask(request["own_assign_mask"][i])
        dyn_assigns.append(assign_mask)
        chosen = assigns[i] if assigns is not None and i < len(assigns) else 0
        if chosen:
            legal = chosen <= SLOT_COUNT and (assign_mask >> (chosen - 1)) & 1 == 1
            assign_legal.append(legal)
            if legal:
                assign_ledger.apply(chosen)
        else:
            assign_legal.append(True)
        base_command = request["command_mask"][i]
        base_pair = request["economy_pair_mask_words"][i]
        if unresolved_from is not None and i >= unresolved_from:
            command_mask = base_command & ((1 << COMMAND_HARVEST) - 1)
            pair_words = [0] * econ_words
        else:
            command_mask, pair_words = ledger.dynamic_masks(base_command,
                                                            base_pair)
        dyn_commands.append(command_mask)
        budgets.append(ledger.budget())
        dyn_pairs.append(pair_words)
        if unresolved_from is not None and i >= unresolved_from:
            continue
        command = commands[i]
        argument = arguments[i]
        if command in ECONOMY_COMMANDS and command_legal(
                command, argument, command_mask, pair_words,
                request["point_mask"][i], request["attack_pair_mask_words"][i],
                request["candidates"]):
            ledger.reserve(command, argument)
    return {"dynamic_command_mask": dyn_commands,
            "remaining_budget": budgets,
            "dynamic_economy_pair_mask_words": dyn_pairs,
            "dynamic_assign_mask": dyn_assigns,
            "assign_legal": assign_legal}


def stochastic_rows(dynamic_command_masks: Sequence[int]) -> List[bool]:
    """A row is a stochastic (trainable) choice iff any non-KEEP command is
    legal under its dynamic mask: every set non-KEEP bit implies >= 1 legal
    argument, so the joint choice count is >= 2 exactly then."""
    return [(mask & ~1) != 0 for mask in dynamic_command_masks]


# ---------------------------------------------------------------------------
# SHD3 dataset records: 'SHD3' + u32 body size + wire header + ACT_REQ
# payload + u32 U + 16-byte labels + dynamic command mask + remaining budget
# + dynamic economy pair mask + dynamic assign mask + 4 slot labels.
# ---------------------------------------------------------------------------

SHADOW_MAGIC = b"SHD5"
SHADOW_KEEP = 0
SHADOW_ISSUE = 1
SHADOW_EXCLUDED = 2
SHADOW_EXCLUDED_COMMAND = 255
SHADOW_REASON_NONE = 0
SHADOW_REASON_SOURCE_MISSING = 1
SHADOW_REASON_TARGET_MISSING = 2
SHADOW_REASON_CANDIDATE_MISSING = 3
SHADOW_REASON_STALE = 4
SHADOW_REASON_MULTIPLE_DESIRED = 5
SHADOW_REASON_RETURN_CARGO = 6
SHADOW_REASON_PREFIX_UNRESOLVED = 7
SHADOW_REASON_MASK_MISMATCH = 8
_SHADOW_LABEL_STRUCT = struct.Struct("<BBHifBBH")
assert _SHADOW_LABEL_STRUCT.size == 16
_SHADOW_SLOT_LABEL_STRUCT = struct.Struct("<BBHi")
assert _SHADOW_SLOT_LABEL_STRUCT.size == 8


@dataclass
class ShadowLabel:
    label: int
    command: int
    exclude_reason: int
    argument: int
    inclusion_probability: float
    assign_label: int = SHADOW_KEEP
    assign: int = 0
    reserved_zero: int = 0


@dataclass
class ShadowSlotLabel:
    label: int = SHADOW_KEEP
    command: int = 0
    cell: int = -1


@dataclass
class ShadowRecord:
    header: Header
    request: Dict
    labels: List[ShadowLabel]
    dynamic_command_mask: List[int]
    remaining_budget: List[Tuple[int, int, int]]
    dynamic_economy_pair_mask_words: List[List[int]]
    dynamic_assign_mask: List[int]
    slot_labels: List[ShadowSlotLabel]
    source: str = ""


def pack_shadow_record(header: Header, payload: bytes,
                       labels: Sequence[ShadowLabel],
                       dynamic_command_mask: Sequence[int],
                       remaining_budget: Sequence[Tuple[int, int, int]],
                       dynamic_pair_words: Sequence[Sequence[int]],
                       dynamic_assign_mask: Optional[Sequence[int]] = None,
                       slot_labels: Optional[Sequence[ShadowSlotLabel]] = None) -> bytes:
    u = header.own_rows
    if len(labels) != u or len(dynamic_command_mask) != u or \
            len(remaining_budget) != u or len(dynamic_pair_words) != u:
        raise WireError("shadow block lengths must equal U")
    dynamic_assign_mask = list(dynamic_assign_mask) if dynamic_assign_mask is not None \
        else [0] * u
    slot_labels = list(slot_labels) if slot_labels is not None \
        else [ShadowSlotLabel() for _ in range(SLOT_COUNT)]
    if len(dynamic_assign_mask) != u or len(slot_labels) != SLOT_COUNT:
        raise WireError("shadow slot block lengths malformed")
    econ_words = (header.candidate_rows + 31) // 32
    parts = [pack_header(header), payload, struct.pack("<I", u)]
    for label in labels:
        parts.append(_SHADOW_LABEL_STRUCT.pack(
            label.label, label.command, label.exclude_reason, label.argument,
            label.inclusion_probability, label.assign_label, label.assign, 0))
    parts.append(struct.pack("<%dI" % u, *dynamic_command_mask))
    flat = [x for row in remaining_budget for x in row]
    parts.append(struct.pack("<%dI" % (3 * u), *flat))
    for words in dynamic_pair_words:
        if len(words) != econ_words:
            raise WireError("dynamic pair word count mismatch")
        parts.append(struct.pack("<%dI" % econ_words, *words))
    parts.append(struct.pack("<%dI" % u, *dynamic_assign_mask))
    for label in slot_labels:
        parts.append(_SHADOW_SLOT_LABEL_STRUCT.pack(label.label, label.command, 0,
                                                    label.cell))
    body = b"".join(parts)
    return SHADOW_MAGIC + struct.pack("<I", len(body)) + body


def parse_shadow_records(data: bytes, source: str = ""):
    """Yield ShadowRecord per record; the stored dynamic block is verified
    against a teacher-forced ledger replay (plan 15.1)."""
    offset = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise WireError("truncated shadow record prefix")
        if data[offset:offset + 4] != SHADOW_MAGIC:
            raise WireError("bad shadow record magic")
        (body_bytes,) = struct.unpack_from("<I", data, offset + 4)
        offset += 8
        if len(data) - offset < body_bytes:
            raise WireError("truncated shadow record body")
        body = data[offset:offset + body_bytes]
        offset += body_bytes
        header = parse_header(body[:HEADER_BYTES], legacy_shadow=True)
        if header.kind != KIND_ACT_REQ:
            raise WireError("shadow record is not an ACT_REQ")
        payload = body[HEADER_BYTES:HEADER_BYTES + header.payload_bytes]
        if len(payload) != header.payload_bytes:
            raise WireError("shadow payload truncated")
        request = parse_act_request(header, payload)
        r = _Reader(body[HEADER_BYTES + header.payload_bytes:])
        (label_count,) = r.take("<I")
        u = header.own_rows
        if label_count != u:
            raise WireError("shadow label count mismatch")
        labels = [ShadowLabel(*r.take("<BBHifBBH")) for _ in range(u)]
        dyn_commands = r.array("I", u)
        flat = r.array("I", 3 * u)
        budgets = [tuple(flat[3 * i:3 * i + 3]) for i in range(u)]
        econ_words = (header.candidate_rows + 31) // 32
        flat = r.array("I", u * econ_words)
        dyn_pairs = [flat[i * econ_words:(i + 1) * econ_words]
                     for i in range(u)]
        dyn_assigns = r.array("I", u)
        slot_labels = []
        for _ in range(SLOT_COUNT):
            label, command, zero, cell = r.take("<BBHi")
            if zero != 0:
                raise WireError("slot label reserved word nonzero")
            slot_labels.append(ShadowSlotLabel(label, command, cell))
        r.done()
        _validate_shadow_labels(header, request, labels, dyn_commands,
                                budgets, dyn_pairs, dyn_assigns, slot_labels)
        yield ShadowRecord(header, request, labels, dyn_commands, budgets,
                           dyn_pairs, dyn_assigns, slot_labels, source)


def _validate_shadow_labels(header: Header, request: Dict,
                            labels: Sequence[ShadowLabel],
                            dyn_commands: Sequence[int],
                            budgets: Sequence[Tuple[int, int, int]],
                            dyn_pairs: Sequence[Sequence[int]],
                            dyn_assigns: Sequence[int],
                            slot_labels: Sequence[ShadowSlotLabel]) -> None:
    unresolved_from = None
    commands: List[int] = []
    arguments: List[int] = []
    assigns: List[int] = []
    for i, label in enumerate(labels):
        if label.reserved_zero != 0:
            raise WireError("shadow label reserved word nonzero")
        if label.label == SHADOW_KEEP:
            if label.command != COMMAND_KEEP or label.argument != -1 or \
                    label.exclude_reason != SHADOW_REASON_NONE:
                raise WireError("KEEP label carries command/argument")
        elif label.label == SHADOW_ISSUE:
            if label.command == COMMAND_KEEP or \
                    label.command >= COMMAND_COUNT or \
                    not argument_domain_ok(label.command, label.argument,
                                           header):
                raise WireError("ISSUE label outside the command domain")
        elif label.label == SHADOW_EXCLUDED:
            if label.command != SHADOW_EXCLUDED_COMMAND or \
                    label.argument != -1 or \
                    label.exclude_reason == SHADOW_REASON_NONE or \
                    label.exclude_reason > SHADOW_REASON_MASK_MISMATCH:
                raise WireError("EXCLUDED label malformed")
            if label.exclude_reason == SHADOW_REASON_PREFIX_UNRESOLVED and \
                    unresolved_from is None:
                unresolved_from = i
        else:
            raise WireError("unknown shadow label")
        if not 0.0 < label.inclusion_probability <= 1.0:
            raise WireError("inclusion probability outside (0,1]")
        if label.assign_label > SHADOW_EXCLUDED or label.assign > SLOT_COUNT:
            raise WireError("assign label out of range")
        if (label.assign_label == SHADOW_ISSUE) != (label.assign != 0):
            raise WireError("assign label/value disagree")
        commands.append(label.command if label.label == SHADOW_ISSUE
                        else COMMAND_KEEP)
        arguments.append(label.argument if label.label == SHADOW_ISSUE
                         else -1)
        assigns.append(label.assign if label.assign_label == SHADOW_ISSUE else 0)
    replay = replay_ledger(request, header, commands, arguments,
                           unresolved_from, assigns)
    if list(replay["dynamic_command_mask"]) != list(dyn_commands) or \
            [tuple(b) for b in replay["remaining_budget"]] != \
            [tuple(b) for b in budgets] or \
            [list(w) for w in replay["dynamic_economy_pair_mask_words"]] != \
            [list(w) for w in dyn_pairs] or \
            list(replay["dynamic_assign_mask"]) != list(dyn_assigns):
        raise WireError("shadow dynamic block disagrees with ledger replay")
    for i, label in enumerate(labels):
        if label.assign_label == SHADOW_ISSUE and not replay["assign_legal"][i]:
            raise WireError("assign ISSUE label illegal under its dynamic mask")
        if label.label != SHADOW_ISSUE:
            continue
        if not command_legal(label.command, label.argument, dyn_commands[i],
                             dyn_pairs[i], request["point_mask"][i],
                             request["attack_pair_mask_words"][i],
                             request["candidates"]):
            raise WireError("ISSUE label illegal under its dynamic mask")
    for slot, label in enumerate(slot_labels):
        if label.label > SHADOW_EXCLUDED:
            raise WireError("slot label out of range")
        if label.label == SHADOW_ISSUE:
            if label.command == SLOT_COMMAND_KEEP or \
                    not slot_choice_legal(request, slot, label.command, label.cell):
                raise WireError("slot ISSUE label illegal under the slot mask")
        elif label.command != 0 or label.cell != -1:
            raise WireError("non-ISSUE slot label carries a command")


# ---------------------------------------------------------------------------


def _fixture_request(u: int = 2, e: int = 1) -> Tuple[Dict, Header]:
    """Small ACT_REQ fixture: a worker + a producer building, one hostile,
    R=1 B=2 P=1 Q=1 with an overlapping build pair to exercise the ledger;
    no combat rows, so every slot is empty."""
    body: Dict = {
        "global": [0.0] * GLOBAL_COUNT,
        "spendable_primary": 400, "spendable_secondary": 0,
        "spendable_population": 3,
        "cumulative_losses": [120, 0, 350, 80],
        "economy_reward_material": [400, 0, 100, 0, 500, 0, 0, 0, 0, 10],
        "slots": [SlotBlock() for _ in range(SLOT_COUNT)],
        "start_candidates": [StartCandidate(9, 1, 1), StartCandidate(54, 0, 0)] +
                            [StartCandidate() for _ in range(6)],
        "slot_command_mask": [1, 1, 1, 1],
        "slot_cell_mask_words": [[0, 0] for _ in range(SLOT_COUNT)],
        "intent_reward_material": [1, 2, 0, 0xFFFFFFFF],
        "own_id": [0x1D0, 0x3A0], "own_generation": [1, 1],
        "own_control_epoch": [1, 1], "own_type_id": [0x10, 0x80],
        "own_movement_class": [0, 0], "own_distance_check_mode": [0, 0],
        "own_role": [ROLE_WORKER, ROLE_BUILDING],
        "own_render_class": [1, 1], "own_command_base": [0, 0],
        "own_command_state_high_flags": [0, 0],
        "own_unit_command_flags": [0, 0], "own_movement_state": [0, 0],
        "own_semantic_order": [0, 0], "own_order_status": [0, 0],
        "own_presence_bits": [3, 3], "own_engine_order_match": [0, 0],
        "own_last_attempt_command": [255, 255],
        "own_last_attempt_result": [255, 255], "own_last_reject_code": [0, 0],
        "own_active_target_row": [-1, -1],
        "own_attackable_class_mask": [0xFFFFFFFF, 0xFFFFFFFF],
        "own_feature": [[0.0] * OWN_CONTINUOUS_COUNT for _ in range(2)],
        "command_mask": [(1 << COMMAND_KEEP) | (1 << COMMAND_MOVE) |
                         (1 << COMMAND_HARVEST) | (1 << COMMAND_BUILD),
                         (1 << COMMAND_KEEP) | (1 << COMMAND_PRODUCE_UNIT) |
                         (1 << COMMAND_RESEARCH_UPGRADE)],
        "point_mask": [[0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF], [0, 0, 0]],
        "own_capability_bits": [CAP_MOVE | CAP_HARVEST | CAP_BUILD,
                                CAP_PRODUCE | CAP_RESEARCH],
        "own_queued_production_type_id": [TYPE_SENTINEL, TYPE_SENTINEL],
        "own_production_variant": [0, 0],
        "own_deferred_command_count": [0, 0],
        "own_walking_build_type_id": [TYPE_SENTINEL, TYPE_SENTINEL],
        "own_active_economy_candidate_row": [-1, -1],
        "own_source_state_bits": [STATE_COMPLETED, STATE_COMPLETED],
        "own_cargo_ratio": [0.0, 0.0], "own_queue_fill_ratio": [0.0, 0.0],
        "own_slot_id": [SLOT_NONE, SLOT_NONE],
        "own_slot_order_relation": [0, 0],
        "own_assign_mask": [0, 0],
        "queue_slots": [[QueueSlot(origin_channel=0) for _ in range(5)]
                        for _ in range(2)],
        "target_id": [0x5D0], "target_generation": [1],
        "target_type_id": [5], "target_owner": [1], "target_role": [0],
        "target_render_class": [1], "target_kind_bits": [1],
        "target_feature": [[0.0] * TARGET_CONTINUOUS_COUNT],
        "candidates": [
            Candidate(key=(29 << 7) | 41, kind=CAND_RESOURCE, flags=1,
                      object_id=0, x=41 * 32 + 16, y=29 * 32 + 16,
                      raw0=(29 << 7) | 41, raw1=1800, raw2=3,
                      feature=[0.32, 0.23, 0.0, 0.0, 0.0, 0.44, 0.375, 0.1]),
            Candidate(key=(0x82 << 32) | (28 << 16) | 40, kind=CAND_BUILD_SITE,
                      flags=1 | 16, object_id=0x82, x=40 * 32, y=28 * 32,
                      raw0=300, raw1=0, raw2=(3 << 8) | 3,
                      feature=[0.31, 0.22, 0.3, 0.0, 0.0, 9 / 64, 1.0, 0.1]),
            Candidate(key=(0x82 << 32) | (29 << 16) | 42, kind=CAND_BUILD_SITE,
                      flags=1 | 16, object_id=0x82, x=42 * 32, y=29 * 32,
                      raw0=300, raw1=0, raw2=(3 << 8) | 3,
                      feature=[0.33, 0.23, 0.3, 0.0, 0.0, 9 / 64, 1.0, 0.1]),
            Candidate(key=0x22, kind=CAND_PRODUCE_UNIT, flags=16,
                      object_id=0x22, x=0, y=0, raw0=150, raw1=0, raw2=1,
                      feature=[0.0, 0.0, 0.15, 0.0, 0.01, 0.125, 1.0, 0.0]),
            Candidate(key=(0x19 << 32) | 2, kind=CAND_RESEARCH_UPGRADE,
                      flags=16, object_id=0x19, x=0, y=0, raw0=400, raw1=0,
                      raw2=2, feature=[0.0, 0.0, 0.4, 0.0, 0.0, 0.5, 1.0, 0.0]),
        ],
        "attack_pair_mask_words": [[0], [0]],
        "economy_pair_mask_words": [[0b00111], [0b11000]],
    }
    header = Header(kind=KIND_ACT_REQ, owner=1, episode=3, frame=9600,
                    sequence=7, reply_to_sequence=6, own_rows=2,
                    target_rows=1, resource_rows=1, build_rows=2,
                    produce_rows=1, research_rows=1, policy_version=4)
    return body, header


def _slot_fixture_request() -> Tuple[Dict, Header]:
    """Fixture with two fighters (rows 2, 3): A in MAIN which marches to
    cell 63, B in SCOUT; the worker/base rows keep no slot."""
    body, header = _fixture_request()
    body = dict(body)
    for name, fmt, per_row in OWN_PREFIX_FIELDS + OWN_APPENDIX_FIELDS:
        body[name] = list(body[name])
    fighters = [(0x570, SLOT_MAIN, SLOT_RELATION_MATCH, 0b0110),
                (0x740, SLOT_SCOUT, SLOT_RELATION_NONE, 0b0111)]
    for unit_id, slot, relation, assign_mask in fighters:
        body["own_id"].append(unit_id)
        body["own_generation"].append(1)
        body["own_control_epoch"].append(1)
        body["own_type_id"].append(5)
        body["own_movement_class"].append(0)
        body["own_distance_check_mode"].append(0)
        body["own_role"].append(ROLE_MELEE)
        body["own_render_class"].append(1)
        body["own_command_base"].append(0)
        body["own_command_state_high_flags"].append(0)
        body["own_unit_command_flags"].append(0)
        body["own_movement_state"].append(0)
        body["own_semantic_order"].append(0)
        body["own_order_status"].append(0)
        body["own_presence_bits"].append(3)
        body["own_engine_order_match"].append(0)
        body["own_last_attempt_command"].append(255)
        body["own_last_attempt_result"].append(255)
        body["own_last_reject_code"].append(0)
        body["own_active_target_row"].append(-1)
        body["own_attackable_class_mask"].append(0xFFFFFFFF)
        body["own_feature"].append([0.0] * OWN_CONTINUOUS_COUNT)
        # A (MAIN, marching): personal MOVE/ATTACK_MOVE/PATROL closed.
        combat = (1 << COMMAND_KEEP) | (1 << COMMAND_ATTACK_UNIT) | (1 << COMMAND_HOLD)
        if slot == SLOT_SCOUT:
            combat |= (1 << COMMAND_MOVE) | (1 << COMMAND_ATTACK_MOVE) | (1 << COMMAND_PATROL)
        body["command_mask"].append(combat)
        body["point_mask"].append([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF])
        body["own_capability_bits"].append(CAP_MOVE | CAP_ATTACK | CAP_PATROL | CAP_HOLD)
        body["own_queued_production_type_id"].append(TYPE_SENTINEL)
        body["own_production_variant"].append(0)
        body["own_deferred_command_count"].append(0)
        body["own_walking_build_type_id"].append(TYPE_SENTINEL)
        body["own_active_economy_candidate_row"].append(-1)
        body["own_source_state_bits"].append(STATE_COMPLETED)
        body["own_cargo_ratio"].append(0.0)
        body["own_queue_fill_ratio"].append(0.0)
        body["own_slot_id"].append(slot)
        body["own_slot_order_relation"].append(relation)
        body["own_assign_mask"].append(assign_mask)
    body["queue_slots"] = body["queue_slots"] + [
        [QueueSlot(origin_channel=0) for _ in range(5)] for _ in range(2)]
    body["attack_pair_mask_words"] = body["attack_pair_mask_words"] + [[1], [1]]
    body["economy_pair_mask_words"] = body["economy_pair_mask_words"] + [[0], [0]]
    body["slots"] = [SlotBlock(1, 112, 208, SLOT_COMMAND_ATTACK_MOVE, 1, 0, 63, 100, 1, 0, 0),
                     SlotBlock(), SlotBlock(),
                     SlotBlock(1, 144, 208, 0, 0, 0, -1, 0, 0, 0, 0)]
    full = 0xFFFFFFFF
    body["slot_command_mask"] = [
        (1 << SLOT_COMMAND_KEEP) | (1 << SLOT_COMMAND_MOVE) | (1 << SLOT_COMMAND_ATTACK_MOVE) |
        (1 << SLOT_COMMAND_PATROL) | (1 << SLOT_COMMAND_HOLD) | (1 << SLOT_COMMAND_STOP) |
        (1 << SLOT_COMMAND_HUNT_NEUTRAL),
        (1 << SLOT_COMMAND_KEEP) | (1 << SLOT_COMMAND_MOVE) | (1 << SLOT_COMMAND_ATTACK_MOVE) |
        (1 << SLOT_COMMAND_PATROL) | (1 << SLOT_COMMAND_HOLD),
        (1 << SLOT_COMMAND_KEEP) | (1 << SLOT_COMMAND_MOVE) | (1 << SLOT_COMMAND_ATTACK_MOVE) |
        (1 << SLOT_COMMAND_PATROL) | (1 << SLOT_COMMAND_HOLD),
        (1 << SLOT_COMMAND_KEEP) | (1 << SLOT_COMMAND_MOVE) | (1 << SLOT_COMMAND_ATTACK_MOVE) |
        (1 << SLOT_COMMAND_PATROL) | (1 << SLOT_COMMAND_HOLD)]
    body["slot_cell_mask_words"] = [[full, full] for _ in range(SLOT_COUNT)]
    header = Header(**{**header.__dict__, "own_rows": 4})
    return body, header


def selftest() -> None:
    assert crc32(b"123456789") == 0xCBF43926

    # Header golden anchors shared with the C++ regression
    # (tests/ai_play_interface_regression.cpp, test_ai_entity2_wire_contract).
    header = Header(kind=KIND_ACT_REQ, flags=0, payload_bytes=3624, owner=1,
                    episode=37, frame=1232, sequence=154,
                    reply_to_sequence=153, own_rows=2, target_rows=3,
                    resource_rows=4, build_rows=5, produce_rows=6,
                    research_rows=7, payload_crc32=0x12345678,
                    policy_version=21)
    raw = pack_header(header)
    assert len(raw) == HEADER_BYTES
    assert raw[0:4] == MAGIC and raw[4] == 128 and raw[6] == PROTOCOL
    assert raw[16:24] == CONTRACT_ID
    assert raw[24] == 5 and raw[26] == 10 and raw[28] == 3 and raw[30] == 6
    assert raw[32] == 3 and raw[34] == 1 and raw[36] == 1 and raw[38] == 3
    assert raw[40] == 1 and raw[44] == 37 and raw[52] == 154
    assert raw[60] == 2 and raw[64] == 3 and raw[68] == 4 and raw[72] == 5
    assert raw[76] == 6 and raw[80] == 7
    assert raw[84] == 0x22 and raw[85] == 0x03 and raw[88] == 10
    assert raw[92] == 96 and raw[96] == 0x78 and raw[100] == 21
    assert raw[104:128] == b"\x00" * 24
    assert parse_header(raw) == header
    for mutate in (
            lambda b: b[:0] + b"X" + b[1:],                    # magic
            lambda b: b[:2] + b"I2" + b[4:],                   # RAI2
            lambda b: b[:23] + b"1" + b[24:],                  # ENTCMD01
            lambda b: b[:28] + b"\x02" + b[29:],               # feature v2
            lambda b: b[:30] + b"\x03" + b[31:],               # action v3 (STOP era)
            lambda b: b[:104] + b"\x01" + b[105:],             # reserved
            lambda b: b[:10] + bytes([FLAG_TERMINATED | FLAG_TRUNCATED]) +
            b[11:],
            lambda b: b[:10] + b"\x04" + b[11:],               # undefined flag
            lambda b: b[:88] + b"\x0b" + b[89:],               # command count 11
    ):
        try:
            parse_header(mutate(bytearray(raw)))
        except WireError:
            pass
        else:
            raise AssertionError("bad header accepted")

    # Payload size formula anchors (plan 12.2 + slot extension).
    assert act_request_payload_bytes(0, 0, 0) == 3624
    assert act_request_payload_bytes(1, 0, 0) == 3624 + 329
    assert act_request_payload_bytes(1, 2, 5) == 3624 + 329 + 152 + 320 + 8
    assert act_request_payload_bytes(2, 33, 65, terminal=True) == \
        4 + 3624 + 658 + 76 * 33 + 64 * 65 + 4 * 2 * (2 + 3)

    body, req_header = _fixture_request()
    payload = pack_act_request(body)
    assert len(payload) == act_request_payload_bytes(2, 1, 5)
    req_header.payload_bytes = len(payload)
    req_header.payload_crc32 = crc32(payload)
    request = parse_act_request(req_header, payload)
    assert request["own_id"] == [0x1D0, 0x3A0]
    assert request["spendable_primary"] == 400
    assert request["economy_reward_material"][9] == 10
    assert request["start_candidates"][0].cell == 9 and \
        request["start_candidates"][1].explored == 0 and \
        request["start_candidates"][2].cell == -1
    assert request["intent_reward_material"] == [1, 2, 0, 0xFFFFFFFF]
    assert request["candidates"][1].width == 3 and \
        request["candidates"][1].footprint_rect() == (40, 28, 43, 31)
    assert request["economy_pair_mask_words"] == [[7], [24]]
    # Byte anchors: budget after global, slot blocks at 3336, own prefix at
    # 3624, appendix after 207*U, slot id after 36*U within the appendix.
    assert payload[3208:3212] == struct.pack("<I", 400)
    assert payload[3336:3340] == struct.pack("<I", 0)          # MAIN member_count
    assert payload[3336 + 144:3336 + 148] == struct.pack("<i", 9)  # start cand 0
    assert payload[3624:3628] == struct.pack("<I", 0x1D0)
    assert payload[3624 + 414:3624 + 414 + 4] == struct.pack(
        "<I", CAP_MOVE | CAP_HARVEST | CAP_BUILD)
    assert payload[3624 + 414 + 72:3624 + 414 + 74] == bytes([SLOT_NONE, SLOT_NONE])
    target_offset = 3624 + 414 + 84 + 160
    assert payload[target_offset:target_offset + 4] == struct.pack("<I", 0x5D0)

    # Contract-semantic rejections.
    def expect_reject(mutator, reason):
        bad = {k: (list(v) if isinstance(v, list) else v)
               for k, v in body.items()}
        bad["candidates"] = list(body["candidates"])
        mutator(bad)
        bad_payload = pack_act_request(bad)
        bad_header = Header(**{**req_header.__dict__,
                               "payload_bytes": len(bad_payload),
                               "payload_crc32": crc32(bad_payload)})
        try:
            parse_act_request(bad_header, bad_payload)
        except WireError:
            return
        raise AssertionError("accepted: " + reason)

    def dup_candidate(b):
        b["candidates"][2] = b["candidates"][1]
    expect_reject(dup_candidate, "duplicate (kind,key)")

    def bad_slot_mask(b):
        b["slot_command_mask"] = [1 | (1 << SLOT_COMMAND_COUNT), 1, 1, 1]
    expect_reject(bad_slot_mask, "unused slot command in mask")

    def bad_assign_mask(b):
        b["own_assign_mask"] = [1, 0]
    expect_reject(bad_assign_mask, "assign mask on slotless row")

    def bad_start(b):
        b["start_candidates"] = [StartCandidate(64, 0, 0)] + [StartCandidate()] * 7
    expect_reject(bad_start, "start candidate cell 64")

    def high_cmd_bits(b):
        b["command_mask"] = [b["command_mask"][0] | (1 << COMMAND_COUNT),
                             b["command_mask"][1]]
    expect_reject(high_cmd_bits, "command mask high bits")

    # TERMINAL prefixes the outcome word.
    terminal_payload = pack_act_request(body, terminal_outcome=TERMINAL_WIN)
    terminal_header = Header(**{**req_header.__dict__, "kind": KIND_TERMINAL,
                                "payload_bytes": len(terminal_payload),
                                "payload_crc32": crc32(terminal_payload)})
    assert parse_act_request(terminal_header, terminal_payload,
                             terminal=True)["terminal_outcome"] == TERMINAL_WIN

    # Ledger replay (plan 10): worker BUILDs site 1 (300 primary) -> the
    # building's PRODUCE (150) no longer fits 100 remaining; RESEARCH (400)
    # was never affordable at 400-300.
    replay = replay_ledger(request, req_header, [COMMAND_BUILD, COMMAND_KEEP],
                           [1, -1])
    assert replay["remaining_budget"] == [(400, 0, 3), (100, 0, 3)]
    assert replay["dynamic_command_mask"][1] == (1 << COMMAND_KEEP)
    assert stochastic_rows(replay["dynamic_command_mask"]) == [True, False]
    assert replay["dynamic_assign_mask"] == [0, 0]

    # Slot fixture: assign ledger closes SCOUT after the first taker in
    # canonical order; the commander mask/cell legality mirrors C++.
    sbody, sheader = _slot_fixture_request()
    spayload = pack_act_request(sbody)
    sheader.payload_bytes = len(spayload)
    sheader.payload_crc32 = crc32(spayload)
    srequest = parse_act_request(sheader, spayload)
    assert scout_free_at_snapshot(srequest) == 0
    srep = replay_ledger(srequest, sheader, [0, 0, 0, 0], [-1] * 4,
                         assigns=[0, 0, 4, 0])
    assert srep["assign_legal"] == [True, True, False, True]
    assert (srep["dynamic_assign_mask"][2] & (1 << SLOT_SCOUT)) == 0
    free_body = dict(sbody)
    free_body["slots"] = list(sbody["slots"])
    free_body["slots"][SLOT_SCOUT] = SlotBlock()
    free_body["own_slot_id"] = [SLOT_NONE, SLOT_NONE, SLOT_MAIN, SLOT_MAIN]
    free_body["own_assign_mask"] = [0, 0, 0b1110, 0b1110]
    fpayload = pack_act_request(free_body)
    fheader = Header(**{**sheader.__dict__, "payload_bytes": len(fpayload),
                        "payload_crc32": crc32(fpayload)})
    frequest = parse_act_request(fheader, fpayload)
    assert scout_free_at_snapshot(frequest) == 1
    frep = replay_ledger(frequest, fheader, [0] * 4, [-1] * 4, assigns=[0, 0, 4, 4])
    assert frep["assign_legal"] == [True, True, True, False]
    assert (frep["dynamic_assign_mask"][2] & (1 << SLOT_SCOUT)) != 0
    assert (frep["dynamic_assign_mask"][3] & (1 << SLOT_SCOUT)) == 0
    assert slot_choice_legal(srequest, SLOT_MAIN, SLOT_COMMAND_STOP, -1)
    assert not slot_choice_legal(srequest, SLOT_RAID_A, SLOT_COMMAND_STOP, -1)
    assert slot_choice_legal(srequest, SLOT_RAID_A, SLOT_COMMAND_ATTACK_MOVE, 63)
    assert not slot_choice_legal(srequest, SLOT_RAID_A, SLOT_COMMAND_ATTACK_MOVE, 64)
    assert not slot_choice_legal(srequest, SLOT_MAIN, SLOT_COMMAND_HOLD, 5)

    # Reply / outcome.
    reply = pack_reply([COMMAND_BUILD, COMMAND_KEEP], [1, -1], req_header,
                       assigns=[0, 0], slot_commands=[SLOT_COMMAND_ATTACK_MOVE, 0, 0, 0],
                       slot_cells=[58, -1, -1, -1])
    assert len(reply) == 2 * 6 + 4 + 16
    parsed_reply = parse_reply(req_header, reply)
    assert parsed_reply["command"] == [COMMAND_BUILD, 0] and \
        parsed_reply["argument"] == [1, -1]
    assert parsed_reply["slot_command"] == [2, 0, 0, 0] and \
        parsed_reply["slot_cell"] == [58, -1, -1, -1]
    for kwargs in ({"commands": [COMMAND_KEEP, COMMAND_KEEP], "arguments": [0, -1]},
                   {"commands": [COMMAND_MOVE, COMMAND_KEEP], "arguments": [96, -1]},
                   {"commands": [COMMAND_BUILD, COMMAND_KEEP], "arguments": [5, -1]},
                   {"commands": [0, 0], "arguments": [-1, -1], "assigns": [5, 0]},
                   {"commands": [0, 0], "arguments": [-1, -1],
                    "slot_commands": [SLOT_COMMAND_COUNT, 0, 0, 0]},
                   {"commands": [0, 0], "arguments": [-1, -1],
                    "slot_cells": [3, -1, -1, -1]}):
        try:
            pack_reply(header=req_header, **kwargs)
        except WireError:
            continue
        raise AssertionError("out-of-domain reply accepted: %r" % kwargs)
    outcome_payload = pack_outcome([RESULT_PUBLISHED, RESULT_KEPT],
                                   [REJECT_NONE, REJECT_NONE], [True, False],
                                   slot_results=[RESULT_PUBLISHED, 0, 0, 0],
                                   slot_trainable_bits=0xF,
                                   assign_trainable=[False, True])
    assert len(outcome_payload) == 4 * 2 + 4 + 16 + 4 + 4
    outcome = parse_outcome(req_header, outcome_payload)
    assert outcome["result"] == [2, 0] and outcome["trainable"] == [True, False]
    assert outcome["slot_result"][0] == RESULT_PUBLISHED and \
        outcome["slot_trainable"] == [True] * 4 and \
        outcome["assign_trainable"] == [False, True]
    for results, rejects, trainable in (
            ([RESULT_REJECTED_MASK, RESULT_KEPT], [REJECT_NONE, 0], [False, False]),
            ([RESULT_PUBLISHED, RESULT_KEPT], [REJECT_MASKED, 0], [True, False]),
            ([RESULT_REJECTED_CONFLICT, RESULT_KEPT], [REJECT_SITE_CONFLICT, 0],
             [True, False])):
        try:
            parse_outcome(req_header, pack_outcome(results, rejects, trainable))
        except WireError:
            continue
        raise AssertionError("inconsistent outcome accepted")

    # HELLO round trip + ordering violation.
    hello = HelloBody(controlled_owner_mask=0b11, owners=[
        HelloOwnerRecord(0, 0b10), HelloOwnerRecord(1, 0b01)])
    packed = pack_hello(hello)
    assert len(packed) == 16 + 2 * 48
    assert [o.owner for o in parse_hello(packed).owners] == [0, 1]
    hello.owners.reverse()
    try:
        parse_hello(pack_hello(hello))
    except WireError:
        pass
    else:
        raise AssertionError("non-ascending hello accepted")

    # SHD3 round trip with teacher-forced ledger verification (economy +
    # assign + commander labels).
    labels = [ShadowLabel(SHADOW_ISSUE, COMMAND_BUILD, 0, 1, 1.0),
              ShadowLabel(SHADOW_KEEP, 0, 0, -1, 0.5)]
    record = pack_shadow_record(req_header, payload, labels,
                                replay["dynamic_command_mask"],
                                replay["remaining_budget"],
                                replay["dynamic_economy_pair_mask_words"],
                                replay["dynamic_assign_mask"])
    records = list(parse_shadow_records(record * 2, "fixture"))
    assert len(records) == 2 and records[0].labels[0].command == COMMAND_BUILD
    assert records[1].remaining_budget[1] == (100, 0, 3)
    assert records[0].slot_labels[0].label == SHADOW_KEEP
    slabels = [ShadowLabel(SHADOW_KEEP, 0, 0, -1, 1.0),
               ShadowLabel(SHADOW_KEEP, 0, 0, -1, 1.0),
               ShadowLabel(SHADOW_KEEP, 0, 0, -1, 1.0, SHADOW_EXCLUDED, 0),
               ShadowLabel(SHADOW_KEEP, 0, 0, -1, 1.0, SHADOW_ISSUE, SLOT_RAID_A + 1)]
    slot_labels = [ShadowSlotLabel(), ShadowSlotLabel(SHADOW_ISSUE, SLOT_COMMAND_MOVE, 9),
                   ShadowSlotLabel(), ShadowSlotLabel()]
    srep2 = replay_ledger(srequest, sheader, [0] * 4, [-1] * 4,
                          assigns=[0, 0, 0, SLOT_RAID_A + 1])
    srecord = pack_shadow_record(sheader, spayload, slabels,
                                 srep2["dynamic_command_mask"], srep2["remaining_budget"],
                                 srep2["dynamic_economy_pair_mask_words"],
                                 srep2["dynamic_assign_mask"], slot_labels)
    parsed = list(parse_shadow_records(srecord))
    assert parsed[0].labels[3].assign == SLOT_RAID_A + 1 and \
        parsed[0].slot_labels[1].cell == 9
    bad_slot = list(slot_labels)
    bad_slot[1] = ShadowSlotLabel(SHADOW_ISSUE, SLOT_COMMAND_MOVE, 64)
    try:
        list(parse_shadow_records(pack_shadow_record(
            sheader, spayload, slabels, srep2["dynamic_command_mask"],
            srep2["remaining_budget"], srep2["dynamic_economy_pair_mask_words"],
            srep2["dynamic_assign_mask"], bad_slot)))
    except WireError:
        pass
    else:
        raise AssertionError("illegal slot label accepted")
    # Tampered dynamic block / SHD2 magic are rejected.
    bad_record = pack_shadow_record(req_header, payload, labels,
                                    replay["dynamic_command_mask"],
                                    [(400, 0, 3), (400, 0, 3)],
                                    replay["dynamic_economy_pair_mask_words"])
    try:
        list(parse_shadow_records(bad_record))
    except WireError:
        pass
    else:
        raise AssertionError("tampered shadow ledger block accepted")
    try:
        list(parse_shadow_records(b"SHD3" + record[4:]))
    except WireError:
        pass
    else:
        raise AssertionError("SHD3 record accepted by the SHD5 reader")

    print("ranker_entity2_contract: selftest passed")


if __name__ == "__main__":
    selftest()
