#!/usr/bin/env python3
"""Generate every player-operable production-order cancellation position."""

from __future__ import annotations

import hashlib
import json
from collections import defaultdict
from pathlib import Path

from generate_attack_batch_replays import (
    SOURCE_POSITIONS,
    build_row_library,
    chain_slots,
    configure_row,
)
from generate_production_order_batch_replays import rule_value
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
    write_trc_archive,
)


ENQUEUE_FRAME = 31
CANCEL_FRAME = 34
END_FRAME = 40
BATCH_SIZE = 8


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    orders = {row["order_id"]: row for row in report["production_orders"]}
    orders_by_source: dict[int, list[int]] = defaultdict(list)
    for binding in report["production_order_bindings"]:
        orders_by_source[binding["unit_id"]].append(binding["order_id"])
    for values in orders_by_source.values():
        values[:] = sorted(set(values))

    cases = []
    for source_type, source_orders in sorted(orders_by_source.items()):
        maximum_index = min(4, len(source_orders) - 1)
        for target_order in source_orders:
            fillers = [order for order in source_orders if order != target_order]
            for logical_index in range(maximum_index + 1):
                queue = fillers[:logical_index] + [target_order]
                cases.append({
                    "case_id": (f"production_order_cancel_u{source_type:03d}_"
                                f"o{target_order:02d}_q{logical_index}"),
                    "source_unit_id": source_type,
                    "source_unit_name": units[source_type]["name"],
                    "order_id": target_order,
                    "order_name": orders[target_order]["name"],
                    "logical_index": logical_index,
                    "queued_order_ids": queue,
                })

    pending_cases = cases
    case_batches = []
    while pending_cases:
        used_orders = set()
        batch = []
        remaining = []
        for case in pending_cases:
            queue_orders = set(case["queued_order_ids"])
            if (len(batch) < BATCH_SIZE and
                    not (queue_orders & used_orders)):
                batch.append(case)
                used_orders.update(queue_orders)
            else:
                remaining.append(case)
        case_batches.append(batch)
        pending_cases = remaining

    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    base_replay_archive = TrcArchive(game / "Replays" / "error1.ply")
    base_replay = next(record for record in base_replay_archive.records
                       if record.name.casefold() == "replay")
    replay_header_template = base_replay.data[:REPLAY_HEADER_SIZE]
    manifest = {
        "schema": 1,
        "binding_count": len(report["production_order_bindings"]),
        "case_count": len(cases),
        "enqueue_frame": ENQUEUE_FRAME,
        "cancel_frame": CANCEL_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, batch_cases in enumerate(case_batches):
        selected_slots = slots[:len(batch_cases)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        # +0x1698 is owner[8] x unit-type[170], ending at +0x2bd8.
        for owner in range(8):
            for unit_type in range(170):
                write_u32(header,
                          0x1698 + (owner * 170 + unit_type) * 4, 1)
        for owner in range(8):
            for order_id in range(64):
                write_u32(header, 0x2BD8 + (owner * 64 + order_id) * 4, 0)
        header[0x469C:0x469C + 8 * 4] = b"\0" * (8 * 4)
        header[0x46EC:0x46EC + 18 * 8 * 170 * 4] = \
            b"\0" * (18 * 8 * 170 * 4)

        packet_specs = []
        output_cases = []
        batch_net_cost = 0
        batch_secondary_refund = 0
        for local_index, case in enumerate(batch_cases):
            owner = 0
            source_type = case["source_unit_id"]
            source = units[source_type]
            template_type = source_type if source_type in library else 96
            slot = selected_slots[local_index]
            previous_link = (0 if local_index == 0 else
                             selected_slots[local_index - 1] *
                             SCENARIO_OBJECT_STRIDE)
            next_link = (0 if local_index + 1 == len(selected_slots) else
                         selected_slots[local_index + 1] *
                         SCENARIO_OBJECT_STRIDE)
            x, y = SOURCE_POSITIONS[local_index]
            row = bytearray(configure_row(
                library[template_type], source, owner, x, y,
                previous_link, next_link))
            write_u32(row, 0x60, 1)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row

            queued_cost = 0
            for queued_order in case["queued_order_ids"]:
                secondary_cost = rule_value(
                    orders[queued_order]["secondary_cost"], 0)
                queued_cost += rule_value(
                    orders[queued_order]["primary_cost"], 0)
                packet_specs.append((
                    ENQUEUE_FRAME, owner, queued_order,
                    slot * SCENARIO_OBJECT_STRIDE, secondary_cost, 0))
            packet_specs.append((
                CANCEL_FRAME, owner, case["order_id"],
                slot * SCENARIO_OBJECT_STRIDE, 1, case["logical_index"]))
            refunded = rule_value(orders[case["order_id"]]["primary_cost"], 0)
            batch_net_cost += queued_cost - refunded
            # The original subtype-0x0c refund deliberately uses the primary
            # formula for both owner resource arrays.
            batch_secondary_refund += refunded
            output_cases.append({
                **case,
                "owner": owner,
                "source_slot": slot,
                "expected_remaining_order_ids": [
                    order for index, order in
                    enumerate(case["queued_order_ids"])
                    if index != case["logical_index"]],
            })

        map_stem = f"(2) GP Production Cancel B{batch_index:02d}"
        map_path = game / "Maps" / f"{map_stem}.trk"
        replay_path = game / "Replays" / f"{map_stem}.ply"
        map_records = list(base.records)
        map_records[0] = replace_trc_record(base.record(0), bytes(header))
        map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
        player_record = bytearray(base.record(3).data)
        for owner in range(8):
            write_u32(player_record, 0x144 + owner * 4, 100_000)
            write_u32(player_record, 0x194 + owner * 4, 100_000)
        map_records[3] = replace_trc_record(base.record(3), bytes(player_record))
        write_trc_archive(map_path, map_records, base.directory_slots)

        replay_header = bytearray(replay_header_template)
        replay_header[0x5F] = 0
        replay_header[0x87] = 5
        map_name = f"Maps\\{map_path.name}".encode("ascii")
        replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
        replay_header[0x1FB:0x1FB + len(map_name)] = map_name
        packets = [make_packet(30, 0, 0, 0x30)]
        for packet_sequence, spec in enumerate(
                sorted(packet_specs, key=lambda row: row[0]), 1):
            frame, owner, order_id, unit_offset, mode, logical_index = spec
            packets.append(make_packet(
                frame, packet_sequence, owner, 0x0C, order_id, unit_offset,
                mode, logical_index, 0))
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
            "expected_owner_primary_after_batch": 100_000 - batch_net_cost,
            "expected_owner_secondary_after_batch": (
                100_000 + batch_secondary_refund),
            "cases": output_cases,
        })
        print(f"batch={batch_index:02d} cases={len(output_cases)}")

    manifest_path = tool_dir / "production_order_cancel_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"bindings={manifest['binding_count']} cases={len(cases)} "
          f"batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
