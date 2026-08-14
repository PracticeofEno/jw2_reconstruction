#!/usr/bin/env python3
"""Generate every player-operable linked pair and triad release."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import (
    SOURCE_POSITIONS, build_row_library, chain_slots, configure_row)
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import MAP_BASE_VISIBILITY_RECORD
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE, SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET, SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE, TrcArchive, append_trc_record,
    replace_trc_record, write_trc_archive)


COMMAND_FRAME = 31
END_FRAME = 350
GROUPS = (
    ("pair_velocis", (34, 34), 35),
    ("pair_rhamphos", (37, 37), 38),
    ("pair_pteras", (39, 39), 45),
    ("triad_dilophos_pteras_trices", (36, 39, 40), 43),
)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    needed = sum(len(group[1]) for group in GROUPS)
    support_count = 2
    active_slots = slots[:needed + support_count]
    selected_slots = active_slots[:needed]
    header = bytearray(base_header)
    objects = bytearray(base_objects)
    write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
              selected_slots[0] * SCENARIO_OBJECT_STRIDE)
    write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    packets = []
    cases = []
    cursor = 0
    for group_index, (case_id, type_ids, result_type) in enumerate(GROUPS):
        group_slots = selected_slots[cursor:cursor + len(type_ids)]
        x, y = SOURCE_POSITIONS[group_index]
        for local_index, (unit_id, slot) in enumerate(zip(type_ids, group_slots)):
            unit = units[unit_id]
            position = cursor + local_index
            previous = (0 if position == 0 else
                        selected_slots[position - 1] * SCENARIO_OBJECT_STRIDE)
            following = (active_slots[position + 1] *
                         SCENARIO_OBJECT_STRIDE)
            row = bytearray(configure_row(
                library[unit_id], unit, 0, x + local_index * 2, y,
                previous, following))
            ref = slot * SCENARIO_OBJECT_STRIDE
            write_u32(row, 0x94, ref)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
        refs = [slot * SCENARIO_OBJECT_STRIDE for slot in group_slots]
        if len(refs) == 2:
            link_packets = ((refs[1], refs[0]), (refs[0], refs[1]))
        else:
            # Outer 36 selects primary 39 and secondary 40.  This is the
            # exact ring emitted by selector 0x0b's triad branch.
            link_packets = ((refs[2], refs[0]), (refs[1], refs[2]),
                            (refs[0], refs[1]))
        for source, target in link_packets:
            packets.append(make_packet(
                COMMAND_FRAME, len(packets), 0, 0x02, 0x0B,
                source, target, x, 0))
        cases.append({
            "case_id": case_id,
            "source_unit_ids": list(type_ids),
            "source_slots": group_slots,
            "result_unit_id": result_type,
            "result_unit_name": units[result_type]["name"],
        })
        cursor += len(type_ids)
    support = units[96]
    for support_index, slot in enumerate(active_slots[needed:]):
        position = needed + support_index
        previous = active_slots[position - 1] * SCENARIO_OBJECT_STRIDE
        following = (0 if position + 1 == len(active_slots) else
                     active_slots[position + 1] * SCENARIO_OBJECT_STRIDE)
        x, y = ((2240, 1920), (2400, 2400))[support_index]
        row = bytearray(configure_row(
            library[96], support, 0, x, y, previous, following))
        write_u32(row, 0x60, 1)
        write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
        base_offset = slot * SCENARIO_OBJECT_STRIDE
        objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
    stem = "(2) GP Linked Release"
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
    manifest = {
        "schema": 1, "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "map": map_path.relative_to(root).as_posix(),
        "replay": replay_path.relative_to(root).as_posix(),
        "replay_sha256": hashlib.sha256(
            replay_path.read_bytes()).hexdigest().upper(),
        "cases": cases,
    }
    manifest_path = tool_dir / "linked_release_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"groups={len(cases)} units={needed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
