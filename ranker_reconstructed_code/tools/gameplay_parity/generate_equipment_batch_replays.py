#!/usr/bin/env python3
"""Generate deterministic subtype-03 fixtures for every player equipment effect."""

from __future__ import annotations

import hashlib
import json
import struct
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
END_FRAME = 100


def observable_fields(effect: dict) -> list[str]:
    fields = []
    if effect["replacement_type_id"] != 0xFFFFFFFF:
        fields.append("type")
    for catalog_field, runtime_field in (
        ("max_health_delta", "max_health"),
        ("max_secondary_delta", "max_secondary"),
        ("health_delta", "health"),
        ("secondary_delta", "secondary"),
        ("offense_delta", "runtime_stat_1c"),
        ("defense_delta", "runtime_stat_20"),
        ("stat28_delta", "runtime_stat_28"),
        ("experience_delta", "elite_progress"),
        ("level_delta", "production_variant"),
    ):
        if effect[catalog_field] != 0:
            fields.append(runtime_field)
    if effect["level_delta"] != 0:
        fields.append("status_timer")
    if effect["owner_resource_delta"] != 0:
        fields.append("owner_resource")
    if effect["command_value_delta"] != 0:
        fields.append("command_value")
    if effect["generic_modifiers"][6] > 0:
        fields.append("command_flags")
    return fields


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root = tool_dir.parents[2]
    game = root / "RankerOCPV_Win"
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    units = {row["unit_id"]: row for row in report["units"]}
    effects = report["player_equipment_effects"]
    library = build_row_library(game / "Maps")
    base = TrcArchive(game / "Maps" / "(2) OBC Batch A.trk")
    base_header = base.record(0).data
    base_objects = base.record(SCENARIO_OBJECT_RECORD).data
    slots = chain_slots(base_header, base_objects, SCENARIO_ACTIVE_HEAD_OFFSET)
    if not slots:
        raise ValueError("base map has no active slots")
    bindings = [
        (effect, unit_id)
        for effect in effects
        for unit_id in effect["allowed_mobile_unit_ids"]
    ]
    batch_size = len(slots)

    fallback_types = {0: 1, 1: 75, 2: 48, 3: 77, 4: 79}
    base_replay_archive = TrcArchive(game / "Replays" / "error1.ply")
    base_replay = next(record for record in base_replay_archive.records
                       if record.name.casefold() == "replay")
    replay_header_template = base_replay.data[:REPLAY_HEADER_SIZE]
    manifest = {
        "schema": 1,
        "effect_count": len(effects),
        "binding_count": len(bindings),
        "batch_size": batch_size,
        "command_frame": COMMAND_FRAME,
        "end_frame": END_FRAME,
        "batches": [],
    }

    for batch_index, start in enumerate(range(0, len(bindings), batch_size)):
        batch_bindings = bindings[start:start + batch_size]
        selected_slots = slots[:len(batch_bindings)]
        header = bytearray(base_header)
        objects = bytearray(base_objects)
        write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET,
                  selected_slots[0] * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET, 0)
        packets = []
        batch_cases = []

        for local_index, (effect, source_type) in enumerate(batch_bindings):
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
            # Mobile units use raw +0x30..+0x44 for all six equipment slots.
            # Direct subtype-03 application is isolated from preloaded items.
            for offset in range(0x30, 0x48, 4):
                write_u32(row, offset, 0)
            objects[base_offset:base_offset + SCENARIO_OBJECT_STRIDE] = row
            packets.append(make_packet(
                COMMAND_FRAME, local_index, 0, 0x03,
                effect["effect_id"], slot * SCENARIO_OBJECT_STRIDE))
            batch_cases.append({
                "case_id": (f"equipment_apply_{effect['effect_id']:03d}_"
                            f"u{source_type:03d}"),
                "effect_id": effect["effect_id"],
                "effect_name": effect["name"],
                "category": effect["category"],
                "mode": effect["mode"],
                "source_slot": slot,
                "source_unit_id": source_type,
                "source_unit_name": source["name"],
                "used_exact_row_template": source_type in library,
                "initial": {
                    "type": source_type,
                    "health": max(1, source["max_health"]),
                    "max_health": max(1, source["max_health"]),
                    "max_secondary": max(0, source["max_secondary"]),
                    "secondary": max(0, source["max_secondary"]),
                    "runtime_stat_1c": max(0, source["offense"]),
                    "runtime_stat_20": max(0, source["defense"]),
                    "runtime_stat_28": 0,
                    "elite_progress": 0,
                    "production_variant": 0,
                    "status_timer": 0,
                    "command_flags": 0,
                },
                "observable_fields": observable_fields(effect),
                "attachment_definition_id": effect["attachment_definition_id"],
                "generic_modifiers": effect["generic_modifiers"],
            })

        map_stem = f"(2) GP Equip Apply B{batch_index:02d}"
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
        print(f"batch={batch_index:02d} effects={len(batch_cases)} "
              f"replay={replay_path.name}")

    manifest_path = tool_dir / "equipment_apply_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(f"manifest={manifest_path}")
    print(f"effects={len(effects)} bindings={len(bindings)} "
          f"batches={len(manifest['batches'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
