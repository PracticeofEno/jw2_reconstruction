#!/usr/bin/env python3
"""Generate factorized fixtures for player commands not covered elsewhere."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from generate_attack_batch_replays import build_row_library, chain_slots, configure_row
from generate_skill_replay import make_packet, write_u32
from generate_unit_production_batch_replays import (
    MAP_BASE_VISIBILITY_RECORD,
    register_serialized_building_footprint,
)
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
FOLLOWUP_FRAME = 50
END_FRAME = 140
PAIR_BATCH_SIZE = 25
SINGLE_BATCH_SIZE = 51
TRANSFER_EFFECT_ID = 51
TRANSFER_TYPES_MAX = 0x60
DROPOFF_TYPE = 0x60


def pair_position(index: int) -> tuple[int, int, int, int]:
    x = 256 + (index % 5) * 600
    y = 256 + (index // 5) * 600
    return x, y, x + 32, y


def single_position(index: int) -> tuple[int, int]:
    return 160 + (index % 8) * 384, 160 + (index // 8) * 384


def equipment_slot(effect: dict) -> tuple[int, int]:
    if effect["category"] == 1:
        return 4, 1
    if effect["category"] == 2:
        return 5, 2
    return 0, 3


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    effects = report["player_equipment_effects"]
    transfer_types = [
        row["unit_id"] for row in report["units"]
        if row["unit_id"] < TRANSFER_TYPES_MAX and
        (row["initial_command_or_type_flags"] & 2) != 0
    ]
    workers = [
        row["unit_id"] for row in report["units"]
        if (row["initial_command_or_type_flags"] & (1 << 7)) != 0 and
        # State-entry 0x07 deliberately sends type 0x10 to reserved work
        # (original 0x004d0189), not to the worker harvest/return pipeline.
        row["unit_id"] != 0x10
    ]
    if TRANSFER_EFFECT_ID not in {row["effect_id"] for row in effects}:
        raise ValueError("representative transfer effect is absent")

    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    if len(slots) < SINGLE_BATCH_SIZE:
        raise ValueError(f"base map has only {len(slots)} active slots")
    replay_source = TrcArchive(game / "Replays" / "error1.ply")
    replay_record = next(record for record in replay_source.records
                         if record.name.casefold() == "replay")
    replay_header_template = replay_record.data[:REPLAY_HEADER_SIZE]
    fallback_types = {0: 1, 1: 113, 2: 48, 3: 77, 4: 79}

    effect_cases = []
    transfer_set = set(transfer_types)
    for effect in effects:
        allowed = [unit_id for unit_id in effect["allowed_mobile_unit_ids"]
                   if unit_id in transfer_set]
        if not allowed:
            raise ValueError(
                f"effect {effect['effect_id']} has no transfer-capable type")
        effect_cases.append({
            "case_id": f"equipment_effect_{effect['effect_id']:03d}",
            "operation": "equipment_transfer",
            "axis": "effect",
            "effect_id": effect["effect_id"],
            "effect_name": effect["name"],
            "effect_category": effect["category"],
            "effect_mode": effect["mode"],
            "source_unit_id": allowed[0],
            "target_unit_id": allowed[0],
        })
    transfer_axis_cases = []
    representative = transfer_types[0]
    for unit_id in transfer_types:
        transfer_axis_cases.append({
            "case_id": f"equipment_source_u{unit_id:03d}",
            "operation": "equipment_transfer",
            "axis": "source_type",
            "effect_id": TRANSFER_EFFECT_ID,
            "effect_name": "equipment_051",
            "effect_category": 0,
            "effect_mode": 0,
            "source_unit_id": unit_id,
            "target_unit_id": representative,
        })
        transfer_axis_cases.append({
            "case_id": f"equipment_target_u{unit_id:03d}",
            "operation": "equipment_transfer",
            "axis": "target_type",
            "effect_id": TRANSFER_EFFECT_ID,
            "effect_name": "equipment_051",
            "effect_category": 0,
            "effect_mode": 0,
            "source_unit_id": representative,
            "target_unit_id": unit_id,
        })

    food_transfer_cases = []
    for unit_id in transfer_types:
        food_transfer_cases.extend(({
            "case_id": f"food_source_u{unit_id:03d}",
            "operation": "food_transfer",
            "axis": "source_type",
            "source_unit_id": unit_id,
            "target_unit_id": representative,
        }, {
            "case_id": f"food_target_u{unit_id:03d}",
            "operation": "food_transfer",
            "axis": "target_type",
            "source_unit_id": representative,
            "target_unit_id": unit_id,
        }))

    balance_cases = []
    for unit_id in transfer_types:
        balance_cases.extend(({
            "case_id": f"balance_donor_u{unit_id:03d}",
            "operation": "balance_secondary",
            "axis": "donor_type",
            "source_unit_id": representative,
            "target_unit_id": unit_id,
        }, {
            "case_id": f"balance_recipient_u{unit_id:03d}",
            "operation": "balance_secondary",
            "axis": "recipient_type",
            "source_unit_id": unit_id,
            "target_unit_id": representative,
        }))

    return_food_cases = [{
        "case_id": f"return_food_u{unit_id:03d}",
        "operation": "return_food",
        "source_unit_id": unit_id,
        "target_unit_id": DROPOFF_TYPE,
    } for unit_id in range(TRANSFER_TYPES_MAX)]
    return_cargo_cases = [{
        "case_id": f"return_cargo_u{unit_id:03d}",
        "operation": "return_cargo",
        "source_unit_id": unit_id,
        "target_unit_id": DROPOFF_TYPE + (unit_id // 16) * 16,
    } for unit_id in workers]
    return_cargo_cases.append({
        "case_id": "reserved_work_u016",
        "operation": "reserved_work",
        "source_unit_id": 0x10,
        "target_unit_id": 0x70,
    })
    hold_cases = [{
        "case_id": f"hold_u{unit_id:03d}",
        "operation": "hold_position",
        "unit_id": unit_id,
    } for unit_id in sorted(units)]
    death_cases = [{
        "case_id": f"nested_death_u{unit_id:03d}",
        "operation": "nested_death_mark",
        "unit_id": unit_id,
    } for unit_id in sorted(units)]

    manifest = {
        "schema": 1,
        "command_frame": COMMAND_FRAME,
        "followup_frame": FOLLOWUP_FRAME,
        "end_frame": END_FRAME,
        "coverage": {
            "equipment_effects": len(effect_cases),
            "equipment_transfer_type_axes": len(transfer_axis_cases),
            "food_transfer_type_axes": len(food_transfer_cases),
            "balance_type_axes": len(balance_cases),
            "return_food_types": len(return_food_cases),
            "return_cargo_types": len(return_cargo_cases),
            "hold_types": len(hold_cases),
            "nested_death_types": len(death_cases),
        },
        "batches": [],
    }

    def configured_row(unit_id: int, owner: int, x: int, y: int,
                       previous: int, following: int) -> bytearray:
        unit = units[unit_id]
        template_id = unit_id if unit_id in library else fallback_types[
            unit["movement_or_render_class"]]
        row = bytearray(configure_row(
            library[template_id], unit, owner, x, y, previous, following))
        if unit_id < TRANSFER_TYPES_MAX:
            for offset in range(0x30, 0x48, 4):
                write_u32(row, offset, 0)
        else:
            write_u32(row, 0x30, 0)
        return row

    def write_batch(label: str, batch_number: int, cases: list[dict],
                    paired: bool) -> None:
        used_count = len(cases) * (2 if paired else 1)
        # Contact-return cases finish so quickly that an eight-object replay
        # can pass frame 140 before the paired live-state sampler establishes
        # its first baseline.  Inert mobile rows provide normal simulation
        # pacing without affecting the command cases.
        if label == "ReturnCargo":
            used_count = max(used_count, 50)
        selected_slots = slots[:used_count]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        visibility = bytearray(
            len(base.record(MAP_BASE_VISIBILITY_RECORD).data))
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        packets = []
        batch_cases = []
        flat_index = 0
        for case_index, original_case in enumerate(cases):
            case = dict(original_case)
            if paired:
                source_slot = selected_slots[flat_index]
                target_slot = selected_slots[flat_index + 1]
                source_x, source_y, target_x, target_y = pair_position(case_index)
                # The balance command is a contact interaction.  Keeping the
                # donor and recipient on adjacent tile centers leaves the
                # widest interaction bounds in approach state and does not
                # exercise the value-transfer branch for every unit type.
                if case["operation"] == "balance_secondary":
                    target_x, target_y = source_x, source_y
                elif case["operation"] == "food_transfer":
                    # Type 0x2f is a valid transfer-capable catalog unit but
                    # has no useful approach speed.  Contact placement makes
                    # the slot-zero food transfer execute for that source
                    # instead of remaining forever in state 0x0e.
                    target_x, target_y = source_x, source_y
                elif case["operation"] in ("return_cargo", "reserved_work"):
                    # Put the worker on the adjacent tile while its interaction
                    # rectangle already touches the dropoff's close bounds.
                    # Put the building exactly on the next tile boundary and
                    # the worker ten pixels before it; this avoids placing the
                    # mobile unit in registered BGI occupancy.
                    target_x = ((source_x >> 5) + 1) << 5
                    source_x = target_x - 10
                    target_y = source_y
                source_ref = source_slot * SCENARIO_OBJECT_STRIDE
                target_ref = target_slot * SCENARIO_OBJECT_STRIDE
                source_row = configured_row(
                    case["source_unit_id"], 0, source_x, source_y,
                    (0 if flat_index == 0 else
                     selected_slots[flat_index - 1] * SCENARIO_OBJECT_STRIDE),
                    target_ref)
                target_row = configured_row(
                    case["target_unit_id"], 0, target_x, target_y,
                    source_ref,
                    (0 if flat_index + 2 == used_count else
                     selected_slots[flat_index + 2] * SCENARIO_OBJECT_STRIDE))
                write_u32(source_row, 0x94, source_ref)
                write_u32(target_row, 0x94, target_ref)
                operation = case["operation"]
                if operation == "equipment_transfer":
                    effect = next(row for row in effects
                                  if row["effect_id"] == case["effect_id"])
                    slot_index, slot_code = equipment_slot(effect)
                    write_u32(source_row, 0x30 + slot_index * 4,
                              case["effect_id"])
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x03,
                        case["effect_id"], source_ref))
                    packets.append(make_packet(
                        FOLLOWUP_FRAME, len(packets), 0, 0x02,
                        0x02, source_ref, target_ref, slot_code, target_y))
                    case["slot_code"] = slot_code
                    case["source_equipment_slot"] = slot_index
                    case["expected_immobile_approach"] = (
                        units[case["source_unit_id"]]["movement_step_limit"] == 0)
                elif operation == "food_transfer":
                    write_u32(source_row, 0x2C, 100)
                    write_u32(target_row, 0x2C, 0)
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x02,
                        0x02, source_ref, target_ref, 0, target_y))
                elif operation == "balance_secondary":
                    # Packet source is the recipient and packet target is the
                    # donor; the synchronized threshold is 50.
                    write_u32(source_row, 0x2C, 0)
                    write_u32(target_row, 0x2C, 100)
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x02,
                        0x23, source_ref, target_ref, 50, target_y))
                elif operation == "return_food":
                    write_u32(source_row, 0x2C, 100)
                    encoded_x = (target_x & 0xFFFFFF00) | 1
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x02,
                        0x02, source_ref, target_ref, encoded_x, target_y))
                    case["encoded_center_x"] = encoded_x
                elif operation in ("return_cargo", "reserved_work"):
                    write_u32(source_row, 0x4C, 10)
                    write_u32(source_row, 0x9C, 4)
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x02,
                        0x07, source_ref, 0x80000000, target_x, target_y))
                else:
                    raise ValueError(f"unsupported pair operation {operation}")
                for slot, row in ((source_slot, source_row),
                                  (target_slot, target_row)):
                    offset = slot * SCENARIO_OBJECT_STRIDE
                    objects[offset:offset + SCENARIO_OBJECT_STRIDE] = row
                register_serialized_building_footprint(
                    visibility, units[case["source_unit_id"]], 0,
                    source_x, source_y)
                register_serialized_building_footprint(
                    visibility, units[case["target_unit_id"]], 0,
                    target_x, target_y)
                case["source_slot"] = source_slot
                case["target_slot"] = target_slot
                case["source_unit_name"] = units[case["source_unit_id"]]["name"]
                case["target_unit_name"] = units[case["target_unit_id"]]["name"]
                flat_index += 2
            else:
                slot = selected_slots[flat_index]
                x, y = single_position(case_index)
                ref = slot * SCENARIO_OBJECT_STRIDE
                row = configured_row(
                    case["unit_id"], 0, x, y,
                    (0 if flat_index == 0 else
                     selected_slots[flat_index - 1] * SCENARIO_OBJECT_STRIDE),
                    (0 if flat_index + 1 == used_count else
                     selected_slots[flat_index + 1] * SCENARIO_OBJECT_STRIDE))
                write_u32(row, 0x94, ref)
                offset = slot * SCENARIO_OBJECT_STRIDE
                objects[offset:offset + SCENARIO_OBJECT_STRIDE] = row
                register_serialized_building_footprint(
                    visibility, units[case["unit_id"]], 0, x, y)
                if case["operation"] == "hold_position":
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x0A,
                        0x21, ref, 0, x, y))
                elif case["operation"] == "nested_death_mark":
                    packets.append(make_packet(
                        COMMAND_FRAME, len(packets), 0, 0x07,
                        0x1C, ref, 0, x, y))
                else:
                    raise ValueError(f"unsupported unit operation {case['operation']}")
                case["unit_slot"] = slot
                case["unit_name"] = units[case["unit_id"]]["name"]
                flat_index += 1
            batch_cases.append(case)

        while flat_index < used_count:
            slot = selected_slots[flat_index]
            x, y = single_position(flat_index)
            row = configured_row(
                1, 0, x, y,
                (0 if flat_index == 0 else
                 selected_slots[flat_index - 1] * SCENARIO_OBJECT_STRIDE),
                (0 if flat_index + 1 == used_count else
                 selected_slots[flat_index + 1] * SCENARIO_OBJECT_STRIDE))
            write_u32(row, 0x94, slot * SCENARIO_OBJECT_STRIDE)
            offset = slot * SCENARIO_OBJECT_STRIDE
            objects[offset:offset + SCENARIO_OBJECT_STRIDE] = row
            flat_index += 1

        stem = f"(2) GP Direct {label} B{batch_number:02d}"
        map_path = game / "Maps" / f"{stem}.trk"
        replay_path = game / "Replays" / f"{stem}.ply"
        records = list(base.records)
        records[0] = replace_trc_record(base.record(0), bytes(header))
        records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
            base.record(SCENARIO_OBJECT_RECORD), bytes(objects))
        records[MAP_BASE_VISIBILITY_RECORD] = replace_trc_record(
            base.record(MAP_BASE_VISIBILITY_RECORD),
            bytes(visibility))
        write_trc_archive(map_path, records, base.directory_slots)
        replay_header = bytearray(replay_header_template)
        replay_header[0x5F] = 0
        replay_header[0x87] = 5
        map_name = f"Maps\\{map_path.name}".encode("ascii")
        replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
        replay_header[0x1FB:0x1FB + len(map_name)] = map_name
        payload = bytes(replay_header) + b"".join(packets)
        payload += make_packet(END_FRAME, len(packets), 0, 0x13)
        replay_records = append_trc_record(records, "Replay", payload, 2)
        write_trc_archive(replay_path, replay_records, base.directory_slots)
        manifest["batches"].append({
            "batch_index": len(manifest["batches"]),
            "label": label,
            "map": map_path.relative_to(root).as_posix(),
            "replay": replay_path.relative_to(root).as_posix(),
            "replay_sha256": hashlib.sha256(
                replay_path.read_bytes()).hexdigest().upper(),
            "cases": batch_cases,
        })
        print(f"batch={len(manifest['batches']) - 1:02d} "
              f"label={label} cases={len(batch_cases)}")

    pair_groups = (
        ("EquipEffect", effect_cases),
        ("EquipAxis", transfer_axis_cases),
        ("FoodTransfer", food_transfer_cases),
        ("Balance", balance_cases),
        ("ReturnFood", return_food_cases),
        ("ReturnCargo", return_cargo_cases),
    )
    for label, cases in pair_groups:
        for batch_number, start in enumerate(range(0, len(cases), PAIR_BATCH_SIZE)):
            write_batch(label, batch_number,
                        cases[start:start + PAIR_BATCH_SIZE], True)
    for label, cases in (("Hold", hold_cases), ("Death", death_cases)):
        for batch_number, start in enumerate(range(0, len(cases), SINGLE_BATCH_SIZE)):
            write_batch(label, batch_number,
                        cases[start:start + SINGLE_BATCH_SIZE], False)

    manifest_path = tool_dir / "direct_command_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"batches={len(manifest['batches'])} "
          f"cases={sum(len(row['cases']) for row in manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
