# -*- coding: utf-8 -*-
"""act2 entity-command wire contract (protocol 2).

Python mirror of include/ranker_ai_entity_control.h /
src/ranker_ai_entity_control.cpp (plan: docs/AI_PLAY_ENTITY_COMMAND_RL_PLAN.md
section 11).  The C++ side is normative; the golden anchors in `selftest()`
pin both implementations to the same bytes.

Framing rules (plan 11.2): any header/size/version/CRC mismatch is a
`WireError` — the connection must be closed, never adapted to.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from typing import Dict, List, Sequence

HEADER_BYTES = 96
MAGIC = b"RAI2"
CONTRACT_ID = b"ENTCMD01"
PROTOCOL = 2
# observation / global-feature / entity-feature / entity-action / semantic /
# point-geometry versions, in header order.
VERSIONS = (5, 10, 1, 1, 2, 1)
GLOBAL_COUNT = 802
MACRO_ACTION_COUNT = 80
MACRO_MASK_WORDS = 3
COMMAND_COUNT = 7
POINT_COUNT = 96
POINT_MASK_WORDS = 3
OWN_CONTINUOUS_COUNT = 33
TARGET_CONTINUOUS_COUNT = 14
WIRE_ROW_LIMIT = 2048
MAX_PAYLOAD_BYTES = 16 * 1024 * 1024

KIND_HELLO = 1
KIND_ACK = 2
KIND_ACT_REQ = 3
KIND_ACT_REPLY = 4
KIND_OUTCOME = 5
KIND_TERMINAL = 6
KIND_ERROR = 7

FLAG_MACRO_DUE = 1 << 0
FLAG_TERMINATED = 1 << 1
FLAG_TRUNCATED = 1 << 2
_KNOWN_FLAGS = FLAG_MACRO_DUE | FLAG_TERMINATED | FLAG_TRUNCATED

# OUTCOME result / reject enums (plan 11.1).
RESULT_KEPT = 0
RESULT_DEDUPED = 1
RESULT_PUBLISHED = 2
RESULT_REJECTED_MASK = 3
RESULT_REJECTED_STALE = 4
RESULT_PLANNER_FAILED = 5
RESULT_ENCODE_FAILED = 6
RESULT_NOT_DUE = 7
RESULT_TRANSACTION_ABORTED = 8

TERMINAL_ONGOING = 0
TERMINAL_WIN = 1
TERMINAL_LOSS = 2
TERMINAL_DRAW = 3


class WireError(RuntimeError):
    """Hard framing/contract violation: close the connection."""


def crc32(data: bytes) -> int:
    """IEEE reflected CRC32 (poly 0xedb88320, init/xor-out 0xffffffff)."""
    return zlib.crc32(data) & 0xFFFFFFFF


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
    payload_crc32: int = 0
    entity_policy_version: int = 0
    macro_policy_version: int = 0


_HEADER_STRUCT = struct.Struct(
    "<4sHHHHI8s6H" + "5I" + "2I" + "4I" + "I" + "2I" + "I")
assert _HEADER_STRUCT.size == HEADER_BYTES


def pack_header(header: Header) -> bytes:
    return _HEADER_STRUCT.pack(
        MAGIC, HEADER_BYTES, PROTOCOL, header.kind, header.flags,
        header.payload_bytes, CONTRACT_ID, *VERSIONS,
        header.owner, header.episode, header.frame, header.sequence,
        header.reply_to_sequence,
        header.own_rows, header.target_rows,
        GLOBAL_COUNT, MACRO_ACTION_COUNT, COMMAND_COUNT, POINT_COUNT,
        header.payload_crc32,
        header.entity_policy_version, header.macro_policy_version,
        0)


def parse_header(data: bytes) -> Header:
    if len(data) < HEADER_BYTES:
        raise WireError("short header")
    fields = _HEADER_STRUCT.unpack(data[:HEADER_BYTES])
    (magic, header_bytes, protocol, kind, flags, payload_bytes, contract,
     v0, v1, v2, v3, v4, v5,
     owner, episode, frame, sequence, reply_to,
     own_rows, target_rows,
     global_count, macro_count, command_count, point_count,
     payload_crc, entity_version, macro_version, reserved) = fields
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
    if (v0, v1, v2, v3, v4, v5) != VERSIONS:
        raise WireError("contract version mismatch")
    if (global_count, macro_count, command_count, point_count) != (
            GLOBAL_COUNT, MACRO_ACTION_COUNT, COMMAND_COUNT, POINT_COUNT):
        raise WireError("fixed count mismatch")
    if own_rows > WIRE_ROW_LIMIT or target_rows > WIRE_ROW_LIMIT:
        raise WireError("row count above wire hard limit")
    if reserved != 0:
        raise WireError("reserved field nonzero")
    return Header(kind, flags, payload_bytes, owner, episode, frame, sequence,
                  reply_to, own_rows, target_rows, payload_crc,
                  entity_version, macro_version)


def act_request_payload_bytes(own_rows: int, target_rows: int,
                              terminal: bool = False) -> int:
    if own_rows > WIRE_ROW_LIMIT or target_rows > WIRE_ROW_LIMIT:
        raise WireError("row count above wire hard limit")
    pair_words = (target_rows + 31) // 32
    size = 3260 + 207 * own_rows + 76 * target_rows + 4 * own_rows * pair_words
    return size + (4 if terminal else 0)


def expand_bitmask(words: Sequence[int], count: int) -> List[bool]:
    """LSB-first bit expansion; the count-exceeding high bits must be zero."""
    bits = [bool((words[i >> 5] >> (i & 31)) & 1) for i in range(count)]
    total_bits = len(words) * 32
    for i in range(count, total_bits):
        if (words[i >> 5] >> (i & 31)) & 1:
            raise WireError("bitset high bits above count are nonzero")
    return bits


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


# SoA field order of the ACT_REQ body (plan 11.1); (name, struct char, per-row
# element count).  Serialization is field-major.
_OWN_FIELDS = (
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
_TARGET_FIELDS = (
    ("target_id", "I", 1), ("target_generation", "I", 1),
    ("target_type_id", "H", 1), ("target_owner", "B", 1),
    ("target_role", "B", 1), ("target_render_class", "I", 1),
    ("target_kind_bits", "I", 1),
    ("target_feature", "f", TARGET_CONTINUOUS_COUNT),
)


def parse_act_request(header: Header, payload: bytes,
                      terminal: bool = False) -> Dict:
    expected = act_request_payload_bytes(header.own_rows, header.target_rows,
                                         terminal)
    if len(payload) != expected:
        raise WireError("ACT_REQ payload size mismatch")
    if crc32(payload) != header.payload_crc32:
        raise WireError("payload CRC mismatch")
    r = _Reader(payload)
    out: Dict = {}
    if terminal:
        out["terminal_outcome"] = r.take("<I")[0]
    out["global"] = r.array("f", GLOBAL_COUNT)
    out["macro_gate"] = r.array("f", 2)
    out["macro_mask_words"] = r.array("I", MACRO_MASK_WORDS)
    out["cumulative_losses"] = r.array("Q", 4)
    u = header.own_rows
    e = header.target_rows
    for name, fmt, per_row in _OWN_FIELDS:
        values = r.array(fmt, u * per_row)
        if per_row > 1:
            values = [values[i * per_row:(i + 1) * per_row] for i in range(u)]
        out[name] = values
    for name, fmt, per_row in _TARGET_FIELDS:
        values = r.array(fmt, e * per_row)
        if per_row > 1:
            values = [values[i * per_row:(i + 1) * per_row] for i in range(e)]
        out[name] = values
    pair_words = (e + 31) // 32
    flat = r.array("I", u * pair_words)
    out["attack_pair_mask_words"] = [
        flat[i * pair_words:(i + 1) * pair_words] for i in range(u)]
    r.done()
    return out


def pack_act_request(body: Dict, terminal_outcome: int = None) -> bytes:
    """Inverse of parse_act_request (fixtures / mock servers)."""
    u = len(body["own_id"])
    e = len(body["target_id"])
    parts: List[bytes] = []
    if terminal_outcome is not None:
        parts.append(struct.pack("<I", terminal_outcome))
    parts.append(struct.pack("<%df" % GLOBAL_COUNT, *body["global"]))
    parts.append(struct.pack("<2f", *body["macro_gate"]))
    parts.append(struct.pack("<%dI" % MACRO_MASK_WORDS,
                             *body["macro_mask_words"]))
    parts.append(struct.pack("<4Q", *body["cumulative_losses"]))
    for name, fmt, per_row in _OWN_FIELDS:
        values = body[name]
        if per_row > 1:
            values = [x for row in values for x in row]
        parts.append(struct.pack("<%d%s" % (u * per_row, fmt), *values))
    for name, fmt, per_row in _TARGET_FIELDS:
        values = body[name]
        if per_row > 1:
            values = [x for row in values for x in row]
        parts.append(struct.pack("<%d%s" % (e * per_row, fmt), *values))
    pair_words = (e + 31) // 32
    flat = [x for row in body["attack_pair_mask_words"] for x in row]
    parts.append(struct.pack("<%dI" % (u * pair_words), *flat))
    payload = b"".join(parts)
    expected = act_request_payload_bytes(u, e, terminal_outcome is not None)
    if len(payload) != expected:
        raise WireError("packed ACT_REQ size drifted from the formula")
    return payload


def pack_reply(macro: int, macro_target: int, commands: Sequence[int],
               points: Sequence[int], targets: Sequence[int]) -> bytes:
    u = len(commands)
    if len(points) != u or len(targets) != u:
        raise WireError("reply arrays must share the own-row length")
    if not 0 <= macro < MACRO_ACTION_COUNT:
        raise WireError("macro action out of range")
    if not -1 <= macro_target < 64:
        raise WireError("macro target out of range")
    for command in commands:
        if not 0 <= command < COMMAND_COUNT:
            raise WireError("entity command out of range")
    for point in points:
        if not -1 <= point < POINT_COUNT:
            raise WireError("point token out of range")
    for target in targets:
        if not -1 <= target < WIRE_ROW_LIMIT:
            raise WireError("target row out of range")
    return (struct.pack("<2i", macro, macro_target) +
            struct.pack("<%dB" % u, *commands) +
            struct.pack("<%di" % u, *points) +
            struct.pack("<%di" % u, *targets))


def parse_outcome(header: Header, payload: bytes) -> Dict:
    u = header.own_rows
    mask_words = (u + 31) // 32
    expected = 8 + 4 * u + 4 * mask_words
    if len(payload) != expected:
        raise WireError("outcome payload size mismatch")
    r = _Reader(payload)
    macro_result, macro_reject, macro_trainable, z0, z1, z2 = r.take("<HHBBBB")
    if (z0, z1, z2) != (0, 0, 0):
        raise WireError("outcome reserved bytes nonzero")
    entity_result = r.array("H", u)
    entity_reject = r.array("H", u)
    trainable_words = r.array("I", mask_words)
    r.done()
    trainable = expand_bitmask(trainable_words, u) if u else []
    # Trainable bit must equal (result in 0..2) exactly (plan 11.1).
    for index in range(u):
        expected_bit = entity_result[index] <= RESULT_PUBLISHED
        if trainable[index] != expected_bit:
            raise WireError("entity trainable bit contradicts its result")
    return {
        "macro_result": macro_result,
        "macro_reject_code": macro_reject,
        "macro_trainable": macro_trainable,
        "entity_result": entity_result,
        "entity_reject_code": entity_reject,
        "trainable": trainable,
    }


@dataclass
class HelloOwnerRecord:
    owner: int
    frozen_hostile_owner_mask: int
    requested_entity_version: int = 0xFFFFFFFF
    requested_macro_version: int = 0xFFFFFFFF
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
            record.requested_entity_version, record.requested_macro_version,
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
        owner, hostile_mask, entity_v, macro_v, sha = r.take("<4I32s")
        if owner <= previous:
            raise WireError("hello owner records not ascending")
        if owner >= 32 or not (owner_mask >> owner) & 1:
            raise WireError("hello owner not in controlled mask")
        previous = owner
        owners.append(HelloOwnerRecord(owner, hostile_mask, entity_v, macro_v,
                                       sha))
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
# -AISHADOW dataset records (plan 13.1): 'SHD1' + u32 body size + wire header
# + ACT_REQ payload + u32 label count + 16-byte labels.
# ---------------------------------------------------------------------------

SHADOW_MAGIC = b"SHD1"
SHADOW_KEEP = 0
SHADOW_ISSUE = 1
SHADOW_EXCLUDED = 2
SHADOW_REASON_NONE = 0
SHADOW_REASON_UNSUPPORTED_KIND = 1
SHADOW_REASON_STALE_TARGET = 2
SHADOW_REASON_POINT_ERROR = 3
SHADOW_REASON_MASKED = 4
_SHADOW_LABEL_STRUCT = struct.Struct("<BBHiif")
assert _SHADOW_LABEL_STRUCT.size == 16


@dataclass
class ShadowLabel:
    label: int
    command: int
    exclude_reason: int
    point: int
    target: int
    inclusion_probability: float


def parse_shadow_records(data: bytes):
    """Yield (header, body_dict, [ShadowLabel]) per record."""
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
        header = parse_header(body[:HEADER_BYTES])
        payload = body[HEADER_BYTES:HEADER_BYTES + header.payload_bytes]
        if len(payload) != header.payload_bytes:
            raise WireError("shadow payload truncated")
        request = parse_act_request(header, payload)
        rest = body[HEADER_BYTES + header.payload_bytes:]
        if len(rest) < 4:
            raise WireError("shadow label count missing")
        (label_count,) = struct.unpack_from("<I", rest)
        if label_count != header.own_rows or \
                len(rest) != 4 + label_count * _SHADOW_LABEL_STRUCT.size:
            raise WireError("shadow label block size mismatch")
        labels = [ShadowLabel(*_SHADOW_LABEL_STRUCT.unpack_from(rest, 4 + i *
                              _SHADOW_LABEL_STRUCT.size))
                  for i in range(label_count)]
        yield header, request, labels


# ---------------------------------------------------------------------------


def selftest() -> None:
    # CRC check value.
    assert crc32(b"123456789") == 0xCBF43926

    # Header golden anchors shared with the C++ regression
    # (tests/ai_play_interface_regression.cpp, test_ai_entity_wire_contract).
    header = Header(kind=KIND_ACT_REQ, flags=FLAG_MACRO_DUE,
                    payload_bytes=3260, owner=1, episode=37, frame=1232,
                    sequence=154, reply_to_sequence=153, own_rows=2,
                    target_rows=3, payload_crc32=0x12345678,
                    entity_policy_version=21, macro_policy_version=8)
    raw = pack_header(header)
    assert len(raw) == HEADER_BYTES
    assert raw[0:4] == MAGIC and raw[4] == 96 and raw[6] == PROTOCOL
    assert raw[16:24] == CONTRACT_ID
    assert raw[24] == 5 and raw[26] == 10 and raw[28] == 1
    assert raw[30] == 1 and raw[32] == 2 and raw[34] == 1
    assert raw[64] == 0x22 and raw[65] == 0x03 and raw[68] == 80
    assert raw[72] == 7 and raw[76] == 96
    parsed = parse_header(raw)
    assert parsed == header

    for mutate, _reason in (
            (lambda b: b[:0] + b"X" + b[1:], "magic"),
            (lambda b: b[:26] + b"\x09" + b[27:], "version"),
            (lambda b: b[:92] + b"\x01" + b[93:], "reserved"),
            (lambda b: b[:10] + bytes([FLAG_TERMINATED | FLAG_TRUNCATED]) +
             b[11:], "terminal flags"),
            (lambda b: b[:10] + b"\x08" + b[11:], "undefined flag"),
    ):
        try:
            parse_header(mutate(bytearray(raw)))
        except WireError:
            pass
        else:
            raise AssertionError("bad header accepted")

    # ACT_REQ pack/parse round trip (U=1, E=2).
    body = {
        "global": [0.0] * GLOBAL_COUNT,
        "macro_gate": [0.125, 0.875],
        "macro_mask_words": [1, 0, 0],
        "cumulative_losses": [120, 0, 350, 80],
        "own_id": [0x1D0], "own_generation": [1], "own_control_epoch": [1],
        "own_type_id": [5], "own_movement_class": [0],
        "own_distance_check_mode": [0], "own_role": [0],
        "own_render_class": [1], "own_command_base": [0],
        "own_command_state_high_flags": [0], "own_unit_command_flags": [0],
        "own_movement_state": [0], "own_semantic_order": [0],
        "own_order_status": [0], "own_presence_bits": [3],
        "own_engine_order_match": [0], "own_last_attempt_command": [255],
        "own_last_attempt_result": [255], "own_last_reject_code": [0],
        "own_active_target_row": [-1],
        "own_attackable_class_mask": [0xFFFFFFFF],
        "own_feature": [[0.0] * OWN_CONTINUOUS_COUNT],
        "command_mask": [0x7F],
        "point_mask": [[0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF]],
        "target_id": [0x5D0, 0x8D0], "target_generation": [1, 1],
        "target_type_id": [5, 5], "target_owner": [1, 8],
        "target_role": [0, 0], "target_render_class": [1, 1],
        "target_kind_bits": [1, 5],
        "target_feature": [[0.0] * TARGET_CONTINUOUS_COUNT] * 2,
        "attack_pair_mask_words": [[3]],
    }
    payload = pack_act_request(body)
    assert len(payload) == act_request_payload_bytes(1, 2)
    request_header = Header(kind=KIND_ACT_REQ, own_rows=1, target_rows=2,
                            payload_bytes=len(payload),
                            payload_crc32=crc32(payload))
    parsed_body = parse_act_request(request_header, payload)
    assert parsed_body["own_id"] == [0x1D0]
    assert parsed_body["cumulative_losses"] == [120, 0, 350, 80]
    assert parsed_body["attack_pair_mask_words"] == [[3]]
    assert parsed_body["target_owner"] == [1, 8]
    assert expand_bitmask(parsed_body["command_mask"], COMMAND_COUNT) == \
        [True] * 7

    # TERMINAL prefixes the outcome word.
    terminal_payload = pack_act_request(body, terminal_outcome=TERMINAL_WIN)
    terminal_header = Header(kind=KIND_TERMINAL, own_rows=1, target_rows=2,
                             payload_bytes=len(terminal_payload),
                             payload_crc32=crc32(terminal_payload))
    parsed_terminal = parse_act_request(terminal_header, terminal_payload,
                                        terminal=True)
    assert parsed_terminal["terminal_outcome"] == TERMINAL_WIN

    # Reply and outcome.
    reply = pack_reply(0, -1, [0, 4], [-1, -1], [-1, 0])
    assert len(reply) == 8 + 2 * 9
    outcome_payload = (struct.pack("<HHBBBB", RESULT_NOT_DUE, 0, 0, 0, 0, 0) +
                       struct.pack("<2H", RESULT_KEPT, RESULT_PUBLISHED) +
                       struct.pack("<2H", 0, 0) + struct.pack("<I", 3))
    outcome_header = Header(kind=KIND_OUTCOME, own_rows=2)
    outcome = parse_outcome(outcome_header, outcome_payload)
    assert outcome["entity_result"] == [RESULT_KEPT, RESULT_PUBLISHED]
    assert outcome["trainable"] == [True, True]
    bad_outcome = (struct.pack("<HHBBBB", RESULT_NOT_DUE, 0, 0, 0, 0, 0) +
                   struct.pack("<2H", RESULT_REJECTED_MASK, RESULT_PUBLISHED) +
                   struct.pack("<2H", 2, 0) + struct.pack("<I", 3))
    try:
        parse_outcome(outcome_header, bad_outcome)
    except WireError:
        pass
    else:
        raise AssertionError("trainable/result contradiction accepted")

    # HELLO round trip + ordering violation.
    hello = HelloBody(controlled_owner_mask=0b11, owners=[
        HelloOwnerRecord(0, 0b10), HelloOwnerRecord(1, 0b01)])
    packed = pack_hello(hello)
    assert len(packed) == 16 + 2 * 48
    reparsed = parse_hello(packed)
    assert [o.owner for o in reparsed.owners] == [0, 1]
    hello.owners.reverse()
    try:
        parse_hello(pack_hello(hello))
    except WireError:
        pass
    else:
        raise AssertionError("non-ascending hello accepted")

    # Shadow record round trip (built from the same packers).
    shadow_header = Header(kind=KIND_ACT_REQ, own_rows=1, target_rows=2,
                           payload_bytes=len(payload),
                           payload_crc32=crc32(payload))
    label_bytes = _SHADOW_LABEL_STRUCT.pack(SHADOW_ISSUE, 4, 0, -1, 0, 1.0)
    record = (SHADOW_MAGIC +
              struct.pack("<I", HEADER_BYTES + len(payload) + 4 + 16) +
              pack_header(shadow_header) + payload +
              struct.pack("<I", 1) + label_bytes)
    records = list(parse_shadow_records(record * 2))
    assert len(records) == 2
    parsed_header, parsed_request, parsed_labels = records[0]
    assert parsed_header.own_rows == 1
    assert parsed_request["own_id"] == [0x1D0]
    assert parsed_labels[0].label == SHADOW_ISSUE
    assert parsed_labels[0].command == 4
    assert parsed_labels[0].inclusion_probability == 1.0

    print("ranker_entity_contract: selftest passed")


if __name__ == "__main__":
    selftest()
