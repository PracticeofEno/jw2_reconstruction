#!/usr/bin/env python3
"""Generate primary point-action and guard/idle commands for every capable type."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import (
    BATCH_SIZE, SOURCE_POSITIONS, build_row_library, chain_slots, configure_row)
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import MAP_BASE_VISIBILITY_RECORD
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE, SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET, SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE, TrcArchive, append_trc_record,
    replace_trc_record, write_trc_archive)


PRIMARY_FRAME = 31
GUARD_FRAME = 70
END_FRAME = 120


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    capable = [row for row in report["units"]
               if row["initial_command_or_type_flags"] & 1]
    primary_ids = {row["unit_id"] for row in report["units"]
                   if row["initial_command_or_type_flags"] & 2}
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
        "schema": 1,
        "guard_binding_count": len(capable),
        "primary_binding_count": len(primary_ids),
        "primary_frame": PRIMARY_FRAME,
        "guard_frame": GUARD_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }
    for batch_index, start in enumerate(range(0, len(capable), BATCH_SIZE)):
        batch = capable[start:start + BATCH_SIZE]
        selected_slots = slots[:len(batch)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        packets = []
        cases = []
        for index, (unit, slot) in enumerate(zip(batch, selected_slots)):
            x, y = SOURCE_POSITIONS[index]
            previous = (0 if index == 0 else
                        selected_slots[index - 1] * SCENARIO_OBJECT_STRIDE)
            following = (0 if index + 1 == len(selected_slots) else
                         selected_slots[index + 1] * SCENARIO_OBJECT_STRIDE)
            template = library.get(
                unit["unit_id"], library[fallback_types[
                    unit["movement_or_render_class"]]])
            row = bytearray(configure_row(
                template, unit, 0, x, y, previous, following))
            ref = slot * SCENARIO_OBJECT_STRIDE
            write_u32(row, 0x94, ref)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            if unit["unit_id"] in primary_ids:
                packets.append(make_packet(
                    PRIMARY_FRAME, len(packets), 0, 0x02, 0x01,
                    ref, 0, x, y))
            cases.append({
                "unit_slot": slot,
                "unit_id": unit["unit_id"],
                "unit_name": unit["name"],
                "primary_capable": unit["unit_id"] in primary_ids,
            })
        for slot in selected_slots:
            packets.append(make_packet(
                GUARD_FRAME, len(packets), 0, 0x02, 0x00,
                slot * SCENARIO_OBJECT_STRIDE, 0, 0, 0))
        stem = f"(2) GP Basic Command B{batch_index:02d}"
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
        print(f"batch={batch_index:02d} units={len(cases)}")
    manifest_path = tool_dir / "basic_command_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"guard={len(capable)} primary={len(primary_ids)} "
          f"batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
