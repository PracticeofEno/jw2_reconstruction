#!/usr/bin/env python3
"""Generate completion fixtures for every UI-bound producer/order binding."""

from __future__ import annotations

import hashlib
import json
from collections import defaultdict
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
    write_trc_archive,
)


COMMAND_FRAME = 31
END_FRAME = 100


def signed32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value if value < 0x80000000 else value - 0x100000000


def rule_value(rule: dict, variant: int) -> int:
    value = rule["base"]
    if rule["mode"] == 1:
        value += rule["linear"] * variant
    elif rule["mode"] == 2:
        value += rule["linear"] * variant + rule["extra"]
    elif rule["mode"] == 3 and rule["extra"]:
        value += rule["linear"] * int(variant / rule["extra"])
    elif rule["mode"] == 4:
        value += rule["linear"] * variant
        value += rule["extra"] * variant * (variant + 1) // 2
    return signed32(value)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    orders = {row["order_id"]: row for row in report["production_orders"]}
    bindings_by_order = defaultdict(list)
    for binding in report["production_order_bindings"]:
        bindings_by_order[binding["order_id"]].append(binding)
    player_order_ids = sorted(bindings_by_order)
    pending_bindings = sorted(
        report["production_order_bindings"],
        key=lambda row: (row["order_id"], row["unit_id"]))
    binding_batches = []
    while pending_bindings:
        used_orders = set()
        batch = []
        remaining = []
        for binding in pending_bindings:
            if (len(batch) < BATCH_SIZE and
                    binding["order_id"] not in used_orders):
                batch.append(binding)
                used_orders.add(binding["order_id"])
            else:
                remaining.append(binding)
        binding_batches.append(batch)
        pending_bindings = remaining
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
        "catalog_order_count": len(orders),
        "player_order_count": len(player_order_ids),
        "player_binding_count": len(report["production_order_bindings"]),
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, batch_bindings in enumerate(binding_batches):
        selected_slots = slots[:len(batch_bindings)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        # Make every shipped prerequisite present for owner zero.  Original
        # DAT_00707430 is the same primary-record region at +0x1698.
        for unit_type in range(170):
            write_u32(header, 0x1698 + unit_type * 4, 1)
        # Start every order at variant zero with no lock/opaque bits.
        for owner in range(8):
            for order_id in range(64):
                write_u32(header, 0x2BD8 + (owner * 64 + order_id) * 4, 0)
        # The OBC template carries campaign upgrade residue.  Clear the live
        # order-0x2b total and all 18 owner/type completion-effect tables so
        # each terminal delta belongs to the command exercised by this batch.
        header[0x469C:0x469C + 8 * 4] = b"\0" * (8 * 4)
        header[0x46EC:0x46EC + 18 * 8 * 170 * 4] = \
            b"\0" * (18 * 8 * 170 * 4)

        packets = [make_packet(30, 0, 0, 0x30)]
        batch_cases = []
        expected_effects = defaultdict(int)
        expected_order_2b = [0] * 8
        for local_index, binding in enumerate(batch_bindings):
            order_id = binding["order_id"]
            source_type = binding["unit_id"]
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
                library[template_type], source, 0, x, y,
                previous_link, next_link))
            # configure_row intentionally produces inert state zero for
            # attack fixtures.  A live structure rests in command state one;
            # both original idle dispatchers pop the first deferred production
            # command only from that state.
            write_u32(row, 0x60, 1)
            base_offset = slot * SCENARIO_OBJECT_STRIDE
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            secondary_cost = rule_value(orders[order_id]["secondary_cost"], 0)
            packets.append(make_packet(
                COMMAND_FRAME, local_index + 1, 0, 0x0C,
                order_id, slot * SCENARIO_OBJECT_STRIDE,
                secondary_cost, 0, 0))
            deltas = [rule_value(rule, 1)
                      for rule in orders[order_id]["completion_effects"]]
            for unit_type in orders[order_id]["affected_type_ids"]:
                if unit_type >= 170:
                    continue
                for effect_index, delta in enumerate(deltas):
                    if delta:
                        expected_effects[(effect_index, 0, unit_type)] = signed32(
                            expected_effects[(effect_index, 0, unit_type)] + delta)
            if order_id == 0x2B:
                expected_order_2b[0] = signed32(
                    expected_order_2b[0] + deltas[11])
            batch_cases.append({
                "case_id": (
                    f"production_order_u{source_type:03d}_o{order_id:02d}"),
                "order_id": order_id,
                "order_name": orders[order_id]["name"],
                "source_slot": slot,
                "source_unit_id": source_type,
                "source_unit_name": source["name"],
                "used_exact_row_template": source_type in library,
                "primary_cost_variant0": rule_value(
                    orders[order_id]["primary_cost"], 0),
                "completion_deltas_variant1": deltas,
                "affected_type_ids": orders[order_id]["affected_type_ids"],
            })

        map_stem = f"(2) GP Production Order B{batch_index:02d}"
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
            "expected_completion_effects": [
                [effect, owner, unit_type, value]
                for (effect, owner, unit_type), value in
                sorted(expected_effects.items()) if value != 0
            ],
            "expected_order_2b_bonus": expected_order_2b,
        })
        print(f"batch={batch_index:02d} orders={len(batch_cases)}")

    manifest_path = tool_dir / "production_order_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"orders={len(player_order_ids)} "
          f"bindings={len(report['production_order_bindings'])} "
          f"batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
