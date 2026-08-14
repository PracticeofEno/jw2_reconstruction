#!/usr/bin/env python3
"""Generate move and patrol fixtures for every capable unit definition."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import BATCH_SIZE, build_row_library, chain_slots, configure_row
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import MAP_BASE_VISIBILITY_RECORD, MAP_WIDTH_TILES
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    replace_trc_record,
    u32,
    write_trc_archive,
)


MOVE_FRAME = 31
PATROL_FRAME = 32
END_FRAME = 180
ALTERNATE_TERRAIN_RECORD = 10
TERRAIN_RECORD = 12


def movement_cell_allowed(unit: dict, alternate: bytes, terrain: bytes,
                          tile_x: int, tile_y: int,
                          map_width: int, map_height: int) -> bool:
    if not (0 <= tile_x < map_width and 0 <= tile_y < map_height):
        return False
    index = tile_y * MAP_WIDTH_TILES + tile_x
    alternate_value = u32(alternate, index * 4)
    terrain_value = u32(terrain, index * 4)
    movement_class = unit["movement_or_render_class"]
    if movement_class == 3:
        return True
    if movement_class == 4:
        # CheckUnitCanEnterTerrainCell calls the legacy helper with the
        # command shortcut enabled.  Class four then tests only its 0x40000000
        # decoration bit and deliberately ignores the terrain mask.
        return (alternate_value & 0x40000000) != 0
    if (alternate_value & 0x80000000) == 0 or (terrain_value & 0x700) != 0:
        return False
    if movement_class == 0:
        return (alternate_value & 0x20000000) != 0
    if movement_class == 2:
        return (alternate_value & 0x60000000) != 0
    return False


def choose_paths(batch: list[dict], alternate: bytes, terrain: bytes,
                 map_width: int, map_height: int) -> list[tuple[int, int, int, int]]:
    reserved: set[tuple[int, int]] = set()
    result = []
    for unit in batch:
        selected = None
        for tile_y in range(8, map_height - 8):
            for tile_x in range(8, map_width - 12):
                path = {(tile_x + dx, tile_y) for dx in range(4)}
                if path & reserved or not all(movement_cell_allowed(
                        unit, alternate, terrain, x, y, map_width, map_height)
                        for x, y in path):
                    continue
                selected = (tile_x * 32 + 16, tile_y * 32 + 16,
                            (tile_x + 3) * 32 + 16, tile_y * 32 + 16)
                reserved.update(path)
                break
            if selected is not None:
                break
        if selected is None:
            raise ValueError(f"no movement path for unit {unit['unit_id']}")
        result.append(selected)
    return result


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = [row for row in report["units"]
             if (row["initial_command_or_type_flags"] & (1 << 4)) != 0]
    patrol_ids = {row["unit_id"] for row in report["units"]
                  if (row["initial_command_or_type_flags"] & (1 << 9)) != 0}
    if {row["unit_id"] for row in units} != patrol_ids:
        raise ValueError("move and patrol capability catalogs differ")
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    map_width = u32(base.record(1).data, 0x164)
    map_height = u32(base.record(1).data, 0x168)
    alternate = base.record(ALTERNATE_TERRAIN_RECORD).data
    terrain = base.record(TERRAIN_RECORD).data
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    fallback_types = {0: 1, 2: 48, 3: 77, 4: 79}
    manifest = {
        "schema": 1,
        "binding_count": len(units),
        "move_frame": MOVE_FRAME,
        "patrol_frame": PATROL_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(units), BATCH_SIZE)):
        batch = units[start:start + BATCH_SIZE]
        selected_slots = slots[:len(batch)]
        paths = choose_paths(
            batch, alternate, terrain, map_width, map_height)
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        packets = []
        cases = []
        for index, (unit, slot, path) in enumerate(
                zip(batch, selected_slots, paths)):
            source_x, source_y, destination_x, destination_y = path
            previous_link = (0 if index == 0 else
                             selected_slots[index - 1] * SCENARIO_OBJECT_STRIDE)
            next_link = (0 if index + 1 == len(selected_slots) else
                         selected_slots[index + 1] * SCENARIO_OBJECT_STRIDE)
            template = library.get(unit["unit_id"], library[
                fallback_types[unit["movement_or_render_class"]]])
            row = bytearray(configure_row(
                template, unit, 0, source_x, source_y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            packets.append(make_packet(
                MOVE_FRAME, len(packets), 0, 0x02,
                0x04, slot * SCENARIO_OBJECT_STRIDE,
                0, destination_x, destination_y))
            cases.append({
                "case_id": f"move_patrol_{unit['unit_id']:03d}",
                "unit_slot": slot,
                "unit_id": unit["unit_id"],
                "unit_name": unit["name"],
                "movement_class": unit["movement_or_render_class"],
                "expected_travel": bool(
                    unit["movement_step_limit"] and
                    unit["movement_frame_delta_nonzero"]),
                "source_world": [source_x, source_y],
                "move_world": [destination_x, destination_y],
            })
        for index, (slot, path) in enumerate(zip(selected_slots, paths)):
            source_x, source_y, _, _ = path
            packets.append(make_packet(
                PATROL_FRAME, len(packets), 0, 0x02,
                0x80000009, slot * SCENARIO_OBJECT_STRIDE,
                0, source_x, source_y))

        stem = f"(2) GP Move Patrol B{batch_index:02d}"
        map_path = game / "Maps" / f"{stem}.trk"
        replay_path = game / "Replays" / f"{stem}.ply"
        map_records = list(base.records)
        map_records[0] = replace_trc_record(base.record(0), bytes(header))
        map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
        map_records[MAP_BASE_VISIBILITY_RECORD] = replace_trc_record(
            base.record(MAP_BASE_VISIBILITY_RECORD),
            bytes(len(base.record(MAP_BASE_VISIBILITY_RECORD).data)))
        write_trc_archive(map_path, map_records, base.directory_slots)
        replay_header = bytearray(replay_header_template)
        replay_header[0x5F] = 0
        replay_header[0x87] = 5
        map_name = f"Maps\\{map_path.name}".encode("ascii")
        replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
        replay_header[0x1FB:0x1FB + len(map_name)] = map_name
        payload = bytes(replay_header) + b"".join(packets)
        payload += make_packet(END_FRAME, len(packets), 0, 0x13)
        replay_records = append_trc_record(map_records, "Replay", payload, 2)
        write_trc_archive(replay_path, replay_records, base.directory_slots)
        manifest["batches"].append({
            "batch_index": batch_index,
            "map": map_path.relative_to(root).as_posix(),
            "replay": replay_path.relative_to(root).as_posix(),
            "replay_sha256": hashlib.sha256(
                replay_path.read_bytes()).hexdigest().upper(),
            "cases": cases,
        })
        print(f"batch={batch_index:02d} bindings={len(cases)}")

    manifest_path = tool_dir / "move_patrol_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"bindings={len(units)} batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
