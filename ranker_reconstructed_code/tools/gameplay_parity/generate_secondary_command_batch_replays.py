#!/usr/bin/env python3
"""Generate activation/cancellation fixtures for selectors 0x12..0x15."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import BATCH_SIZE, SOURCE_POSITIONS, build_row_library, chain_slots, configure_row
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import MAP_BASE_VISIBILITY_RECORD
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    replace_trc_record,
    write_trc_archive,
)


ACTIVATE_FRAME = 31
CANCEL_FRAME = 55
END_FRAME = 85
COMMAND_FLAGS = {0x12: 0x4000, 0x13: 0x8000, 0x14: 0x10000, 0x15: 0x20000}


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    bindings = [
        {"unit": unit, "selector": selector}
        for unit in report["units"]
        for selector in range(0x12, 0x16)
        if (unit["initial_command_or_type_flags"] & (1 << selector)) != 0
    ]
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    fallback_types = {0: 1, 2: 48, 3: 77, 4: 79}
    manifest = {
        "schema": 1,
        "binding_count": len(bindings),
        "activate_frame": ACTIVATE_FRAME,
        "cancel_frame": CANCEL_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(bindings), BATCH_SIZE)):
        batch = bindings[start:start + BATCH_SIZE]
        selected_slots = slots[:len(batch)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        # Actions 0x20..0x23 require production orders 55..58.  Set every
        # owner-zero order variant to one so the replay represents the UI-
        # enabled player path for all four faction command groups.
        for order in range(59):
            header[0x2BD8 + order * 4] = 1
        packets = []
        cases = []
        for index, (binding, slot) in enumerate(zip(batch, selected_slots)):
            unit = binding["unit"]
            selector = binding["selector"]
            x, y = SOURCE_POSITIONS[index]
            previous_link = (0 if index == 0 else
                             selected_slots[index - 1] * SCENARIO_OBJECT_STRIDE)
            next_link = (0 if index + 1 == len(selected_slots) else
                         selected_slots[index + 1] * SCENARIO_OBJECT_STRIDE)
            template = library.get(unit["unit_id"], library[
                fallback_types[unit["movement_or_render_class"]]])
            row = bytearray(configure_row(
                template, unit, 0, x, y, previous_link, next_link))
            write_u32(row, 0x10, 1_000_000)
            write_u32(row, 0x14, 1_000_000)
            write_u32(row, 0x18, 1_000_000)
            write_u32(row, 0x24, 1_000_000)
            write_u32(row, 0x2C, 1_000_000)
            write_u32(row, 0x60, 1)
            write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            packets.append(make_packet(
                ACTIVATE_FRAME, len(packets), 0, 0x02,
                selector, slot * SCENARIO_OBJECT_STRIDE, 0, x, y))
            cases.append({
                "case_id": f"secondary_{selector:02x}_{unit['unit_id']:03d}",
                "unit_slot": slot,
                "unit_id": unit["unit_id"],
                "unit_name": unit["name"],
                "action_selector": selector,
                "attachment_action_id": selector + 0x0E,
                "command_flag": COMMAND_FLAGS[selector],
            })
        for index, (binding, slot) in enumerate(zip(batch, selected_slots)):
            selector = binding["selector"]
            packets.append(make_packet(
                CANCEL_FRAME, len(packets), 0, 0x02,
                selector, slot * SCENARIO_OBJECT_STRIDE,
                1, COMMAND_FLAGS[selector], SOURCE_POSITIONS[index][1]))

        stem = f"(2) GP Secondary Command B{batch_index:02d}"
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

    manifest_path = tool_dir / "secondary_command_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"bindings={len(bindings)} batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
