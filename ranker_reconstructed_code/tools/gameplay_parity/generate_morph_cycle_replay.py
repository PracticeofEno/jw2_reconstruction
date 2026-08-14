#!/usr/bin/env python3
"""Generate a replay that enters and exits every player-operable morph pair."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import build_row_library, chain_slots, configure_row
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


ENTER_FRAME = 31
EXIT_FRAME = 55
END_FRAME = 90


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    bindings = [
        (unit["unit_id"], unit["alternate_morph_type"])
        for unit in report["units"]
        if (unit["initial_command_or_type_flags"] & (1 << 0x11)) != 0 and
        unit["alternate_morph_type"] != 0
    ]

    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    header = bytearray(base.record(0).data)
    objects = bytearray(base.record(SCENARIO_OBJECT_RECORD).data)
    slots = chain_slots(
        bytes(header), bytes(objects), SCENARIO_ACTIVE_HEAD_OFFSET)[:len(bindings)]
    if len(slots) != len(bindings):
        raise ValueError("morph fixture lacks active template slots")
    write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
              slots[0] * SCENARIO_OBJECT_STRIDE)
    # The local UI publishes command 0x11 only after production order 0x2a
    # has been unlocked.  Preserve that player-operable gate in the map even
    # though replay injection itself enters below the UI layer.
    header[0x2BD8 + 0x2A * 4] = 1
    positions = ((512, 512), (768, 512), (1024, 512),
                 (1280, 512), (1536, 512))
    packets = []
    cases = []
    for index, ((source_id, target_id), slot) in enumerate(zip(bindings, slots)):
        source = units[source_id]
        previous_link = 0 if index == 0 else slots[index - 1] * SCENARIO_OBJECT_STRIDE
        next_link = (0 if index + 1 == len(slots) else
                     slots[index + 1] * SCENARIO_OBJECT_STRIDE)
        x, y = positions[index]
        row = bytearray(configure_row(
            library[source_id], source, 0, x, y, previous_link, next_link))
        write_u32(row, 0x60, 1)
        write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
        base_offset = slot * SCENARIO_OBJECT_STRIDE
        objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
        packets.append(make_packet(
            ENTER_FRAME, len(packets), 0, 0x02,
            0x11, slot * SCENARIO_OBJECT_STRIDE, 0, x, y))
        cases.append({
            "case_id": f"morph_{source_id:03d}_{target_id:03d}",
            "unit_slot": slot,
            "source_unit_id": source_id,
            "source_unit_name": source["name"],
            "morph_unit_id": target_id,
            "morph_unit_name": units[target_id]["name"],
        })
    for index, ((source_id, _), slot) in enumerate(zip(bindings, slots)):
        x, y = positions[index]
        packets.append(make_packet(
            EXIT_FRAME, len(packets), 0, 0x02,
            0x1B, slot * SCENARIO_OBJECT_STRIDE, 0, x, y))

    map_records = list(base.records)
    map_records[0] = replace_trc_record(base.record(0), bytes(header))
    map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
    map_records[MAP_BASE_VISIBILITY_RECORD] = replace_trc_record(
        base.record(MAP_BASE_VISIBILITY_RECORD),
        bytes(len(base.record(MAP_BASE_VISIBILITY_RECORD).data)))
    stem = "(2) GP Morph Cycle"
    map_path = game / "Maps" / f"{stem}.trk"
    replay_path = game / "Replays" / f"{stem}.ply"
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
        "schema": 1,
        "enter_frame": ENTER_FRAME,
        "exit_frame": EXIT_FRAME,
        "end_frame": END_FRAME,
        "map": map_path.relative_to(root).as_posix(),
        "replay": replay_path.relative_to(root).as_posix(),
        "replay_sha256": hashlib.sha256(
            replay_path.read_bytes()).hexdigest().upper(),
        "cases": cases,
    }
    manifest_path = tool_dir / "morph_cycle_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"bindings={len(cases)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
