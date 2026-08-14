#!/usr/bin/env python3
"""Generate selector-three commands for every capable source/valid target pair."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import (
    BATCH_SIZE, SOURCE_POSITIONS, build_row_library, chain_slots, configure_row)
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import (
    MAP_BASE_VISIBILITY_RECORD, MAP_WIDTH_TILES)
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE, SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET, SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE, TrcArchive, append_trc_record,
    replace_trc_record, write_trc_archive)


COMMAND_FRAME = 31
END_FRAME = 180
FOOTPRINT_OCCUPIED = 0x20000000


def register_target_footprint(visibility: bytearray, target: dict,
                              x: int, y: int, owner: int) -> None:
    """Mirror the saved-map footprint bits expected by the original loader.

    The shipped executable consumes record 13 as the authoritative occupancy
    grid when hydrating an existing scenario.  Merely inserting a building in
    record 7 leaves the original pathfinder's map empty, while the rebuilt
    runtime additionally reconstructs the footprint from the typed object.
    Keep generated fixtures self-consistent so both executables pathfind over
    the same map state.
    """
    if target["lifecycle_class"] != 2:
        return
    width = target["footprint_width"]
    height = target["footprint_height"]
    if width == 0 or height == 0:
        return
    base_x = x >> 5
    base_y = y >> 5
    for dy in range(height):
        for dx in range(width):
            cell = (base_y + dy) * MAP_WIDTH_TILES + base_x + dx
            offset = cell * 4
            value = FOOTPRINT_OCCUPIED
            if dx == 0 and dy == 0:
                value |= target["unit_id"] & 0xff
                value |= owner << 8
            write_u32(visibility, offset, value)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    sources = [row for row in report["units"]
               if row["initial_command_or_type_flags"] & (1 << 3)]
    targets = [row for row in report["units"]
               if row["support_target_or_action_effect_flags"] & 1]
    pairs = [(source, target) for source in sources for target in targets]
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    fallback_types = {0: 1, 1: 113, 2: 48, 3: 77, 4: 79}
    manifest = {
        "schema": 1, "source_count": len(sources),
        "target_count": len(targets), "pair_count": len(pairs),
        "command_frame": COMMAND_FRAME, "end_frame": END_FRAME,
        "batches": [],
    }
    for batch_index, start in enumerate(range(0, len(pairs), BATCH_SIZE)):
        batch = pairs[start:start + BATCH_SIZE]
        selected_slots = slots[:len(batch) * 2]
        source_slots = selected_slots[:len(batch)]
        target_slots = selected_slots[len(batch):]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        visibility = bytearray(
            len(base.record(MAP_BASE_VISIBILITY_RECORD).data))
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        packets = []
        cases = []
        for local_index, ((source, target), source_slot,
                          target_slot) in enumerate(zip(
                              batch, source_slots, target_slots)):
            source_x, source_y = SOURCE_POSITIONS[local_index]
            target_x, target_y = source_x + 64, source_y
            source_position = local_index
            target_position = len(batch) + local_index
            source_previous = (0 if source_position == 0 else
                selected_slots[source_position - 1] * SCENARIO_OBJECT_STRIDE)
            source_next = (selected_slots[source_position + 1] *
                           SCENARIO_OBJECT_STRIDE)
            target_previous = (selected_slots[target_position - 1] *
                               SCENARIO_OBJECT_STRIDE)
            target_next = (0 if target_position + 1 == len(selected_slots) else
                           selected_slots[target_position + 1] *
                           SCENARIO_OBJECT_STRIDE)
            source_template = library.get(
                source["unit_id"], library[fallback_types[
                    source["movement_or_render_class"]]])
            target_template = library.get(
                target["unit_id"], library[fallback_types[
                    target["movement_or_render_class"]]])
            source_row = bytearray(configure_row(
                source_template, source, 0, source_x, source_y,
                source_previous, source_next))
            target_row = bytearray(configure_row(
                target_template, target, 0, target_x, target_y,
                target_previous, target_next))
            source_ref = source_slot * SCENARIO_OBJECT_STRIDE
            target_ref = target_slot * SCENARIO_OBJECT_STRIDE
            write_u32(source_row, 0x94, source_ref)
            write_u32(target_row, 0x18, max(1, target["max_health"] // 2))
            write_u32(target_row, 0x94, target_ref)
            source_base = source_slot * SCENARIO_OBJECT_STRIDE
            target_base = target_slot * SCENARIO_OBJECT_STRIDE
            objects[source_base:source_base + SCENARIO_OBJECT_STRIDE] = source_row
            objects[target_base:target_base + SCENARIO_OBJECT_STRIDE] = target_row
            register_target_footprint(
                visibility, target, target_x, target_y, 0)
            packets.append(make_packet(
                COMMAND_FRAME, len(packets), 0, 0x02, 0x03,
                source_ref, target_ref, target_x, target_y))
            cases.append({
                "case_id": (f"definition_group_{source['unit_id']:03d}_"
                            f"{target['unit_id']:03d}"),
                "source_slot": source_slot,
                "source_unit_id": source["unit_id"],
                "source_unit_name": source["name"],
                "target_slot": target_slot,
                "target_unit_id": target["unit_id"],
                "target_unit_name": target["name"],
                "target_initial_health": max(1, target["max_health"] // 2),
            })
        stem = f"(2) GP Definition Group B{batch_index:02d}"
        map_path = game / "Maps" / f"{stem}.trk"
        replay_path = game / "Replays" / f"{stem}.ply"
        map_records = list(base.records)
        map_records[0] = replace_trc_record(base.record(0), bytes(header))
        map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
        map_records[MAP_BASE_VISIBILITY_RECORD] = replace_trc_record(
            base.record(MAP_BASE_VISIBILITY_RECORD),
            bytes(visibility))
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
        print(f"batch={batch_index:02d} pairs={len(cases)}")
    manifest_path = tool_dir / "definition_group_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"sources={len(sources)} targets={len(targets)} pairs={len(pairs)} "
          f"batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
