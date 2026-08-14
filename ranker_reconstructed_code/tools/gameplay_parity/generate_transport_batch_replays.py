#!/usr/bin/env python3
"""Generate deterministic unload fixtures for every valid carrier/passenger pair."""

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
from generate_unit_production_batch_replays import MAP_BASE_VISIBILITY_RECORD
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    replace_trc_record,
    write_trc_archive,
)


COMMAND_FRAME = 31
END_FRAME = 110


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    carriers = [row for row in report["units"]
                if (row["initial_command_or_type_flags"] & (1 << 0x0A))]
    pairs = []
    for carrier in carriers:
        capacity = carrier["transport_value"]
        for passenger in report["units"]:
            passenger_size = passenger["transport_value"]
            if (passenger["unit_id"] != carrier["unit_id"] and
                    passenger["unit_id"] < 0x60 and
                    0 < passenger_size <= capacity):
                pairs.append((carrier, passenger))

    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    if len(slots) < BATCH_SIZE * 2:
        raise ValueError("transport fixture lacks sixteen active slots")
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    fallback_types = {0: 1, 1: 113, 2: 48, 3: 77, 4: 79}
    manifest = {
        "schema": 1,
        "pair_count": len(pairs),
        "carrier_count": len(carriers),
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(pairs), BATCH_SIZE)):
        batch = pairs[start:start + BATCH_SIZE]
        selected_slots = slots[:len(batch) * 2]
        carrier_slots = selected_slots[:len(batch)]
        passenger_slots = selected_slots[len(batch):]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        packets = []
        cases = []
        for local_index, ((carrier, passenger), carrier_slot,
                          passenger_slot) in enumerate(zip(
                              batch, carrier_slots, passenger_slots)):
            carrier_x, carrier_y = SOURCE_POSITIONS[local_index]
            passenger_x, passenger_y = carrier_x, carrier_y
            carrier_position = local_index
            passenger_position = len(batch) + local_index
            carrier_previous = (0 if carrier_position == 0 else
                selected_slots[carrier_position - 1] * SCENARIO_OBJECT_STRIDE)
            carrier_next = (selected_slots[carrier_position + 1] *
                            SCENARIO_OBJECT_STRIDE)
            passenger_previous = (selected_slots[passenger_position - 1] *
                                  SCENARIO_OBJECT_STRIDE)
            passenger_next = (0 if passenger_position + 1 == len(selected_slots)
                              else selected_slots[passenger_position + 1] *
                              SCENARIO_OBJECT_STRIDE)
            carrier_template = library.get(
                carrier["unit_id"], library[fallback_types[
                    carrier["movement_or_render_class"]]])
            passenger_template = library.get(
                passenger["unit_id"], library[fallback_types[
                    passenger["movement_or_render_class"]]])
            carrier_row = bytearray(configure_row(
                carrier_template, carrier, 0, carrier_x, carrier_y,
                carrier_previous, carrier_next))
            passenger_row = bytearray(configure_row(
                passenger_template, passenger, 0, passenger_x, passenger_y,
                passenger_previous, passenger_next))
            carrier_ref = carrier_slot * SCENARIO_OBJECT_STRIDE
            passenger_ref = passenger_slot * SCENARIO_OBJECT_STRIDE
            passenger_size = passenger["transport_value"]
            # Raw +0x4c is the carrier's occupied capacity.  An attached
            # passenger is hidden, in state 0x45, and targets its carrier.
            write_u32(carrier_row, 0x4C, passenger_size)
            write_u32(carrier_row, 0x94, carrier_ref)
            write_u32(passenger_row, 0x4C, 0)
            write_u32(passenger_row, 0x60, 0x45)
            write_u32(passenger_row, 0x68, carrier_ref)
            write_u32(passenger_row, 0x94, passenger_ref)
            write_u32(passenger_row, 0xA0, 0x80)
            carrier_base = carrier_slot * SCENARIO_OBJECT_STRIDE
            passenger_base = passenger_slot * SCENARIO_OBJECT_STRIDE
            objects[carrier_base:carrier_base + SCENARIO_OBJECT_STRIDE] = carrier_row
            objects[passenger_base:passenger_base + SCENARIO_OBJECT_STRIDE] = passenger_row
            packets.append(make_packet(
                COMMAND_FRAME, len(packets), 0, 0x02, 0x24,
                passenger_ref, carrier_ref, carrier_x + 96, carrier_y + 64))
            cases.append({
                "case_id": (f"transport_{carrier['unit_id']:03d}_"
                            f"{passenger['unit_id']:03d}"),
                "carrier_slot": carrier_slot,
                "carrier_unit_id": carrier["unit_id"],
                "carrier_unit_name": carrier["name"],
                "carrier_capacity": carrier["transport_value"],
                "passenger_slot": passenger_slot,
                "passenger_unit_id": passenger["unit_id"],
                "passenger_unit_name": passenger["name"],
                "passenger_size": passenger_size,
                # Original boarding entry rejects a passenger that has the
                # carrier type flag.  Retain those catalog-size-compatible
                # pairs as explicit rejection coverage, but do not count them
                # as player-reachable load/unload combinations.
                "player_reachable": not bool(
                    passenger["initial_command_or_type_flags"] & (1 << 0x0A)),
            })

        stem = f"(2) GP Transport B{batch_index:02d}"
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
        print(f"batch={batch_index:02d} pairs={len(cases)}")

    manifest_path = tool_dir / "transport_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"carriers={len(carriers)} pairs={len(pairs)} "
          f"batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
