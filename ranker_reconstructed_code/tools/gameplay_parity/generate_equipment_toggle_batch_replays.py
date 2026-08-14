#!/usr/bin/env python3
"""Generate every UI-reachable subtype-04 equipment-slot transition."""

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
END_FRAME = 80


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    effects = report["player_equipment_effects"]
    cases = []
    for effect in effects:
        for unit_id in effect["allowed_mobile_unit_ids"]:
            # Generic-slot buttons publish subtype 04 only for mode <= 3.
            # Shipped primary/secondary effects are all mode 0.
            if effect["mode"] <= 3:
                cases.append(("generic_click", effect, unit_id))
            if effect["category"] in (1, 2):
                cases.append(("equipped_remove", effect, unit_id))

    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    if not slots:
        raise ValueError("base map has no active slots")
    batch_size = len(slots)
    fallback_types = {0: 1, 1: 75, 2: 48, 3: 77, 4: 79}
    base_replay_archive = TrcArchive(game / "Replays" / "error1.ply")
    base_replay = next(record for record in base_replay_archive.records
                       if record.name.casefold() == "replay")
    replay_header_template = base_replay.data[:REPLAY_HEADER_SIZE]
    manifest = {
        "schema": 1,
        "case_count": len(cases),
        "batch_size": batch_size,
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(cases), batch_size)):
        batch_input = cases[start:start + batch_size]
        selected_slots = slots[:len(batch_input)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        packets = []
        batch_cases = []

        for local_index, (operation, effect, source_type) in enumerate(batch_input):
            source = units[source_type]
            template_type = (source_type if source_type in library else
                             fallback_types[source["movement_or_render_class"]])
            slot = selected_slots[local_index]
            previous_link = (0 if local_index == 0 else
                             selected_slots[local_index - 1] *
                             SCENARIO_OBJECT_STRIDE)
            next_link = (0 if local_index + 1 == len(selected_slots) else
                         selected_slots[local_index + 1] *
                         SCENARIO_OBJECT_STRIDE)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            x = u32(base_objects, base_offset + 0xB8)
            y = u32(base_objects, base_offset + 0xBC)
            row = bytearray(configure_row(
                library[template_type], source, 0, x, y,
                previous_link, next_link))
            for offset in range(0x30, 0x48, 4):
                write_u32(row, offset, 0)

            initial_equipment = [0] * 6
            if operation == "generic_click":
                storage_index = 0
                slot_code = 3
                initial_equipment[0] = effect["effect_id"]
                if effect["category"] == 0:
                    expected_equipment = ([0] * 6 if effect["mode"] in (2, 3)
                                          else initial_equipment[:])
                else:
                    expected_equipment = [0] * 6
                    expected_equipment[4 if effect["category"] == 1 else 5] = \
                        effect["effect_id"]
            else:
                storage_index = 4 if effect["category"] == 1 else 5
                slot_code = 1 if effect["category"] == 1 else 2
                initial_equipment[storage_index] = effect["effect_id"]
                expected_equipment = [0] * 6
                expected_equipment[0] = effect["effect_id"]
            write_u32(row, 0x30 + storage_index * 4, effect["effect_id"])
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            packets.append(make_packet(
                COMMAND_FRAME, local_index, 0, 0x04,
                slot_code, slot * SCENARIO_OBJECT_STRIDE))
            batch_cases.append({
                "case_id": (f"equipment_toggle_{operation}_"
                            f"{effect['effect_id']:03d}_u{source_type:03d}"),
                "operation": operation,
                "effect_id": effect["effect_id"],
                "effect_name": effect["name"],
                "category": effect["category"],
                "mode": effect["mode"],
                "source_slot": slot,
                "source_unit_id": source_type,
                "source_unit_name": source["name"],
                "slot_code": slot_code,
                "initial_equipment": initial_equipment,
                "expected_equipment": expected_equipment,
                "used_exact_row_template": source_type in library,
            })

        map_stem = f"(2) GP Equip Toggle B{batch_index:02d}"
        map_path = game / "Maps" / f"{map_stem}.trk"
        replay_path = game / "Replays" / f"{map_stem}.ply"
        map_records = list(base.records)
        map_records[0] = replace_trc_record(base.record(0), bytes(header))
        map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
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
            "cases": batch_cases,
        })
        print(f"batch={batch_index:02d} cases={len(batch_cases)}")

    manifest_path = tool_dir / "equipment_toggle_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"cases={len(cases)} batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
