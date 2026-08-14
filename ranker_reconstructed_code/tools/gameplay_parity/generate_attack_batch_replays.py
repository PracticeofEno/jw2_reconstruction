#!/usr/bin/env python3
"""Generate batched player-owned attacks against every render class."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

from generate_skill_replay import make_packet, write_u32
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    pool_reference_to_slot,
    replace_trc_record,
    u32,
    write_trc_archive,
)


BATCH_SIZE = 8
TARGET_TYPES = {0: 75, 1: 164, 2: 48, 3: 77, 4: 79}
COMMAND_FRAME = 31
END_FRAME = 180
SOURCE_POSITIONS = (
    (508, 992),
    (1360, 360),
    (2360, 360),
    (360, 1360),
    (1360, 1360),
    (2360, 1360),
    (360, 2360),
    (1360, 2360),
)


def chain_slots(header: bytes, objects: bytes, head_offset: int) -> list[int]:
    result: list[int] = []
    seen: set[int] = set()
    slot = pool_reference_to_slot(u32(header, head_offset))
    count = len(objects) // SCENARIO_OBJECT_STRIDE
    while slot and slot < count and slot not in seen:
        seen.add(slot)
        result.append(slot)
        slot = pool_reference_to_slot(u32(
            objects, slot * SCENARIO_OBJECT_STRIDE + 0x1CC))
    return result


def active_rows(path: Path) -> list[tuple[int, bytes]]:
    archive = TrcArchive(path)
    header = archive.record(0).data
    objects = archive.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(header, objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    slots += chain_slots(header, objects, SCENARIO_LIFECYCLE_HEAD_OFFSET)
    return [
        (u32(objects, slot * SCENARIO_OBJECT_STRIDE),
         objects[slot * SCENARIO_OBJECT_STRIDE:
                 (slot + 1) * SCENARIO_OBJECT_STRIDE])
        for slot in slots
    ]


def build_row_library(map_directory: Path) -> dict[int, bytes]:
    library: dict[int, bytes] = {}
    for path in sorted(map_directory.glob("*.trk")):
        if path.name.startswith("(2) GP Attack "):
            continue
        try:
            for unit_id, row in active_rows(path):
                if unit_id not in library and u32(row, 0x18) != 0:
                    library[unit_id] = row
        except (KeyError, OSError, ValueError, struct.error):
            continue
    return library


def spread_building_target_slots(header: bytes, objects: bytes,
                                 units: dict[int, dict], count: int) -> list[int]:
    candidates = [
        slot for slot in chain_slots(header, objects, SCENARIO_ACTIVE_HEAD_OFFSET)
        if (u32(objects, slot * SCENARIO_OBJECT_STRIDE) == TARGET_TYPES[1]
            and units[u32(objects, slot * SCENARIO_OBJECT_STRIDE)]
            ["movement_or_render_class"] == 1)
    ]
    if len(candidates) < count:
        raise ValueError(f"building fixture has {len(candidates)} class-1 targets")
    selected = [candidates[0]]
    while len(selected) < count:
        def separation(slot: int) -> int:
            base = slot * SCENARIO_OBJECT_STRIDE
            x = u32(objects, base + 0xB8)
            y = u32(objects, base + 0xBC)
            return min(
                (x - u32(objects, other * SCENARIO_OBJECT_STRIDE + 0xB8)) ** 2 +
                (y - u32(objects, other * SCENARIO_OBJECT_STRIDE + 0xBC)) ** 2
                for other in selected)
        selected.append(max(
            (slot for slot in candidates if slot not in selected),
            key=lambda slot: (separation(slot), -slot)))
    return selected


def configure_row(template: bytes, unit: dict, owner: int, x: int, y: int,
                  previous_link: int, next_link: int,
                  durable_target: bool = False) -> bytes:
    row = bytearray(template)
    unit_id = unit["unit_id"]
    write_u32(row, 0x00, unit_id)
    write_u32(row, 0x04, owner)
    write_u32(row, 0x08, 0)
    write_u32(row, 0x0C, 0)
    health = 1_000_000 if durable_target else max(1, unit["max_health"])
    secondary = max(0, unit["max_secondary"])
    write_u32(row, 0x10, health)
    write_u32(row, 0x14, secondary)
    write_u32(row, 0x18, health)
    write_u32(row, 0x1C, max(0, unit["offense"]))
    write_u32(row, 0x20, max(0, unit["defense"]))
    write_u32(row, 0x24, secondary)
    write_u32(row, 0x28, 0)
    write_u32(row, 0x2C, 0)
    # Explicit attack packets are the subject of this fixture.  Suppress idle
    # auto-acquisition so neither side starts an unrecorded target-class case
    # before its scheduled command or between rounds.
    write_u32(row, 0x58, unit["initial_command_or_type_flags"] & ~0x20)
    for offset in (0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74,
                   0x78, 0x7C, 0x84, 0x88, 0x8C, 0x90, 0x9C, 0xA4,
                   0xB0, 0xB4, 0xD8, 0xDC, 0xE0, 0xE4, 0xE8, 0xEC,
                   0xF0, 0x124, 0x128, 0x12C, 0x130, 0x134):
        write_u32(row, offset, 0)
    write_u32(row, 0xA0, 1)
    for offset, value in (
            (0xB8, x), (0xBC, y), (0xC0, x), (0xC4, y),
            (0xC8, x), (0xCC, y), (0xD0, x), (0xD4, y)):
        write_u32(row, offset, value)
    write_u32(row, 0x1C8, previous_link)
    write_u32(row, 0x1CC, next_link)
    return bytes(row)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    # Profile row zero is a real BuildMan hit definition, not merely a null
    # sentinel.  Include every explicit-attack-capable type even when its
    # primary profile id is zero, as well as automatic/NPC attack profiles.
    # Keep the established nonzero-profile order stable so adding the valid
    # zero-index profile does not move every previously proven source to a new
    # terrain coordinate.  Append explicit profile-zero attackers as their own
    # final batch.
    nonzero_profile_sources = sorted({
        row["unit_id"] for row in report["attack_bindings"]
        if row["attack_id"] != 0
    })
    profile_zero_sources = sorted(
        row["unit_id"] for row in report["units"]
        if row["attack_profile"] == 0 and
        row["initial_command_or_type_flags"] & (1 << 5))
    source_ids = nonzero_profile_sources + [
        unit_id for unit_id in profile_zero_sources
        if unit_id not in nonzero_profile_sources
    ]
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    building_base = TrcArchive(game / "Maps" / "(4) Far Away v1.2.trk")
    building_header = building_base.record(0).data
    building_objects = building_base.record(SCENARIO_OBJECT_RECORD).data
    building_slots = chain_slots(
        building_header, building_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    building_targets = spread_building_target_slots(
        building_header, building_objects, units, BATCH_SIZE)
    needed = BATCH_SIZE * 2
    if len(slots) < needed:
        raise ValueError(f"base map has {len(slots)} active slots, need {needed}")

    fallback_types = {0: 1, 1: 113, 2: 48, 3: 77, 4: 79}
    missing_templates = sorted(unit_id for unit_id in source_ids
                               if unit_id not in library)
    base_replay_archive = TrcArchive(game / "Replays" / "error1.ply")
    base_replay = next(record for record in base_replay_archive.records
                       if record.name.casefold() == "replay")
    replay_header_template = base_replay.data[:REPLAY_HEADER_SIZE]
    manifest = {
        "schema": 1,
        "source_unit_count": len(source_ids),
        "target_render_classes": TARGET_TYPES,
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "missing_exact_row_templates": missing_templates,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(source_ids), BATCH_SIZE)):
        batch_sources = source_ids[start:start + BATCH_SIZE]
        for render_class, target_type in TARGET_TYPES.items():
            fixture = building_base if render_class == 1 else base
            header = bytearray(
                building_header if render_class == 1 else base_header)
            objects = bytearray(
                building_objects if render_class == 1 else base_objects)
            if render_class == 1:
                target_slots = building_targets[:len(batch_sources)]
                source_slots = [
                    slot for slot in building_slots if slot not in target_slots
                    and units[u32(building_objects,
                                  slot * SCENARIO_OBJECT_STRIDE)]
                    ["movement_or_render_class"] != 1
                ][:len(batch_sources)]
                if len(source_slots) < len(batch_sources):
                    raise ValueError(
                        "building fixture lacks enough non-building source slots")
                selected_slots = source_slots + target_slots
            else:
                selected_slots = slots[:len(batch_sources) * 2]
                source_slots = selected_slots[:len(batch_sources)]
                target_slots = selected_slots[len(batch_sources):]
            write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                      selected_slots[0] * SCENARIO_OBJECT_STRIDE)
            write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
            batch_cases = []
            sequence = 0
            packets: list[bytes] = []
            for local_index, unit_id in enumerate(batch_sources):
                source_slot = source_slots[local_index]
                target_slot = target_slots[local_index]
                source_base = source_slot * SCENARIO_OBJECT_STRIDE
                target_base = target_slot * SCENARIO_OBJECT_STRIDE
                unit = units[unit_id]
                if unit["attack_profile"] != 0:
                    attack_distance = 96
                elif render_class == 1:
                    # Existing class-1 occupancy is retained from the map.
                    # Most sources must stay outside that footprint; the Null
                    # and Velociraptor rows need the closer edge to avoid a
                    # terrain-step prerequisite.
                    attack_distance = 64 if unit_id in (47, 69) else 96
                else:
                    # Profile zero is a direct melee hit.  Keep the target
                    # inside its minimum 35-unit range so this matrix tests the
                    # hit rather than the fixture cell's movement terrain.
                    attack_distance = 32
                source_template = library.get(unit_id)
                if source_template is None:
                    source_template = library[fallback_types[
                        unit["movement_or_render_class"]]]
                if render_class == 1:
                    target_x = u32(objects, target_base + 0xB8)
                    target_y = u32(objects, target_base + 0xBC)
                    x = (target_x - attack_distance
                         if target_x >= attack_distance + 32
                         else target_x + attack_distance)
                    y = target_y
                else:
                    x, y = SOURCE_POSITIONS[local_index]
                    target_x = x + attack_distance
                    target_y = y

                source_position = local_index
                source_previous = (0 if source_position == 0 else
                    selected_slots[source_position - 1] * SCENARIO_OBJECT_STRIDE)
                source_next = (selected_slots[source_position + 1] *
                    SCENARIO_OBJECT_STRIDE)
                source_row = configure_row(
                    source_template, unit, 0, x, y,
                    source_previous, source_next)
                objects[source_base:source_base + SCENARIO_OBJECT_STRIDE] = source_row

                target_position = len(batch_sources) + local_index
                target_previous = (selected_slots[target_position - 1] *
                                   SCENARIO_OBJECT_STRIDE)
                target_next = (0 if target_position + 1 == len(selected_slots)
                               else selected_slots[target_position + 1] *
                               SCENARIO_OBJECT_STRIDE)
                target_row = configure_row(
                    (bytes(objects[target_base:
                                   target_base + SCENARIO_OBJECT_STRIDE])
                     if render_class == 1 else library[target_type]),
                    units[target_type], 1,
                    target_x, target_y,
                    target_previous, target_next, durable_target=True)
                target_row = bytearray(target_row)
                # Target representatives must not kill or displace the
                # attacker while later render-class rounds are pending.  Zero
                # defense also makes every accepted direct profile observable
                # on its first hit instead of depending on the low-damage RNG.
                write_u32(target_row, 0x1C, 0)
                write_u32(target_row, 0x20, 0)
                objects[target_base:target_base + SCENARIO_OBJECT_STRIDE] = target_row
                target = {
                    "slot": target_slot,
                    "unit_id": target_type,
                    "initial_health": 1_000_000,
                }
                packets.append(make_packet(
                    COMMAND_FRAME, sequence, 0, 0x02, 5,
                    source_slot * SCENARIO_OBJECT_STRIDE,
                    target_slot * SCENARIO_OBJECT_STRIDE,
                    target_x, target_y))
                sequence += 1
                batch_cases.append({
                    "unit_id": unit_id,
                    "unit_name": unit["name"],
                    "source_slot": source_slot,
                    "target": target,
                    "used_exact_row_template": unit_id in library,
                })

            map_stem = (f"(2) GP Attack B{batch_index:02d} "
                        f"C{render_class}")
            map_path = game / "Maps" / f"{map_stem}.trk"
            replay_path = game / "Replays" / f"{map_stem}.ply"
            map_records = list(fixture.records)
            map_records[0] = replace_trc_record(fixture.record(0), bytes(header))
            map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
                fixture.record(SCENARIO_OBJECT_RECORD), bytes(objects))
            write_trc_archive(map_path, map_records, fixture.directory_slots)

            replay_header = bytearray(replay_header_template)
            replay_header[0x5F] = 0
            replay_header[0x87] = 5
            map_name = f"Maps\\{map_path.name}".encode("ascii")
            replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
            replay_header[0x1FB:0x1FB + len(map_name)] = map_name
            packets.sort(key=lambda packet: (
                struct.unpack_from("<I", packet, 4)[0],
                struct.unpack_from("<I", packet, 8)[0]))
            replay_payload = bytes(replay_header) + b"".join(packets)
            replay_payload += make_packet(END_FRAME, sequence, 0, 0x13)
            replay_records = append_trc_record(
                map_records, "Replay", replay_payload, 2)
            write_trc_archive(
                replay_path, replay_records, fixture.directory_slots)
            manifest["batches"].append({
                "batch_index": batch_index,
                "render_class": render_class,
                "map": map_path.relative_to(root).as_posix(),
                "replay": replay_path.relative_to(root).as_posix(),
                "replay_sha256": hashlib.sha256(
                    replay_path.read_bytes()).hexdigest().upper(),
                "cases": batch_cases,
            })
            print(f"batch={batch_index:02d} class={render_class} "
                  f"sources={len(batch_sources)} replay={replay_path.name}")

    manifest_path = tool_dir / "attack_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"exact_templates={len(source_ids) - len(missing_templates)} "
          f"catalog_initialized_templates={len(missing_templates)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
