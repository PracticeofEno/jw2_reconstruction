#!/usr/bin/env python3
"""Generate local-only replays that exercise persistent equipment modifiers."""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


TOOL_DIR = Path(__file__).resolve().parent
ROOT = TOOL_DIR.parents[2]
PARITY_DIR = ROOT / "ranker_reconstructed_code" / "tools" / "gameplay_parity"
sys.path.insert(0, str(PARITY_DIR))

from generate_attack_batch_replays import (  # noqa: E402
    build_row_library,
    chain_slots,
    configure_row,
)
from generate_move_patrol_batch_replays import (  # noqa: E402
    ALTERNATE_TERRAIN_RECORD,
    MAP_BASE_VISIBILITY_RECORD,
    MAP_WIDTH_TILES,
    TERRAIN_RECORD,
    movement_cell_allowed,
)
from generate_skill_replay import make_packet, write_u32  # noqa: E402
from inventory_gameplay_matrix import (  # noqa: E402
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    replace_trc_record,
    u32,
    write_trc_archive,
)


COMMAND_FRAME = 31
INVALID_EQUIPMENT = 0
SOLDIER_TYPE = 3
TWIN_PTERAS_TYPE = 45
PASSIVE_TARGET_TYPE = 75


def clear_and_set_equipment(row: bytearray, effects: tuple[int, ...]) -> None:
    for index in range(6):
        write_u32(row, 0x30 + index * 4, INVALID_EQUIPMENT)
    for index, effect_id in enumerate(effects[:4]):
        write_u32(row, 0x30 + index * 4, effect_id)


def replay_header(template: bytes, map_name: str) -> bytearray:
    header = bytearray(template)
    header[0x5F] = 0
    header[0x87] = 5
    encoded = f"Maps\\{map_name}".encode("ascii")
    header[0x1FB:0x1FB + 260] = b"\0" * 260
    header[0x1FB:0x1FB + len(encoded)] = encoded
    return header


def write_fixture(base: TrcArchive, header: bytes, objects: bytes,
                  replay_template: bytes, stem: str,
                  packets: list[bytes], end_frame: int,
                  clear_visibility: bool = False) -> dict:
    game = ROOT / "RankerOCPV_Win"
    map_path = game / "Maps" / f"{stem}.trk"
    replay_path = game / "Replays" / f"{stem}.ply"
    records = list(base.records)
    records[0] = replace_trc_record(base.record(0), header)
    records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        base.record(SCENARIO_OBJECT_RECORD), objects)
    if clear_visibility:
        visibility = base.record(MAP_BASE_VISIBILITY_RECORD)
        records[MAP_BASE_VISIBILITY_RECORD] = replace_trc_record(
            visibility, bytes(len(visibility.data)))
    write_trc_archive(map_path, records, base.directory_slots)

    payload = bytes(replay_header(replay_template, map_path.name))
    payload += b"".join(packets)
    payload += make_packet(end_frame, len(packets), 0, 0x13)
    replay_records = append_trc_record(records, "Replay", payload, 2)
    write_trc_archive(replay_path, replay_records, base.directory_slots)
    return {
        "stem": stem,
        "map": map_path.relative_to(ROOT).as_posix(),
        "replay": replay_path.relative_to(ROOT).as_posix(),
        "end_frame": end_frame,
        "sha256": hashlib.sha256(replay_path.read_bytes()).hexdigest().upper(),
    }


def make_attack_fixture(base: TrcArchive, replay_template: bytes,
                        library: dict[int, bytes], units: dict[int, dict],
                        label: str, source_type: int,
                        source_effects: tuple[int, ...], distance: int,
                        end_frame: int,
                        target_effects: tuple[int, ...] = ()) -> dict:
    header = bytearray(base.record(0).data)
    objects = bytearray(base.record(SCENARIO_OBJECT_RECORD).data)
    slots = chain_slots(header, objects, SCENARIO_ACTIVE_HEAD_OFFSET)[:2]
    if len(slots) != 2:
        raise ValueError("attack fixture needs two active slots")
    source_slot, target_slot = slots
    source_x, source_y = 508, 992
    target_x, target_y = source_x + distance, source_y
    write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
              source_slot * SCENARIO_OBJECT_STRIDE)
    write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)

    source = bytearray(configure_row(
        library[source_type], units[source_type], 0, source_x, source_y,
        0, target_slot * SCENARIO_OBJECT_STRIDE))
    clear_and_set_equipment(source, source_effects)
    source_base = source_slot * SCENARIO_OBJECT_STRIDE
    objects[source_base:source_base + SCENARIO_OBJECT_STRIDE] = source

    target = bytearray(configure_row(
        library[PASSIVE_TARGET_TYPE], units[PASSIVE_TARGET_TYPE], 1,
        target_x, target_y, source_slot * SCENARIO_OBJECT_STRIDE, 0,
        durable_target=True))
    clear_and_set_equipment(target, target_effects)
    write_u32(target, 0x1C, 0)
    write_u32(target, 0x20, 0)
    target_base = target_slot * SCENARIO_OBJECT_STRIDE
    objects[target_base:target_base + SCENARIO_OBJECT_STRIDE] = target

    packet = make_packet(
        COMMAND_FRAME, 0, 0, 0x02, 5,
        source_slot * SCENARIO_OBJECT_STRIDE,
        target_slot * SCENARIO_OBJECT_STRIDE, target_x, target_y)
    result = write_fixture(
        base, bytes(header), bytes(objects), replay_template,
        f"(2) GP Equip Modifier {label}", [packet], end_frame)
    result.update({
        "kind": "attack",
        "source_slot": source_slot,
        "target_slot": target_slot,
        "source_type": source_type,
        "source_effects": list(source_effects),
        "target_effects": list(target_effects),
        "distance": distance,
    })
    return result


def long_horizontal_path(unit: dict, alternate: bytes, terrain: bytes,
                         width: int, height: int,
                         cells: int = 20) -> tuple[int, int, int, int]:
    for tile_y in range(8, height - 8):
        for tile_x in range(8, width - cells - 1):
            if all(movement_cell_allowed(
                    unit, alternate, terrain, tile_x + delta, tile_y,
                    width, height) for delta in range(cells + 1)):
                return (tile_x * 32 + 16, tile_y * 32 + 16,
                        (tile_x + cells) * 32 + 16, tile_y * 32 + 16)
    raise ValueError("no long horizontal movement path")


def make_move_fixture(base: TrcArchive, replay_template: bytes,
                      library: dict[int, bytes], units: dict[int, dict],
                      label: str, source_type: int,
                      effects: tuple[int, ...], end_frame: int) -> dict:
    header = bytearray(base.record(0).data)
    objects = bytearray(base.record(SCENARIO_OBJECT_RECORD).data)
    slots = chain_slots(header, objects, SCENARIO_ACTIVE_HEAD_OFFSET)[:1]
    if not slots:
        raise ValueError("move fixture needs an active slot")
    slot = slots[0]
    unit = units[source_type]
    width = u32(base.record(1).data, 0x164)
    height = u32(base.record(1).data, 0x168)
    path = long_horizontal_path(
        unit, base.record(ALTERNATE_TERRAIN_RECORD).data,
        base.record(TERRAIN_RECORD).data, width, height)
    source_x, source_y, destination_x, destination_y = path
    write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
              slot * SCENARIO_OBJECT_STRIDE)
    write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
    row = bytearray(configure_row(
        library[source_type], unit, 0, source_x, source_y, 0, 0))
    clear_and_set_equipment(row, effects)
    write_u32(row, 0x60, 1)
    write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
    base_offset = slot * SCENARIO_OBJECT_STRIDE
    objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
    packet = make_packet(
        COMMAND_FRAME, 0, 0, 0x02, 0x04,
        slot * SCENARIO_OBJECT_STRIDE, 0, destination_x, destination_y)
    result = write_fixture(
        base, bytes(header), bytes(objects), replay_template,
        f"(2) GP Equip Modifier {label}", [packet], end_frame,
        clear_visibility=True)
    result.update({
        "kind": "move",
        "source_slot": slot,
        "source_type": source_type,
        "source_effects": list(effects),
        "source_world": [source_x, source_y],
        "destination_world": [destination_x, destination_y],
    })
    return result


def make_command_flag_action_fixture(replay_template: bytes, label: str,
                                     target_effects: tuple[int, ...],
                                     end_frame: int = 850) -> dict:
    game = ROOT / "RankerOCPV_Win"
    source_slot = 38
    target_slot = 12
    action_id = 0x14
    template = TrcArchive(game / "Maps" / "(2) GP Skill A20 U029.trk")
    header = bytearray(template.record(0).data)
    objects = bytearray(template.record(SCENARIO_OBJECT_RECORD).data)
    source_base = source_slot * SCENARIO_OBJECT_STRIDE
    target_base = target_slot * SCENARIO_OBJECT_STRIDE
    target = bytearray(objects[
        target_base:target_base + SCENARIO_OBJECT_STRIDE])
    clear_and_set_equipment(target, target_effects)
    write_u32(target, 0x9C, 0)
    objects[target_base:target_base + SCENARIO_OBJECT_STRIDE] = target
    target_x = u32(objects, target_base + 0xB8)
    target_y = u32(objects, target_base + 0xBC)
    packet = make_packet(
        COMMAND_FRAME, 0, 0, 0x09, action_id,
        source_base, target_base, target_x, target_y)
    result = write_fixture(
        template, bytes(header), bytes(objects), replay_template,
        f"(2) GP Equip Modifier {label}", [packet], end_frame)
    result.update({
        "kind": "command_flag_action",
        "action_id": action_id,
        "source_slot": source_slot,
        "target_slot": target_slot,
        "source_type": u32(objects, source_base),
        "target_type": u32(objects, target_base),
        "target_effects": list(target_effects),
    })
    return result


def main() -> int:
    game = ROOT / "RankerOCPV_Win"
    report_path = (ROOT / "debug_artifacts" /
                   "gameplay_inventory_20260819" / "gameplay_inventory.json")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_template = next(
        record.data[:REPLAY_HEADER_SIZE] for record in replay_source.records
        if record.name.casefold() == "replay")

    fixtures = [
        make_attack_fixture(base, replay_template, library, units,
                            "Recovery Base", SOLDIER_TYPE, (), 96, 180),
        make_attack_fixture(base, replay_template, library, units,
                            "Recovery E64", SOLDIER_TYPE, (64,), 96, 180),
        make_attack_fixture(base, replay_template, library, units,
                            "Range Base", SOLDIER_TYPE, (), 260, 75),
        make_attack_fixture(base, replay_template, library, units,
                            "Range E68", SOLDIER_TYPE, (68,), 260, 75),
        make_attack_fixture(base, replay_template, library, units,
                            "Filter Base U45", TWIN_PTERAS_TYPE, (), 280, 100),
        make_attack_fixture(base, replay_template, library, units,
                            "Filter E73 U45", TWIN_PTERAS_TYPE, (73,), 280, 100),
        make_attack_fixture(base, replay_template, library, units,
                            "CommandFlag Target E88", SOLDIER_TYPE, (), 96, 100,
                            target_effects=(88,)),
        make_command_flag_action_fixture(
            replay_template, "CommandFlag Action20 Base", ()),
        make_command_flag_action_fixture(
            replay_template, "CommandFlag Action20 E88", (88,)),
        make_move_fixture(base, replay_template, library, units,
                          "Move Base", SOLDIER_TYPE, (), 65),
        make_move_fixture(base, replay_template, library, units,
                          "Move E64", SOLDIER_TYPE, (64,), 65),
        make_move_fixture(base, replay_template, library, units,
                          "Move Stack E64 E65 E66", SOLDIER_TYPE,
                          (64, 65, 66), 65),
    ]
    for fixture in fixtures:
        print(json.dumps(fixture, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
