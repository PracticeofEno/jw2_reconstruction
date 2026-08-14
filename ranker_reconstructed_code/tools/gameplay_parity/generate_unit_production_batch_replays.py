#!/usr/bin/env python3
"""Generate accelerated fixtures for every player UI unit-production binding."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import (
    BATCH_SIZE,
    SOURCE_POSITIONS,
    build_row_library,
    chain_slots,
    configure_row,
)
from generate_skill_replay import make_packet, write_u32
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    i32,
    replace_trc_record,
    u32,
    write_trc_archive,
)


COMMAND_FRAME = 31
END_FRAME = 110
MAP_WIDTH_TILES = 256
MAP_BASE_VISIBILITY_RECORD = 13
MAP_FOOTPRINT_OCCUPIED = 0x20000000
PREREQUISITE_POSITIONS = (
    (320, 1920),
    (800, 1920),
    (1280, 1920),
    (1760, 1920),
    (320, 2400),
    (800, 2400),
    (1280, 2400),
    (1760, 2400),
)
POPULATION_SUPPORT_TYPE_ID = 96
POPULATION_SUPPORT_POSITIONS = ((2240, 1920), (2400, 2400))


def footprint_cells(unit: dict, world_x: int, world_y: int) -> set[tuple[int, int]]:
    tile_x = world_x >> 5
    tile_y = world_y >> 5
    return {
        (tile_x + x, tile_y + y)
        for y in range(unit["footprint_height"])
        for x in range(unit["footprint_width"])
    }


def original_matching_spawn_tile(
        alternate: bytes, terrain: bytes, source_x: int, source_y: int,
        requested_x: int, requested_y: int,
        occupied: set[tuple[int, int]],
        movement_class: int, map_width: int,
        map_height: int) -> tuple[int, int] | None:
    source_tile_x = source_x >> 5
    source_tile_y = source_y >> 5
    source_index = source_tile_y * MAP_WIDTH_TILES + source_tile_x
    terrain_class = (u32(alternate, source_index * 4) & 0x1C000000) >> 26
    scan_left = requested_x >> 5
    scan_top = requested_y >> 5
    scan_size = 1
    for _radius in range(10):
        for row in range(scan_size):
            tile_y = scan_top + row
            for column in range(scan_size):
                tile_x = scan_left + column
                if not (0 <= tile_x < map_width and
                        0 <= tile_y < map_height):
                    continue
                if (tile_x, tile_y) in occupied:
                    continue
                index = tile_y * MAP_WIDTH_TILES + tile_x
                decoration = u32(alternate, index * 4)
                if (decoration & 0x80000000) == 0:
                    continue
                if ((decoration & 0x1C000000) >> 26) != terrain_class:
                    continue
                if (u32(terrain, index * 4) & 0x700) != 0:
                    continue
                # CheckUnitCanEnterTerrainCell's legacy jump table runs after
                # the matching-class test.  Freshly produced units have no
                # command shortcut flag, so retain the raw decoration gates.
                if movement_class == 0 and (decoration & 0x20000000) == 0:
                    continue
                if movement_class == 1:
                    continue
                if movement_class == 2 and (decoration & 0x60000000) == 0:
                    continue
                if movement_class == 4 and (decoration & 0x40000000) == 0:
                    continue
                if movement_class not in (0, 1, 2, 3, 4):
                    continue
                return tile_x, tile_y
        scan_left -= 1
        scan_top -= 1
        scan_size += 2
    return None


def choose_producer_positions(
        batch_bindings: list[dict], units: dict[int, dict],
        exit_offsets: dict[int, tuple[int, int]], alternate: bytes,
        terrain: bytes, initially_occupied: set[tuple[int, int]],
        existing_centers: set[tuple[int, int]], map_width: int,
        map_height: int) -> list[tuple[int, int]]:
    occupied = set(initially_occupied)
    centers = set(existing_centers)
    positions: list[tuple[int, int]] = []
    for binding in batch_bindings:
        producer = units[binding["producer_unit_id"]]
        produced = units[binding["produced_unit_id"]]
        offset_x, offset_y = exit_offsets[binding["producer_unit_id"]]
        selected = None
        # Leave a generous edge margin for large footprints and the original
        # radius-nine small-unit fallback scan.
        for tile_y in range(10, map_height - 10):
            for tile_x in range(10, map_width - 10):
                world_x = tile_x * 32
                world_y = tile_y * 32
                cells = footprint_cells(producer, world_x, world_y)
                if (any(cell_x >= map_width or cell_y >= map_height
                        for cell_x, cell_y in cells) or
                        cells & occupied or (tile_x, tile_y) in centers):
                    continue
                trial_occupied = occupied | cells
                trial_centers = centers | {(tile_x, tile_y)}
                spawn = original_matching_spawn_tile(
                    alternate, terrain, world_x, world_y,
                    world_x + offset_x, world_y + offset_y,
                    trial_occupied | trial_centers,
                    produced["movement_or_render_class"], map_width,
                    map_height)
                if spawn is None:
                    continue
                selected = (world_x, world_y, cells, spawn)
                break
            if selected is not None:
                break
        if selected is None:
            raise ValueError(
                "no legal production source/exit pair for "
                f"{binding['producer_unit_id']} -> "
                f"{binding['produced_unit_id']}")
        world_x, world_y, cells, spawn = selected
        positions.append((world_x, world_y))
        occupied.update(cells)
        occupied.add(spawn)
        centers.add((world_x >> 5, world_y >> 5))
        centers.add(spawn)
    return positions


def register_serialized_building_footprint(
        visibility: bytearray, unit: dict, owner: int, world_x: int,
        world_y: int) -> None:
    """Mirror the footprint that the original map editor stores in BGI.

    Serialized scenario units and the BGI occupancy layer are independent in
    the original archive.  Moving only an OBC row leaves the original using
    the old, empty BGI cells while the reconstruction legitimately rebuilds
    occupancy from the live unit.  Such a fixture tests malformed-map recovery
    instead of player production.  Keep both representations aligned.
    """
    width = unit["footprint_width"]
    height = unit["footprint_height"]
    if unit["lifecycle_class"] != 2 or width == 0 or height == 0:
        return
    tile_x = world_x >> 5
    tile_y = world_y >> 5
    for y in range(height):
        for x in range(width):
            index = (tile_y + y) * MAP_WIDTH_TILES + tile_x + x
            offset = index * 4
            value = u32(visibility, offset) | MAP_FOOTPRINT_OCCUPIED
            if x == 0 and y == 0:
                value = (value & 0x7FFFF000) | unit["unit_id"] | (owner << 8)
            write_u32(visibility, offset, value)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    bindings = [
        {"producer_unit_id": producer["unit_id"],
         "produced_unit_id": produced}
        for producer in report["units"]
        for produced in producer["alternate_references"]
    ]
    bindings.sort(key=lambda row: (
        row["producer_unit_id"], row["produced_unit_id"]))
    library = build_row_library(game / "Maps")
    unit_catalog = TrcArchive(game / "Jw2_09.trc")
    exit_offsets = {
        unit_id: (i32(unit_catalog.records[unit_id].data, 0x2410),
                  i32(unit_catalog.records[unit_id].data, 0x2414))
        for unit_id in range(len(units))
    }
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    map_width = u32(base.record(1).data, 0x164)
    map_height = u32(base.record(1).data, 0x168)
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
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
        batch_bindings = bindings[start:start + BATCH_SIZE]
        producer_types = {
            binding["producer_unit_id"] for binding in batch_bindings
        }
        prerequisite_ids = sorted({
            prerequisite
            for binding in batch_bindings
            for prerequisite in units[binding["produced_unit_id"]]
                ["prerequisite_types"]
            if prerequisite not in producer_types
        })
        current_capacity = sum(
            units[binding["producer_unit_id"]]["population_cost"]
            for binding in batch_bindings
        ) + sum(units[type_id]["population_cost"]
                for type_id in prerequisite_ids)
        required_capacity = sum(
            units[binding["produced_unit_id"]]["population_cost"]
            for binding in batch_bindings
        )
        support_capacity = units[POPULATION_SUPPORT_TYPE_ID]["population_cost"]
        deficit = max(required_capacity - current_capacity, 0)
        support_count = ((deficit + support_capacity - 1) // support_capacity
                         if support_capacity else 0)
        if support_count > len(POPULATION_SUPPORT_POSITIONS):
            raise ValueError(
                f"batch {batch_index} needs {support_count} population "
                "support positions")
        if len(prerequisite_ids) > len(PREREQUISITE_POSITIONS):
            raise ValueError(
                f"batch {batch_index} needs {len(prerequisite_ids)} "
                "prerequisite positions")
        active_slots = slots[:
            len(batch_bindings) + len(prerequisite_ids) + support_count]
        selected_slots = active_slots[:len(batch_bindings)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        # This shortened active list intentionally contains only the generated
        # units.  Retaining the template's BGI cells would leave footprints
        # for now-inactive template objects and create phantom blockers.
        visibility = bytearray(len(base.record(MAP_BASE_VISIBILITY_RECORD).data))
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  active_slots[0] * SCENARIO_OBJECT_STRIDE)
        # Command-index 0x11 resets catalog durations and 0x12 raises this
        # owner's population cap to 500.  The latter is an original debug
        # command and prevents otherwise-valid high-population bindings in a
        # shared batch from suppressing one another.
        packets = [
            make_packet(30, 0, 0, 0x30),
            make_packet(30, 1, 0, 0x31),
        ]
        initially_occupied: set[tuple[int, int]] = set()
        existing_centers: set[tuple[int, int]] = set()
        for prerequisite_index, type_id in enumerate(prerequisite_ids):
            x, y = PREREQUISITE_POSITIONS[prerequisite_index]
            initially_occupied.update(footprint_cells(units[type_id], x, y))
            existing_centers.add((x >> 5, y >> 5))
        for support_index in range(support_count):
            x, y = POPULATION_SUPPORT_POSITIONS[support_index]
            initially_occupied.update(footprint_cells(
                units[POPULATION_SUPPORT_TYPE_ID], x, y))
            existing_centers.add((x >> 5, y >> 5))
        producer_positions = choose_producer_positions(
            batch_bindings, units, exit_offsets, base.record(10).data,
            base.record(12).data, initially_occupied, existing_centers,
            map_width, map_height)
        cases = []
        for local_index, binding in enumerate(batch_bindings):
            producer_id = binding["producer_unit_id"]
            produced_id = binding["produced_unit_id"]
            producer = units[producer_id]
            produced = units[produced_id]
            template_id = producer_id if producer_id in library else 96
            slot = selected_slots[local_index]
            previous_link = (0 if local_index == 0 else
                             active_slots[local_index - 1] *
                             SCENARIO_OBJECT_STRIDE)
            next_link = (0 if local_index + 1 == len(active_slots) else
                         active_slots[local_index + 1] *
                         SCENARIO_OBJECT_STRIDE)
            x, y = producer_positions[local_index]
            row = bytearray(configure_row(
                library[template_id], producer, 0, x, y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            # A newly placed production building owns itself through raw
            # +0x94 until the player assigns a rally target.  Some reusable
            # map rows (notably Alien4) retain a pointer to another scenario
            # unit instead.  Copying that template pointer into this shortened
            # synthetic active list leaves it referring to an orphaned pool
            # row, which tests malformed fixed-pool recovery rather than the
            # no-rally production command represented by this batch.
            write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            register_serialized_building_footprint(
                visibility, producer, 0, x, y)
            packets.append(make_packet(
                COMMAND_FRAME, len(packets), 0, 0x01,
                produced_id, slot * SCENARIO_OBJECT_STRIDE, 0, 0, 0))
            cases.append({
                "case_id": f"unit_production_{producer_id:03d}_{produced_id:03d}",
                "producer_slot": slot,
                "producer_unit_id": producer_id,
                "producer_unit_name": producer["name"],
                "produced_unit_id": produced_id,
                "produced_unit_name": produced["name"],
                "primary_cost": produced["resource_cost"],
                "secondary_cost": produced["secondary_cost"],
                "population_cost": produced["population_cost"],
                "used_exact_row_template": producer_id in library,
            })

        for prerequisite_index, type_id in enumerate(prerequisite_ids):
            chain_index = len(batch_bindings) + prerequisite_index
            slot = active_slots[chain_index]
            previous_link = (active_slots[chain_index - 1] *
                             SCENARIO_OBJECT_STRIDE)
            next_link = (0 if chain_index + 1 == len(active_slots) else
                         active_slots[chain_index + 1] *
                         SCENARIO_OBJECT_STRIDE)
            prerequisite = units[type_id]
            x, y = PREREQUISITE_POSITIONS[prerequisite_index]
            template_id = type_id if type_id in library else 96
            row = bytearray(configure_row(
                library[template_id], prerequisite, 0, x, y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            register_serialized_building_footprint(
                visibility, prerequisite, 0, x, y)

        for support_index in range(support_count):
            chain_index = (len(batch_bindings) + len(prerequisite_ids) +
                           support_index)
            slot = active_slots[chain_index]
            previous_link = (active_slots[chain_index - 1] *
                             SCENARIO_OBJECT_STRIDE)
            next_link = (0 if chain_index + 1 == len(active_slots) else
                         active_slots[chain_index + 1] *
                         SCENARIO_OBJECT_STRIDE)
            support = units[POPULATION_SUPPORT_TYPE_ID]
            x, y = POPULATION_SUPPORT_POSITIONS[support_index]
            row = bytearray(configure_row(
                library[POPULATION_SUPPORT_TYPE_ID], support, 0, x, y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            register_serialized_building_footprint(
                visibility, support, 0, x, y)

        stem = f"(2) GP Unit Production B{batch_index:02d}"
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
        map_records[3] = replace_trc_record(
            base.record(3), bytes(player_record))
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
            "population_support_count": support_count,
        })
        print(f"batch={batch_index:02d} bindings={len(cases)}")

    path = tool_dir / "unit_production_batch_manifest.json"
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
    print(f"manifest={path}")
    print(f"bindings={len(bindings)} batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
