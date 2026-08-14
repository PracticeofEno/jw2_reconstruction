#!/usr/bin/env python3
"""Generate isolated close-range replays for attack cases not yet exercised."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

from generate_attack_batch_replays import (
    TARGET_TYPES,
    build_row_library,
    chain_slots,
    configure_row,
    spread_building_target_slots,
)
from generate_skill_replay import make_packet, write_u32
from inventory_gameplay_matrix import (
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
END_FRAME = 180
SOURCE_POSITION = (508, 992)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    inventory = json.loads(
        (tool_dir / "reports" / "gameplay_inventory.json")
        .read_text(encoding="utf-8"))
    results = json.loads(
        (tool_dir / "parity_results.json").read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in inventory["units"]}
    library = build_row_library(game / "Maps")

    unresolved = {
        (row["unit_id"], row["target_render_class"]): row
        for row in results["cases"]
        if row.get("kind") == "attack_target_class" and
        row.get("result") == "not_exercised" and
        row.get("expectation") == "execute"
    }

    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    base_slots = chain_slots(
        base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    building = TrcArchive(game / "Maps" / "(4) Far Away v1.2.trk")
    building_header = building.record(0).data
    building_objects = building.record(SCENARIO_OBJECT_RECORD).data
    building_slots = chain_slots(
        building_header, building_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    building_target = min(
        (slot for slot in building_slots
         if u32(building_objects, slot * SCENARIO_OBJECT_STRIDE) ==
         TARGET_TYPES[1]),
        key=lambda slot: (
            u32(building_objects,
                slot * SCENARIO_OBJECT_STRIDE + 0xBC),
            u32(building_objects,
                slot * SCENARIO_OBJECT_STRIDE + 0xB8)))
    building_source = next(
        slot for slot in building_slots
        if slot != building_target and
        units[u32(building_objects, slot * SCENARIO_OBJECT_STRIDE)]
        ["movement_or_render_class"] != 1)

    replay_archive = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(
        record for record in replay_archive.records
        if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    fallback_types = {0: 1, 1: 113, 2: 48, 3: 77, 4: 79}
    manifest = {
        "schema": 1,
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "cases": [],
    }

    for unit_id, render_class in sorted(unresolved):
        unit = units[unit_id]
        target_type = TARGET_TYPES[render_class]
        target_unit = units[target_type]
        source_center_x = (unit["center_bounds_left"] +
                           unit["center_bounds_width"] // 2)
        source_center_y = (unit["center_bounds_top"] +
                           unit["center_bounds_height"] // 2)
        target_center_x = (target_unit["center_bounds_left"] +
                           target_unit["center_bounds_width"] // 2)
        target_center_y = (target_unit["center_bounds_top"] +
                           target_unit["center_bounds_height"] // 2)
        if render_class == 1:
            fixture = building
            header = bytearray(building_header)
            objects = bytearray(building_objects)
            source_slot = building_source
            target_slot = building_target
            target_base = target_slot * SCENARIO_OBJECT_STRIDE
            target_x = u32(objects, target_base + 0xB8)
            target_y = u32(objects, target_base + 0xBC)
            # Align the centers used by FUN_004c1e85.  This is required for a
            # legitimate class-3 range of zero and also avoids confusing the
            # visual origin with the catalog's asymmetric interaction center.
            source_x = target_x + target_center_x - source_center_x
            source_y = target_y + target_center_y - source_center_y
            if unit_id == 30:
                # KingDemon/Null has a 172-pixel center-distance range.  Keep
                # its ground footprint outside the building reservation while
                # remaining one pixel inside that legal range.
                source_y = target_y - 160
            target_template = bytes(
                objects[target_base:target_base + SCENARIO_OBJECT_STRIDE])
        else:
            fixture = base
            header = bytearray(base_header)
            objects = bytearray(base_objects)
            source_slot, target_slot = base_slots[:2]
            source_x, source_y = SOURCE_POSITION
            target_x = source_x + source_center_x - target_center_x
            target_y = source_y + source_center_y - target_center_y
            target_template = library[target_type]

        source_base = source_slot * SCENARIO_OBJECT_STRIDE
        target_base = target_slot * SCENARIO_OBJECT_STRIDE
        source_template = library.get(unit_id)
        if source_template is None:
            source_template = library[
                fallback_types[unit["movement_or_render_class"]]]
        source_row = configure_row(
            source_template, unit, 0, source_x, source_y,
            0, target_slot * SCENARIO_OBJECT_STRIDE)
        target_row = bytearray(configure_row(
            target_template, target_unit, 1, target_x, target_y,
            source_slot * SCENARIO_OBJECT_STRIDE, 0, durable_target=True))
        write_u32(target_row, 0x1C, 0)
        write_u32(target_row, 0x20, 0)
        objects[source_base:source_base + SCENARIO_OBJECT_STRIDE] = source_row
        objects[target_base:target_base + SCENARIO_OBJECT_STRIDE] = target_row
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  source_slot * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)

        stem = f"(2) GP Attack F U{unit_id:03d} C{render_class}"
        map_path = game / "Maps" / f"{stem}.trk"
        replay_path = game / "Replays" / f"{stem}.ply"
        map_records = list(fixture.records)
        map_records[0] = replace_trc_record(
            fixture.record(0), bytes(header))
        map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            fixture.record(SCENARIO_OBJECT_RECORD), bytes(objects))
        write_trc_archive(map_path, map_records, fixture.directory_slots)

        replay_header = bytearray(replay_header_template)
        replay_header[0x5F] = 0
        replay_header[0x87] = 5
        map_name = f"Maps\\{map_path.name}".encode("ascii")
        replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
        replay_header[0x1FB:0x1FB + len(map_name)] = map_name
        packet = make_packet(
            COMMAND_FRAME, 0, 0, 0x02, 5,
            source_slot * SCENARIO_OBJECT_STRIDE,
            target_slot * SCENARIO_OBJECT_STRIDE,
            target_x, target_y)
        replay_payload = bytes(replay_header) + packet
        replay_payload += make_packet(END_FRAME, 1, 0, 0x13)
        replay_records = append_trc_record(
            map_records, "Replay", replay_payload, 2)
        write_trc_archive(
            replay_path, replay_records, fixture.directory_slots)
        manifest["cases"].append({
            "unit_id": unit_id,
            "unit_name": unit["name"],
            "target_render_class": render_class,
            "source_slot": source_slot,
            "target_slot": target_slot,
            "target_initial_health": 1_000_000,
            "map": map_path.relative_to(root).as_posix(),
            "replay": replay_path.relative_to(root).as_posix(),
            "replay_sha256": hashlib.sha256(
                replay_path.read_bytes()).hexdigest().upper(),
            "used_exact_row_template": unit_id in library,
            # Unit 30 / class 1 was repeated with four independent placements:
            # center overlap, 64-pixel cluster edge, exact center alignment,
            # and a non-overlapping 159-pixel center distance.  The original
            # consistently returns to idle without starting profile 32; exact
            # rejection is therefore the parity outcome for this combination.
            "accept_original_rejection": unit_id == 30 and render_class == 1,
        })
        print(f"unit={unit_id:03d} class={render_class} "
              f"replay={replay_path.name}")

    manifest_path = tool_dir / "attack_fallback_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"cases={len(manifest['cases'])} manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
