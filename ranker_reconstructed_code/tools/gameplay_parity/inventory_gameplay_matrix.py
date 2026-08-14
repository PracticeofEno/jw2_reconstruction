#!/usr/bin/env python3
"""Build an exhaustive, evidence-aware Ranker gameplay parity inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import zlib
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


TRC_HEADER_SIZE = 0x20
TRC_ENTRY_SIZE = 0x20
UNIT_CATALOG_COUNT = 170
ACTION_CATALOG_COUNT = 0x2E
ATTACK_CATALOG_COUNT = 0x3D
SCENARIO_OBJECT_STRIDE = 0x1D0
SCENARIO_ACTIVE_HEAD_OFFSET = 0x143C
SCENARIO_LIFECYCLE_HEAD_OFFSET = 0x1444
SCENARIO_OBJECT_RECORD = 7
REPLAY_HEADER_SIZE = 0x20FF
REPLAY_PACKET_SIZE = 0x24


def u8(data: bytes, offset: int) -> int:
    return data[offset] if offset < len(data) else 0


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data + b"\0" * 2, offset)[0] if offset + 2 <= len(data) else 0


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data + b"\0" * 4, offset)[0] if offset + 4 <= len(data) else 0


def i32(data: bytes, offset: int) -> int:
    value = u32(data, offset)
    return value - 0x100000000 if value >= 0x80000000 else value


def decode_text(raw: bytes) -> str:
    raw = raw.split(b"\0", 1)[0]
    for encoding in ("cp949", "ascii", "latin1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    return raw.hex()


def embedded_name(data: bytes, fallback: str, offset: int = 0x10C) -> str:
    if len(data) <= offset:
        return fallback
    name = decode_text(data[offset:offset + 0x40])
    return name or fallback


@dataclass(frozen=True)
class TrcRecord:
    index: int
    name: str
    original_size: int
    stored_size: int
    check_value: int
    method: int
    reserved: int
    data: bytes
    stored_data: bytes


class TrcArchive:
    def __init__(self, path: Path):
        self.path = path
        raw = path.read_bytes()
        if len(raw) < TRC_HEADER_SIZE or raw[:4] != b"TRC\x1a":
            raise ValueError(f"not a TRC archive: {path}")
        slots = u32(raw, 4)
        active = u32(raw, 8)
        data_offset = u32(raw, 12)
        self.directory_slots = slots
        if TRC_HEADER_SIZE + slots * TRC_ENTRY_SIZE > data_offset:
            raise ValueError(f"invalid TRC directory: {path}")
        self.records: list[TrcRecord] = []
        for index in range(min(slots, active)):
            base = TRC_HEADER_SIZE + index * TRC_ENTRY_SIZE
            name = decode_text(raw[base:base + 12])
            relative = u32(raw, base + 0x0C)
            original_size = u32(raw, base + 0x10)
            stored_size = u32(raw, base + 0x14)
            check_value = u16(raw, base + 0x18)
            method = u16(raw, base + 0x1A)
            reserved = u32(raw, base + 0x1C)
            start = data_offset + relative
            stored = raw[start:start + stored_size]
            if len(stored) != stored_size:
                raise ValueError(f"truncated TRC record {index}: {path}")
            if method == 2:
                data = zlib.decompress(stored)
            elif method == 0:
                data = stored
            else:
                raise ValueError(f"unsupported TRC method {method}: {path} record {index}")
            if len(data) != original_size:
                raise ValueError(f"wrong TRC record size {index}: {path}")
            self.records.append(TrcRecord(
                index, name, original_size, stored_size, check_value, method,
                reserved, data, stored))

    def record(self, index: int) -> TrcRecord:
        return self.records[index]


def replace_trc_record(record: TrcRecord, data: bytes) -> TrcRecord:
    return TrcRecord(record.index, record.name, len(data), 0,
                     sum(data) & 0xFFFF, record.method, record.reserved,
                     data, b"")


def append_trc_record(records: list[TrcRecord], name: str, data: bytes,
                      method: int = 2) -> list[TrcRecord]:
    result = list(records)
    result.append(TrcRecord(len(result), name, len(data), 0,
                            sum(data) & 0xFFFF, method, 0, data, b""))
    return result


def write_trc_archive(path: Path, records: list[TrcRecord],
                      directory_slots: int) -> None:
    directory_slots = max(directory_slots, len(records))
    stored_records: list[bytes] = []
    for record in records:
        if record.stored_data:
            stored = record.stored_data
        elif record.method == 2:
            stored = zlib.compress(record.data, 6)
        elif record.method == 0:
            stored = record.data
        else:
            raise ValueError(f"unsupported write method {record.method}")
        stored_records.append(stored)
    data_offset = TRC_HEADER_SIZE + directory_slots * TRC_ENTRY_SIZE
    output = bytearray(data_offset)
    output[:4] = b"TRC\x1a"
    struct.pack_into("<III", output, 4, directory_slots, len(records), data_offset)
    relative = 0
    for index, (record, stored) in enumerate(zip(records, stored_records)):
        base = TRC_HEADER_SIZE + index * TRC_ENTRY_SIZE
        name = record.name.encode("latin1")[:12]
        output[base:base + len(name)] = name
        struct.pack_into("<IIIHHI", output, base + 0x0C, relative,
                         len(record.data), len(stored),
                         sum(record.data) & 0xFFFF, record.method,
                         record.reserved)
        relative += len(stored)
    for stored in stored_records:
        output.extend(stored)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)


def pool_reference_to_slot(value: int) -> int:
    if value and value % SCENARIO_OBJECT_STRIDE == 0:
        return value // SCENARIO_OBJECT_STRIDE
    return value


def reference_list(data: bytes, count_offset: int, base_offset: int,
                   item_size: int = 4, limit: int = 64) -> list[int]:
    count = min(u32(data, count_offset), limit)
    if item_size == 1:
        return [u8(data, base_offset + i) for i in range(count)]
    return [u32(data, base_offset + i * item_size) for i in range(count)]


def unit_record(record: TrcRecord) -> dict[str, Any]:
    data = record.data
    skill_mask = u32(data, 0x1F0)
    return {
        "unit_id": record.index,
        "name": embedded_name(data, record.name),
        "archive_name": record.name,
        "lifecycle_class": u32(data, 0x14C),
        "placement_class": u32(data, 0x150),
        "max_health": u32(data, 0x154),
        "offense": u32(data, 0x158),
        "defense": u32(data, 0x15C),
        "max_secondary": u32(data, 0x160),
        "movement_or_render_class": u32(data, 0x17C),
        "movement_step_limit": u32(data, 0x7CC),
        "movement_period": u32(data, 0x7D4),
        "movement_frame_delta_nonzero": any(
            u32(data, 0x3CC + index * 4) != 0 for index in range(256)),
        "overlay_or_projectile_class": u32(data, 0x180),
        "population_cost": u32(data, 0x184),
        "transport_value": u32(data, 0x188),
        "construction_ticks": u32(data, 0x18C),
        "resource_cost": u32(data, 0x190),
        "secondary_cost": u32(data, 0x194),
        "effect_distance_gate": u32(data, 0x198),
        "support_or_acquisition_range": u32(data, 0x19C),
        "attack_profile": u32(data, 0x1A0),
        "attack_profile_vs_class3": u32(data, 0x1A4),
        "attack_range": u32(data, 0x1B0),
        "attack_range_vs_class3": u32(data, 0x1B4),
        "passive_recovery_enabled": u32(data, 0x1C8),
        "passive_recovery_flags": u32(data, 0x1CC),
        "linked_release_type": u32(data, 0x1D4),
        "alternate_morph_type": u32(data, 0x1E4),
        "initial_command_or_type_flags": u32(data, 0x1EC),
        "skill_mask": skill_mask,
        "skill_action_ids": [bit for bit in range(32) if skill_mask & (1 << bit)],
        "support_target_or_action_effect_flags": u32(data, 0x1F4),
        "support_source_flags": u32(data, 0x1F8),
        "prerequisite_types": reference_list(data, 0x1FC, 0x200),
        "primary_references": reference_list(data, 0x240, 0x244),
        "alternate_references": reference_list(data, 0x284, 0x288),
        "completion_references": reference_list(data, 0x2C8, 0x2CC),
        "small_references": reference_list(data, 0x30F, 0x310, 1),
        "footprint_width": u32(data, 0x330),
        "footprint_height": u32(data, 0x334),
        "center_bounds_left": i32(data, 0x360),
        "center_bounds_top": i32(data, 0x364),
        "center_bounds_width": i32(data, 0x368),
        "center_bounds_height": i32(data, 0x36C),
        "interaction_bounds_left": i32(data, 0x370),
        "interaction_bounds_top": i32(data, 0x374),
        "interaction_bounds_width": i32(data, 0x378),
        "interaction_bounds_height": i32(data, 0x37C),
    }


def action_record(record: TrcRecord) -> dict[str, Any]:
    data = record.data
    return {
        "action_id": record.index,
        "name": embedded_name(data, record.name, 0x108),
        "archive_name": record.name,
        "record_size": record.original_size,
        "direction_or_mode": u32(data, 0x150),
        "target_render_class_mask": u32(data, 0x154),
        "icon_marker": u32(data, 0x158),
        "owner_requirement": u32(data, 0x15C),
        "active_limit": u32(data, 0x160),
        "projectile_impact_percent": [u32(data, x) for x in (0x184, 0x188, 0x18C)],
        "source_health_cost_or_resource_limit": i32(data, 0x1E0),
        "secondary_cost_or_queued_limit": i32(data, 0x1E4),
        "target_health_delta": i32(data, 0x1E8),
        "status_or_secondary_value": i32(data, 0x1EC),
        "create_unit_type_or_period": u32(data, 0x1F0),
        "source_stat20_delta_or_threshold": i32(data, 0x1F4),
        "radius": u32(data, 0x1F8),
        "path_control": u32(data, 0x1FC),
        "startup_ticks": u32(data, 0x220),
        "projectile_loop_ticks": u32(data, 0x224),
        "owner_relation_mode": u32(data, 0xAC0),
    }


def attack_record(record: TrcRecord) -> dict[str, Any]:
    data = record.data
    return {
        "attack_id": record.index,
        "name": embedded_name(data, record.name, 0x108),
        "archive_name": record.name,
        "record_size": record.original_size,
        "terrain_gate": u32(data, 0x14C),
        "target_render_class_mask": u32(data, 0x154),
        "damage_by_render_class": [i32(data, x) for x in (0x170, 0x174, 0x178, 0x17C, 0x180)],
        "projectile_impact_by_class": [u32(data, x) for x in (0x184, 0x188, 0x18C)],
        "damage_amount": i32(data, 0x1E8),
        "path_kind": u32(data, 0x1FC),
        "direction_mode": u32(data, 0x378),
        "secondary_cost": i32(data, 0x40C),
        "owner_relation_mode": u32(data, 0xAC0),
    }


def equipment_records(record: TrcRecord) -> list[dict[str, Any]]:
    data = record.data
    if len(data) < 8:
        raise ValueError("JW2_10 equipment record has no catalog header")
    version = u32(data, 0)
    count = u32(data, 4)
    stride = 0x28C
    if version != 0x65 or count >= 0x97 or len(data) < 8 + count * stride:
        raise ValueError("JW2_10 equipment catalog header is invalid")
    result = []
    for effect_id in range(count):
        base = 8 + effect_id * stride
        filter_count = min(u32(data, base + 0x104), 0x80)
        result.append({
            "effect_id": effect_id,
            "name": embedded_name(data[base:base + stride],
                                  f"equipment_{effect_id:03d}"),
            "category": u32(data, base + 0x84),
            "mode": u32(data, base + 0x208),
            "type_filter_mode": u32(data, base + 0x100),
            "type_filter_type_ids": [
                u8(data, base + 0x108 + index)
                for index in range(filter_count)
            ],
            "pickup_filter_mode": u32(data, base + 0xA8),
            "replacement_type_id": u32(data, base + 0x26C),
            "max_health_delta": i32(data, base + 0x210),
            "max_secondary_delta": i32(data, base + 0x214),
            "health_delta": i32(data, base + 0x218),
            "secondary_delta": i32(data, base + 0x21C),
            "offense_delta": i32(data, base + 0x220),
            "defense_delta": i32(data, base + 0x224),
            "stat28_delta": i32(data, base + 0x228),
            "experience_delta": i32(data, base + 0x24C),
            "level_delta": i32(data, base + 0x250),
            "owner_resource_delta": i32(data, base + 0x254),
            "command_value_delta": i32(data, base + 0x258),
            "generic_modifiers": [
                i32(data, base + offset)
                for offset in (0x22C, 0x230, 0x234, 0x238,
                               0x240, 0x244, 0x248)
            ],
            "attachment_definition_id": u32(data, base + 0x260),
        })
    return result


def production_rule(data: bytes, offset: int) -> dict[str, int]:
    return {
        "base": i32(data, offset),
        "mode": u32(data, offset + 4),
        "linear": i32(data, offset + 8),
        "extra": i32(data, offset + 0x0C),
    }


def production_order_records(record: TrcRecord) -> list[dict[str, Any]]:
    data = record.data
    version = u32(data, 0)
    count = u32(data, 4)
    stride = 0x44C
    if version != 0x64 or count > 0x40 or len(data) < 8 + count * stride:
        raise ValueError("JW2_10 production-order catalog header is invalid")
    effect_offsets = [0x274 + index * 0x18 for index in range(18)]
    result = []
    for order_id in range(count):
        base = 8 + order_id * stride
        affected_count = min(u32(data, base + 0x104), 0x20)
        prerequisite_count = min(u32(data, base + 0x188), 0x20)
        result.append({
            "order_id": order_id,
            "name": decode_text(data[base:base + 0x80]) or
                    f"production_order_{order_id:02d}",
            "max_variant_count": u32(data, base + 0x210),
            "icon_marker": u8(data, base + 0x20C),
            "duration": production_rule(data, base + 0x214),
            "primary_cost": production_rule(data, base + 0x22C),
            "auxiliary_cost": production_rule(data, base + 0x244),
            "secondary_cost": production_rule(data, base + 0x25C),
            "affected_type_ids": [
                u32(data, base + 0x108 + index * 4)
                for index in range(affected_count)
            ],
            "prerequisite_type_ids": [
                u32(data, base + 0x18C + index * 4)
                for index in range(prerequisite_count)
            ],
            "completion_effects": [
                production_rule(data, base + offset)
                for offset in effect_offsets
            ],
        })
    return result


def equipment_allows_unit(effect: dict[str, Any], unit: dict[str, Any]) -> bool:
    mode = effect["type_filter_mode"]
    listed = unit["unit_id"] in effect["type_filter_type_ids"]
    if mode == 0:
        return True
    if mode == 1:
        return listed
    if mode == 2:
        return not listed
    if mode == 3:
        return unit["lifecycle_class"] == 0
    if mode == 4:
        return unit["lifecycle_class"] == 2
    return False


def scenario_chain(header: bytes, objects: bytes, head_offset: int,
                   unit_names: dict[int, str]) -> list[dict[str, Any]]:
    count = len(objects) // SCENARIO_OBJECT_STRIDE
    slot = pool_reference_to_slot(u32(header, head_offset))
    seen: set[int] = set()
    result: list[dict[str, Any]] = []
    while slot and slot < count and slot not in seen:
        seen.add(slot)
        base = slot * SCENARIO_OBJECT_STRIDE
        unit_id = u32(objects, base)
        result.append({
            "slot": slot,
            "pool_offset": slot * SCENARIO_OBJECT_STRIDE,
            "unit_id": unit_id,
            "unit_name": unit_names.get(unit_id, f"unit_{unit_id}"),
            "owner_id": u32(objects, base + 4),
            "health": u32(objects, base + 0x18),
            "flags": u32(objects, base + 0xA0),
            "x": i32(objects, base + 0xB8),
            "y": i32(objects, base + 0xBC),
        })
        slot = pool_reference_to_slot(u32(objects, base + 0x1CC))
    return result


SKILL_MAP_PATTERN = re.compile(r"\bA(?P<action>\d{2}) U(?P<unit>\d{3})\.trk$", re.IGNORECASE)


def map_record(path: Path, unit_names: dict[int, str]) -> dict[str, Any]:
    archive = TrcArchive(path)
    header = archive.record(0).data
    objects = archive.record(SCENARIO_OBJECT_RECORD).data
    match = SKILL_MAP_PATTERN.search(path.name)
    result: dict[str, Any] = {
        "name": path.name,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "active_units": scenario_chain(
            header, objects, SCENARIO_ACTIVE_HEAD_OFFSET, unit_names),
        "lifecycle_units": scenario_chain(
            header, objects, SCENARIO_LIFECYCLE_HEAD_OFFSET, unit_names),
    }
    if match:
        result["declared_action_id"] = int(match.group("action"))
        result["declared_unit_id"] = int(match.group("unit"))
        result["declared_source_present"] = any(
            row["unit_id"] == result["declared_unit_id"]
            for row in result["active_units"])
        result["declared_source_player_operable"] = any(
            row["unit_id"] == result["declared_unit_id"] and
            row["owner_id"] < 8
            for row in result["active_units"])
    return result


def replay_map_signature(data: bytes) -> bytes:
    if len(data) < REPLAY_HEADER_SIZE:
        return b""
    # 0x3e0..0x3fb contains replay time/player overlay rather than map bytes.
    return data[0x63:0x3E0] + data[0x3FC:REPLAY_HEADER_SIZE]


def archive_content_signature(archive: TrcArchive,
                              record_count: int | None = None) -> str:
    records = archive.records if record_count is None else archive.records[:record_count]
    digest = hashlib.sha256()
    for record in records:
        digest.update(record.name.encode("latin1"))
        digest.update(struct.pack("<I", len(record.data)))
        digest.update(record.data)
    return digest.hexdigest()


def replay_record(path: Path, map_signatures: dict[str, list[str]]) -> dict[str, Any]:
    archive = TrcArchive(path)
    replay = next((record for record in archive.records
                   if record.name.casefold() == "replay"), None)
    if replay is None:
        raise ValueError(f"replay archive has no Replay record: {path}")
    data = replay.data
    packets = data[REPLAY_HEADER_SIZE:]
    packet_count = len(packets) // REPLAY_PACKET_SIZE
    trailing = len(packets) % REPLAY_PACKET_SIZE
    subtypes: Counter[int] = Counter()
    first_frame = None
    last_frame = None
    for index in range(packet_count):
        base = index * REPLAY_PACKET_SIZE
        frame = u32(packets, base + 4)
        subtype = u8(packets, base + 0x0F)
        subtypes[subtype] += 1
        first_frame = frame if first_frame is None else min(first_frame, frame)
        last_frame = frame if last_frame is None else max(last_frame, frame)
    return {
        "name": path.name,
        "sha256": hashlib.sha256(data).hexdigest(),
        "matched_maps": map_signatures.get(
            archive_content_signature(archive, len(archive.records) - 1), []),
        "packet_count": packet_count,
        "trailing_bytes": trailing,
        "first_packet_frame": first_frame,
        "last_packet_frame": last_frame,
        "packet_subtypes": {f"0x{key:02x}": value for key, value in sorted(subtypes.items())},
    }


MECHANIC_FAMILIES = [
    {"id": "selection_and_mixed_selection", "player_operable": True},
    {"id": "move_path_and_queued_orders", "player_operable": True},
    {"id": "standard_attack_and_retarget", "player_operable": True},
    {"id": "class3_alternate_attack", "player_operable": True},
    {"id": "special_action_cast", "player_operable": True},
    {"id": "production_and_construction", "player_operable": True},
    {"id": "morph_and_variant_upgrade", "player_operable": True},
    {"id": "transport_load_unload", "player_operable": True},
    {"id": "support_recovery_and_status", "player_operable": True},
    {"id": "equipment_and_item_state", "player_operable": True},
    {"id": "resource_gather_and_dropoff", "player_operable": True},
    {"id": "death_decay_rebirth_and_spawn", "player_operable": True},
    {"id": "unit_and_map_effect_pool", "player_operable": False},
    {"id": "rng_area_scan_and_owner_relation", "player_operable": False},
]

PLAYER_ACTION_SELECTOR_NAMES = {
    0x00: "guard_idle",
    0x01: "primary_target_order",
    0x03: "definition_gated_group_order",
    0x04: "move",
    0x05: "attack_or_move_fallback",
    0x06: "worker_building_placement",
    0x07: "point_harvest",
    0x09: "patrol",
    0x0A: "paired_source_target",
    0x0B: "linked_release_group",
    0x0D: "area_status_toggle",
    0x0E: "production_or_definition_action",
    0x10: "unit_production",
    0x11: "morph_enter",
    0x12: "secondary_command_0",
    0x13: "secondary_command_1",
    0x14: "secondary_command_2",
    0x15: "secondary_command_3",
}


def relative_names(paths: Iterable[Path]) -> list[str]:
    return [path.name for path in sorted(paths, key=lambda item: item.name.casefold())]


def build_inventory(root: Path) -> dict[str, Any]:
    game = root / "RankerOCPV_Win"
    maps_dir = game / "Maps"
    replays_dir = game / "Replays"

    unit_archive = TrcArchive(game / "Jw2_09.trc")
    action_archive = TrcArchive(game / "Jw2_11.trc")
    attack_archive = TrcArchive(game / "Jw2_12.trc")
    equipment_archive = TrcArchive(game / "Jw2_10.trc")
    if len(unit_archive.records) < UNIT_CATALOG_COUNT:
        raise ValueError("Jw2_09.trc does not contain the 170 runtime unit rows")
    if len(action_archive.records) < ACTION_CATALOG_COUNT:
        raise ValueError("Jw2_11.trc does not contain the 46 runtime action rows")
    if len(attack_archive.records) < ATTACK_CATALOG_COUNT:
        raise ValueError("Jw2_12.trc does not contain the 61 runtime attack rows")

    units = [unit_record(row) for row in unit_archive.records[:UNIT_CATALOG_COUNT]]
    actions = [action_record(row) for row in action_archive.records[:ACTION_CATALOG_COUNT]]
    attacks = [attack_record(row) for row in attack_archive.records[:ATTACK_CATALOG_COUNT]]
    equipment = equipment_records(equipment_archive.record(2))
    production_orders = production_order_records(equipment_archive.record(0))
    unit_names = {row["unit_id"]: row["name"] for row in units}
    action_names = {row["action_id"]: row["name"] for row in actions}
    attack_names = {row["attack_id"]: row["name"] for row in attacks}
    player_equipment_effects = []
    for effect in equipment:
        allowed = [unit["unit_id"] for unit in units
                   if unit["unit_id"] < 0x60 and
                   equipment_allows_unit(effect, unit)]
        if effect["effect_id"] != 0 and effect["category"] <= 2 and allowed:
            player_equipment_effects.append({
                **effect,
                "allowed_mobile_unit_ids": allowed,
            })

    skill_bindings = []
    attack_bindings = []
    production_order_bindings = []
    action_capability_bindings = []
    construction_bindings = []
    unit_production_bindings = []
    morph_cycle_bindings = []
    for unit in units:
        for action_selector in range(32):
            if (unit["initial_command_or_type_flags"] &
                    (1 << action_selector)) == 0:
                continue
            action_capability_bindings.append({
                "unit_id": unit["unit_id"],
                "unit_name": unit["name"],
                "action_selector": action_selector,
                "selector_name": PLAYER_ACTION_SELECTOR_NAMES.get(
                    action_selector, f"selector_{action_selector:02x}"),
            })
        for building_id in unit["primary_references"]:
            construction_bindings.append({
                "builder_unit_id": unit["unit_id"],
                "builder_unit_name": unit["name"],
                "building_unit_id": building_id,
                "building_unit_name": unit_names.get(
                    building_id, f"unit_{building_id}"),
            })
        for produced_id in unit["alternate_references"]:
            unit_production_bindings.append({
                "producer_unit_id": unit["unit_id"],
                "producer_unit_name": unit["name"],
                "produced_unit_id": produced_id,
                "produced_unit_name": unit_names.get(
                    produced_id, f"unit_{produced_id}"),
            })
        if (unit["alternate_morph_type"] != 0 and
                (unit["initial_command_or_type_flags"] & (1 << 0x11)) != 0):
            morph_cycle_bindings.append({
                "source_unit_id": unit["unit_id"],
                "source_unit_name": unit["name"],
                "morph_unit_id": unit["alternate_morph_type"],
                "morph_unit_name": unit_names.get(
                    unit["alternate_morph_type"],
                    f"unit_{unit['alternate_morph_type']}"),
            })
        for action_id in unit["skill_action_ids"]:
            skill_bindings.append({
                "unit_id": unit["unit_id"],
                "unit_name": unit["name"],
                "action_id": action_id,
                "action_name": action_names.get(action_id, f"action_{action_id}"),
            })
        for variant, profile_key, range_key in (
            ("primary", "attack_profile", "attack_range"),
            ("vs_class3", "attack_profile_vs_class3", "attack_range_vs_class3"),
        ):
            attack_id = unit[profile_key]
            # Attack row zero is a real zero-damage BuildMan profile.  It is
            # player-reachable only through a primary explicit-attack
            # capability; alternate zero remains the catalog's absent value.
            player_primary_zero = (
                variant == "primary" and
                (unit["initial_command_or_type_flags"] & (1 << 5)) != 0)
            if attack_id or player_primary_zero:
                attack_bindings.append({
                    "unit_id": unit["unit_id"],
                    "unit_name": unit["name"],
                    "variant": variant,
                    "attack_id": attack_id,
                    "attack_name": attack_names.get(attack_id, f"attack_{attack_id}"),
                    "range": unit[range_key],
                })
        if unit["unit_id"] >= 0x60:
            for order_id in unit["completion_references"]:
                if order_id < len(production_orders):
                    production_order_bindings.append({
                        "unit_id": unit["unit_id"],
                        "unit_name": unit["name"],
                        "order_id": order_id,
                        "order_name": production_orders[order_id]["name"],
                    })

    map_paths = list(maps_dir.glob("*.trk"))
    maps = [map_record(path, unit_names) for path in sorted(map_paths)]
    raw_map_signatures: dict[str, list[str]] = defaultdict(list)
    for path in map_paths:
        archive = TrcArchive(path)
        raw_map_signatures[archive_content_signature(archive)].append(path.name)
    replays = [replay_record(path, raw_map_signatures)
               for path in sorted(replays_dir.glob("*.ply"))]
    replayed_map_names = {
        name for replay in replays for name in replay["matched_maps"]
    }

    skill_fixtures: dict[tuple[int, int], list[str]] = defaultdict(list)
    invalid_skill_maps = []
    for item in maps:
        if "declared_action_id" not in item:
            continue
        if (not item["declared_source_present"] or
                not item["declared_source_player_operable"]):
            invalid_skill_maps.append(item["name"])
            continue
        key = (item["declared_unit_id"], item["declared_action_id"])
        skill_fixtures[key].append(item["name"])

    result_path = (root / "ranker_reconstructed_code" / "tools" /
                   "gameplay_parity" / "parity_results.json")
    recorded_results = (json.loads(result_path.read_text(encoding="utf-8"))
                        if result_path.exists() else {"cases": []})
    rebuild_path = root / "RankerOCPV_Win" / "ranker_rebuild.exe"
    current_rebuild_sha256 = (
        hashlib.sha256(rebuild_path.read_bytes()).hexdigest().upper()
        if rebuild_path.exists() else None)
    proven_skill_keys = {
        (case["unit_id"], case["action_id"])
        for case in recorded_results.get("cases", [])
        if case.get("kind") == "skill" and case.get("result") == "exact" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    }
    proven_attack_keys = {
        (case["unit_id"], case["variant"], case["attack_id"])
        for case in recorded_results.get("cases", [])
        if case.get("kind") == "attack" and case.get("result") == "exact" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    }
    unreachable_attack_keys = {
        (case["unit_id"], case["variant"], case["attack_id"])
        for case in recorded_results.get("cases", [])
        if case.get("kind") == "attack" and
        case.get("result") == "not_player_reachable"
    }
    current_attack_class_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "attack_target_class" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_equipment_apply_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "equipment_apply" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_equipment_toggle_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "equipment_toggle" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_production_order_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "production_order" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_production_order_cancel_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "production_order_cancel" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_unit_production_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "unit_production" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_construction_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "construction" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]
    current_morph_cycle_cases = [
        case for case in recorded_results.get("cases", [])
        if case.get("kind") == "morph_cycle" and
        case.get("rebuild_sha256") == current_rebuild_sha256
    ]

    skill_coverage = []
    for binding in skill_bindings:
        key = (binding["unit_id"], binding["action_id"])
        fixtures = sorted(skill_fixtures.get(key, []))
        matching_replays = sorted(name for name in fixtures if name in replayed_map_names)
        status = (
            "parity_proven" if key in proven_skill_keys else
            "replay_only" if matching_replays else
            "fixture_only" if fixtures else
            "missing_fixture")
        skill_coverage.append({
            **binding,
            "status": status,
            "fixtures": fixtures,
            "matching_replay_maps": matching_replays,
        })

    attack_coverage = []
    for binding in attack_bindings:
        key = (binding["unit_id"], binding["variant"], binding["attack_id"])
        attack_coverage.append({
            **binding,
            "status": ("parity_proven" if key in proven_attack_keys else
                       "catalog_unreachable" if key in unreachable_attack_keys else
                       "missing_unit_profile_execution"),
        })
    bound_skill_keys = {(row["unit_id"], row["action_id"])
                        for row in skill_bindings}
    unbound_declared_skill_maps = sorted(
        name
        for key in set(skill_fixtures) - bound_skill_keys
        for name in skill_fixtures[key])
    skill_status = Counter(row["status"] for row in skill_coverage)
    attack_status = Counter(row["status"] for row in attack_coverage)

    return {
        "schema": 1,
        "source": {
            "unit_catalog": "RankerOCPV_Win/Jw2_09.trc",
            "action_catalog": "RankerOCPV_Win/Jw2_11.trc",
            "attack_catalog": "RankerOCPV_Win/Jw2_12.trc",
            "map_directory": "RankerOCPV_Win/Maps",
            "replay_directory": "RankerOCPV_Win/Replays",
        },
        "definitions": {
            "coverage_status": {
                "parity_proven": "an exact aligned original/rebuild result is recorded in parity_results.json",
                "replay_only": "fixture has a matching replay but no exact comparison result is recorded",
                "fixture_only": "diagnostic map exists but no matching replay is stored",
                "missing_fixture": "no unit/action-specific diagnostic map exists",
                "missing_unit_profile_execution": "no per-unit attack-profile execution evidence is stored",
                "catalog_unreachable": "the catalog field cannot be selected by any target class allowed by the unit's primary-profile mask",
            },
        },
        "summary": {
            "units": len(units),
            "actions": len(actions),
            "attacks": len(attacks),
            "equipment_effects": len(equipment),
            "player_equipment_effects": len(player_equipment_effects),
            "production_orders": len(production_orders),
            "player_production_order_bindings": len(production_order_bindings),
            "player_production_orders": len({
                row["order_id"] for row in production_order_bindings}),
            "unit_skill_bindings": len(skill_bindings),
            "unit_attack_bindings": len(attack_bindings),
            "attack_source_types": len(
                {row["unit_id"] for row in attack_bindings} |
                {row["unit_id"] for row in units
                 if row["initial_command_or_type_flags"] & (1 << 5)}),
            "player_action_capability_bindings": len(
                action_capability_bindings),
            "player_action_selectors": len({
                row["action_selector"] for row in action_capability_bindings}),
            "construction_bindings": len(construction_bindings),
            "unit_production_bindings": len(unit_production_bindings),
            "morph_cycle_bindings": len(morph_cycle_bindings),
            "maps": len(maps),
            "replays": len(replays),
            "skill_coverage": dict(sorted(skill_status.items())),
            "attack_coverage": dict(sorted(attack_status.items())),
            "attack_target_class_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_attack_class_cases).items())),
            "equipment_apply_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_equipment_apply_cases).items())),
            "equipment_toggle_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_equipment_toggle_cases).items())),
            "production_order_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_production_order_cases).items())),
            "production_order_cancel_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_production_order_cancel_cases).items())),
            "unit_production_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_unit_production_cases).items())),
            "construction_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_construction_cases).items())),
            "morph_cycle_cases": dict(sorted(Counter(
                case.get("result", "missing")
                for case in current_morph_cycle_cases).items())),
            "current_rebuild_sha256": current_rebuild_sha256,
            "invalid_declared_skill_maps": relative_names(
                Path(name) for name in invalid_skill_maps),
            "unbound_declared_skill_maps": unbound_declared_skill_maps,
        },
        "mechanic_families": MECHANIC_FAMILIES,
        "units": units,
        "actions": actions,
        "attacks": attacks,
        "equipment_effects": equipment,
        "player_equipment_effects": player_equipment_effects,
        "production_orders": production_orders,
        "production_order_bindings": production_order_bindings,
        "action_capability_bindings": action_capability_bindings,
        "construction_bindings": construction_bindings,
        "unit_production_bindings": unit_production_bindings,
        "morph_cycle_bindings": morph_cycle_bindings,
        "skill_bindings": skill_bindings,
        "attack_bindings": attack_bindings,
        "skill_coverage": skill_coverage,
        "attack_coverage": attack_coverage,
        "maps": maps,
        "replays": replays,
        "recorded_results": recorded_results,
    }


def markdown_report(inventory: dict[str, Any]) -> str:
    summary = inventory["summary"]
    skill_counts = summary["skill_coverage"]
    attack_counts = summary["attack_coverage"]
    missing_skills = [row for row in inventory["skill_coverage"]
                      if row["status"] != "parity_proven"]
    lines = [
        "# Gameplay parity coverage ledger",
        "",
        "This report is generated from the shipped TRC catalogs and current",
        "diagnostic artifacts. Prepared fixtures are not counted as executed",
        "original/rebuild simulation comparisons.",
        "",
        "## Inventory",
        "",
        f"- Unit definitions: {summary['units']}",
        f"- Special-action definitions: {summary['actions']}",
        f"- Attack/effect definitions: {summary['attacks']}",
        f"- Unit/action bindings: {summary['unit_skill_bindings']}",
        f"- Unit/attack-profile bindings: {summary['unit_attack_bindings']}",
        f"- Player action-capability bindings: {summary['player_action_capability_bindings']}",
        f"- Player action selectors represented: {summary['player_action_selectors']}",
        f"- Worker/building bindings: {summary['construction_bindings']}",
        f"- Producer/unit bindings: {summary['unit_production_bindings']}",
        f"- Player morph-cycle bindings: {summary['morph_cycle_bindings']}",
        f"- Diagnostic maps: {summary['maps']}",
        f"- Stored replays: {summary['replays']}",
        "",
        "## Current executable evidence",
        "",
        f"- Skills with proven original/rebuild parity: {skill_counts.get('parity_proven', 0)}",
        f"- Skills with replay but no proven comparison: {skill_counts.get('replay_only', 0)}",
        f"- Skills with fixture only: {skill_counts.get('fixture_only', 0)}",
        f"- Skills missing a fixture: {skill_counts.get('missing_fixture', 0)}",
        f"- Unit attack bindings missing per-profile execution: {attack_counts.get('missing_unit_profile_execution', 0)}",
        f"- Unit attack bindings parity proven: {attack_counts.get('parity_proven', 0)}",
        f"- Catalog-unreachable attack fields: {attack_counts.get('catalog_unreachable', 0)}",
        f"- Unit/target-class parity cases: {summary['attack_target_class_cases'].get('exact', 0)} / {summary['attack_source_types'] * 5}",
        f"- Construction bindings parity proven: {summary['construction_cases'].get('exact', 0)} / {summary['construction_bindings']}",
        f"- Unit-production bindings parity proven: {summary['unit_production_cases'].get('exact', 0)} / {summary['unit_production_bindings']}",
        f"- Morph cycles parity proven: {summary['morph_cycle_cases'].get('exact', 0)} / {summary['morph_cycle_bindings']}",
        "",
        "A matching replay alone does not prove parity; the next audit stage must",
        "attach an aligned original/rebuild state comparison to every row.",
        "",
        "## Unit/action rows not yet proven",
        "",
        "| Unit | Action | Status | Fixture |",
        "|---|---|---|---|",
    ]
    for row in missing_skills:
        fixture = ", ".join(row["fixtures"]) if row["fixtures"] else "—"
        lines.append(
            f"| {row['unit_id']:03d} {row['unit_name']} | "
            f"{row['action_id']:02d} {row['action_name']} | "
            f"{row['status']} | {fixture} |")
    lines.extend([
        "",
        "## Player-operable mechanic families",
        "",
    ])
    for family in inventory["mechanic_families"]:
        kind = "direct" if family["player_operable"] else "simulation consequence"
        lines.append(f"- `{family['id']}` ({kind})")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    default_root = Path(__file__).resolve().parents[3]
    parser.add_argument("--root", type=Path, default=default_root)
    parser.add_argument("--output-dir", type=Path,
                        default=Path(__file__).resolve().parent / "reports")
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output_dir.resolve()
    inventory = build_inventory(root)
    output.mkdir(parents=True, exist_ok=True)
    json_path = output / "gameplay_inventory.json"
    md_path = output / "gameplay_inventory.md"
    json_path.write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    md_path.write_text(markdown_report(inventory), encoding="utf-8")
    summary = inventory["summary"]
    print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
    print(json_path)
    print(md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
