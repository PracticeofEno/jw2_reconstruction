#!/usr/bin/env python3
"""Generate real harvest-tile commands for every selector-seven unit type."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import build_row_library, chain_slots, configure_row
from generate_skill_replay import make_packet, write_u32
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE, SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET, SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE, TrcArchive, append_trc_record,
    replace_trc_record, u32, write_trc_archive)


MAP_STORAGE_WIDTH = 256
COMMAND_FRAME = 31
END_FRAME = 250


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    capable = [row for row in report["units"]
               if row["initial_command_or_type_flags"] & (1 << 7)]
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    terrain = base.record(12).data
    resource_indices = [
        index for index in range(len(terrain) // 4)
        if (u32(terrain, index * 4) & 0x0FFFF000) != 0
        and index % MAP_STORAGE_WIDTH > 1
        and index // MAP_STORAGE_WIDTH > 1
    ][:len(capable)]
    if len(resource_indices) != len(capable):
        raise ValueError("base map does not contain enough harvestable tiles")
    selected_slots = slots[:len(capable)]
    header = bytearray(base_header)
    objects = bytearray(base_objects)
    write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
              selected_slots[0] * SCENARIO_OBJECT_STRIDE)
    write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
    packets = []
    cases = []
    for index, (unit, slot, resource_index) in enumerate(zip(
            capable, selected_slots, resource_indices)):
        tile_x = resource_index % MAP_STORAGE_WIDTH
        tile_y = resource_index // MAP_STORAGE_WIDTH
        target_x = tile_x * 32 + 16
        target_y = tile_y * 32 + 16
        x, y = target_x - 32, target_y
        previous = (0 if index == 0 else
                    selected_slots[index - 1] * SCENARIO_OBJECT_STRIDE)
        following = (0 if index + 1 == len(selected_slots) else
                     selected_slots[index + 1] * SCENARIO_OBJECT_STRIDE)
        row = bytearray(configure_row(
            library[unit["unit_id"]], unit, 0, x, y,
            previous, following))
        ref = slot * SCENARIO_OBJECT_STRIDE
        write_u32(row, 0x94, ref)
        base_offset = slot * SCENARIO_OBJECT_STRIDE
        objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
        packets.append(make_packet(
            COMMAND_FRAME, len(packets), 0, 0x02, 0x07,
            ref, 0, target_x, target_y))
        cases.append({
            "unit_slot": slot, "unit_id": unit["unit_id"],
            "unit_name": unit["name"], "resource_tile": [tile_x, tile_y],
            "resource_initial_amount": (
                u32(terrain, resource_index * 4) & 0x0FFFF000) >> 12,
        })
    stem = "(2) GP Harvest"
    map_path = game / "Maps" / f"{stem}.trk"
    replay_path = game / "Replays" / f"{stem}.ply"
    map_records = list(base.records)
    map_records[0] = replace_trc_record(base.record(0), bytes(header))
    map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
    write_trc_archive(map_path, map_records, base.directory_slots)
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header = bytearray(replay_record.data[:REPLAY_HEADER_SIZE])
    replay_header[0x5F] = 0
    replay_header[0x87] = 5
    map_name = f"Maps\\{map_path.name}".encode("ascii")
    replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
    replay_header[0x1FB:0x1FB + len(map_name)] = map_name
    payload = bytes(replay_header) + b"".join(packets)
    payload += make_packet(END_FRAME, len(packets), 0, 0x13)
    replay_records = append_trc_record(map_records, "Replay", payload, 2)
    write_trc_archive(replay_path, replay_records, base.directory_slots)
    manifest = {
        "schema": 1, "binding_count": len(cases),
        "command_frame": COMMAND_FRAME, "end_frame": END_FRAME,
        "map": map_path.relative_to(root).as_posix(),
        "replay": replay_path.relative_to(root).as_posix(),
        "replay_sha256": hashlib.sha256(
            replay_path.read_bytes()).hexdigest().upper(),
        "cases": cases,
    }
    manifest_path = tool_dir / "harvest_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"bindings={len(cases)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
