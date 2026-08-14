#!/usr/bin/env python3
"""Generate accelerated fixtures for every player worker/building binding."""

from __future__ import annotations

import hashlib
import json
from collections import Counter
from pathlib import Path

from generate_attack_batch_replays import (
    build_row_library,
    chain_slots,
    configure_row,
)
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import (
    MAP_BASE_VISIBILITY_RECORD,
    MAP_FOOTPRINT_OCCUPIED,
    MAP_WIDTH_TILES,
    register_serialized_building_footprint,
)
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


BATCH_SIZE = 6
COMMAND_FRAME = 31
END_FRAME = 120
TERRAIN_RECORD = 12
ALTERNATE_TERRAIN_RECORD = 10
TERRAIN_BLOCK_MASK = 0x00000700
FOOTPRINT_SPECIAL_OCCUPIED = 0x40000000
PLACEMENT_VALID = 0x80000000


def footprint_tiles(unit: dict, tile_x: int, tile_y: int) -> set[tuple[int, int]]:
    width = max(1, unit["footprint_width"])
    height = max(1, unit["footprint_height"])
    return {
        (tile_x + dx, tile_y + dy)
        for dy in range(height)
        for dx in range(width)
    }


def placement_cells_valid(unit_type: int, alternate: bytes, terrain: bytes,
                          cells: set[tuple[int, int]],
                          map_width: int, map_height: int) -> bool:
    terrain_class = None
    for tile_x, tile_y in cells:
        if not (0 <= tile_x < map_width and 0 <= tile_y < map_height):
            return False
        index = tile_y * MAP_WIDTH_TILES + tile_x
        alternate_value = u32(alternate, index * 4)
        terrain_value = u32(terrain, index * 4)
        if ((alternate_value & PLACEMENT_VALID) == 0 or
                (terrain_value & TERRAIN_BLOCK_MASK) != 0 or
                (terrain_value & FOOTPRINT_SPECIAL_OCCUPIED) != 0):
            return False
        cell_class = (alternate_value & 0x1C000000) >> 26
        if terrain_class is None:
            terrain_class = cell_class
        elif cell_class != terrain_class:
            return False
        if unit_type in (0x60, 0x70, 0x80, 0x90):
            for nearby_y in range(tile_y - 4, tile_y + 5):
                for nearby_x in range(tile_x - 4, tile_x + 5):
                    if not (0 <= nearby_x < map_width and
                            0 <= nearby_y < map_height):
                        continue
                    nearby_index = nearby_y * MAP_WIDTH_TILES + nearby_x
                    if (u32(terrain, nearby_index * 4) &
                            TERRAIN_BLOCK_MASK) == 0x100:
                        return False
    return True


def choose_case_positions(bindings: list[dict], units: dict[int, dict],
                          alternate: bytes, terrain: bytes,
                          reserved: set[tuple[int, int]],
                          map_width: int,
                          map_height: int) -> list[tuple[int, int, int, int]]:
    occupied = set(reserved)
    positions: list[tuple[int, int, int, int]] = []
    for binding in bindings:
        building = units[binding["building_unit_id"]]
        selected = None
        for tile_y in range(8, map_height - 8):
            for tile_x in range(8, map_width - 8):
                cells = footprint_tiles(building, tile_x, tile_y)
                if cells & occupied or not placement_cells_valid(
                        building["unit_id"], alternate, terrain, cells,
                        map_width, map_height):
                    continue
                world_x = tile_x * 32
                world_y = tile_y * 32
                # State 0x23 approaches this interaction-bounds centre.  Put
                # the worker at the exact eventual approach point so the
                # fixture exercises construction immediately without waiting
                # hundreds of movement frames.  The original creation path
                # explicitly excludes its own builder from occupancy checks.
                worker_x = world_x + (building["interaction_bounds_width"] >> 1)
                worker_y = world_y + (building["interaction_bounds_height"] >> 1)
                worker_tile = (worker_x >> 5, worker_y >> 5)
                if worker_tile in occupied - cells:
                    continue
                selected = (world_x, world_y, worker_x, worker_y)
                occupied.update(cells)
                occupied.add(worker_tile)
                break
            if selected is not None:
                break
        if selected is None:
            raise ValueError(
                "no legal construction placement for "
                f"{binding['builder_unit_id']} -> {binding['building_unit_id']}")
        positions.append(selected)
    return positions


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    bindings = [
        {
            "builder_unit_id": builder["unit_id"],
            "building_unit_id": building_id,
        }
        for builder in report["units"]
        for building_id in builder["primary_references"]
    ]
    bindings.sort(key=lambda row: (
        row["builder_unit_id"], row["building_unit_id"]))

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
    manifest = {
        "schema": 1,
        "binding_count": len(bindings),
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(bindings), BATCH_SIZE)):
        batch = bindings[start:start + BATCH_SIZE]
        builder_types = {row["builder_unit_id"] for row in batch}
        prerequisite_ids = sorted({
            requirement
            for row in batch
            for requirement in units[row["building_unit_id"]]["prerequisite_types"]
            if requirement not in builder_types
        })
        active_slots = slots[:len(batch) + len(prerequisite_ids)]
        if len(active_slots) != len(batch) + len(prerequisite_ids):
            raise ValueError(f"batch {batch_index} lacks active template slots")

        prerequisite_positions: list[tuple[int, int]] = []
        reserved: set[tuple[int, int]] = set()
        for prerequisite_index, type_id in enumerate(prerequisite_ids):
            prerequisite = units[type_id]
            tile_x = 8 + (prerequisite_index % 4) * 18
            tile_y = map_height - 12 - (prerequisite_index // 4) * 8
            cells = footprint_tiles(prerequisite, tile_x, tile_y)
            if cells & reserved:
                raise ValueError(f"prerequisite layout overlap in batch {batch_index}")
            reserved.update(cells)
            prerequisite_positions.append((tile_x * 32, tile_y * 32))

        positions = choose_case_positions(
            batch, units, alternate, terrain, reserved, map_width, map_height)
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        visibility = bytearray(len(base.record(MAP_BASE_VISIBILITY_RECORD).data))
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  active_slots[0] * SCENARIO_OBJECT_STRIDE)
        packets = [make_packet(30, 0, 0, 0x30)]
        cases = []

        for local_index, (binding, position) in enumerate(zip(batch, positions)):
            builder_id = binding["builder_unit_id"]
            building_id = binding["building_unit_id"]
            builder = units[builder_id]
            building = units[building_id]
            slot = active_slots[local_index]
            previous_link = (0 if local_index == 0 else
                             active_slots[local_index - 1] * SCENARIO_OBJECT_STRIDE)
            next_link = (0 if local_index + 1 == len(active_slots) else
                         active_slots[local_index + 1] * SCENARIO_OBJECT_STRIDE)
            building_x, building_y, worker_x, worker_y = position
            template_id = builder_id if builder_id in library else 0
            row = bytearray(configure_row(
                library[template_id], builder, 0, worker_x, worker_y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            packets.append(make_packet(
                COMMAND_FRAME, len(packets), 0, 0x02,
                0x06, slot * SCENARIO_OBJECT_STRIDE,
                building_id - 0x60, building_x, building_y))
            cases.append({
                "case_id": f"construction_{builder_id:03d}_{building_id:03d}",
                "builder_slot": slot,
                "builder_unit_id": builder_id,
                "builder_unit_name": builder["name"],
                "building_unit_id": building_id,
                "building_unit_name": building["name"],
                "primary_cost": building["resource_cost"],
                "secondary_cost": building["secondary_cost"],
                "construction_ticks": building["construction_ticks"],
                "building_world": [building_x, building_y],
            })

        for prerequisite_index, type_id in enumerate(prerequisite_ids):
            chain_index = len(batch) + prerequisite_index
            slot = active_slots[chain_index]
            previous_link = active_slots[chain_index - 1] * SCENARIO_OBJECT_STRIDE
            next_link = (0 if chain_index + 1 == len(active_slots) else
                         active_slots[chain_index + 1] * SCENARIO_OBJECT_STRIDE)
            definition = units[type_id]
            x, y = prerequisite_positions[prerequisite_index]
            template_id = type_id if type_id in library else 96
            row = bytearray(configure_row(
                library[template_id], definition, 0, x, y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            register_serialized_building_footprint(
                visibility, definition, 0, x, y)

        initial_counts = Counter(prerequisite_ids)
        constructed_counts = Counter(
            row["building_unit_id"] for row in batch)
        expected_counts = {
            str(type_id): initial_counts[type_id] + count
            for type_id, count in constructed_counts.items()
        }

        stem = f"(2) GP Construction B{batch_index:02d}"
        map_path = game / "Maps" / f"{stem}.trk"
        replay_path = game / "Replays" / f"{stem}.ply"
        map_records = list(base.records)
        map_records[0] = replace_trc_record(base.record(0), bytes(header))
        map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
        map_records[MAP_BASE_VISIBILITY_RECORD] = replace_trc_record(
            base.record(MAP_BASE_VISIBILITY_RECORD), bytes(visibility))
        player_record = bytearray(base.record(3).data)
        for owner in range(8):
            write_u32(player_record, 0x144 + owner * 4, 100_000)
            write_u32(player_record, 0x194 + owner * 4, 100_000)
        map_records[3] = replace_trc_record(base.record(3), bytes(player_record))
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
            "prerequisite_unit_ids": prerequisite_ids,
            "initial_completed_type_counts": {
                str(key): value for key, value in sorted(initial_counts.items())
            },
            "expected_completed_type_counts": expected_counts,
        })
        print(f"batch={batch_index:02d} bindings={len(cases)}")

    manifest_path = tool_dir / "construction_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"bindings={len(bindings)} batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
