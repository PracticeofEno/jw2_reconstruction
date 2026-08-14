#!/usr/bin/env python3
"""Generate a player-operable diagnostic map and deterministic skill replay."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_ACTIVE_HEAD_OFFSET,
    SCENARIO_LIFECYCLE_HEAD_OFFSET,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    i32,
    replace_trc_record,
    u32,
    write_trc_archive,
)


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def make_packet(frame: int, sequence: int, owner: int, subtype: int,
                arg0: int = 0, unit_offset: int = 0, arg1: int = 0,
                arg2: int = 0, arg3: int = 0) -> bytes:
    packet = bytearray(0x24)
    struct.pack_into("<IIIIIIIII", packet, 0,
                     1, frame, sequence, (subtype << 24) | owner,
                     arg0, unit_offset, arg1, arg2, arg3)
    return bytes(packet)


def main() -> int:
    root_default = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=root_default)
    parser.add_argument("--template-map", required=True)
    parser.add_argument("--output-stem", required=True)
    parser.add_argument("--action", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--source-slot", type=lambda x: int(x, 0), default=38)
    parser.add_argument("--target-slot", type=lambda x: int(x, 0), default=12)
    parser.add_argument("--source-owner", type=lambda x: int(x, 0), default=0)
    parser.add_argument("--target-owner", type=lambda x: int(x, 0), default=1)
    parser.add_argument("--target", choices=(
        "unit", "self", "point", "none", "lifecycle"),
                        default="unit")
    parser.add_argument("--command-frame", type=lambda x: int(x, 0), default=31)
    parser.add_argument("--end-frame", type=lambda x: int(x, 0), default=600)
    parser.add_argument("--world-x", type=lambda x: int(x, 0))
    parser.add_argument("--world-y", type=lambda x: int(x, 0))
    parser.add_argument("--target-x", type=lambda x: int(x, 0))
    parser.add_argument("--target-y", type=lambda x: int(x, 0))
    parser.add_argument("--base-replay", default="error1.ply")
    args = parser.parse_args()

    root = args.root.resolve()
    game = root / "RankerOCPV_Win"
    template_path = game / "Maps" / args.template_map
    base_replay_path = game / "Replays" / args.base_replay
    map_path = game / "Maps" / f"{args.output_stem}.trk"
    replay_path = game / "Replays" / f"{args.output_stem}.ply"

    template = TrcArchive(template_path)
    header_record = template.record(0)
    header = bytearray(header_record.data)
    objects_record = template.record(SCENARIO_OBJECT_RECORD)
    objects = bytearray(objects_record.data)
    slot_count = len(objects) // SCENARIO_OBJECT_STRIDE
    if not (0 < args.source_slot < slot_count and
            0 < args.target_slot < slot_count):
        raise ValueError("source or target slot is outside OBC")
    source_base = args.source_slot * SCENARIO_OBJECT_STRIDE
    target_base = args.target_slot * SCENARIO_OBJECT_STRIDE
    source_type = u32(objects, source_base)
    target_type = u32(objects, target_base)
    write_u32(objects, source_base + 4, args.source_owner)
    write_u32(objects, target_base + 4, args.target_owner)
    if args.target_x is not None:
        for offset in (0xB8, 0xC0, 0xD0):
            write_u32(objects, target_base + offset, args.target_x)
    if args.target_y is not None:
        for offset in (0xBC, 0xC4, 0xD4):
            write_u32(objects, target_base + offset, args.target_y)

    if args.target == "lifecycle":
        previous_ref = u32(objects, target_base + 0x1C8)
        next_ref = u32(objects, target_base + 0x1CC)
        previous_slot = (previous_ref // SCENARIO_OBJECT_STRIDE
                         if previous_ref % SCENARIO_OBJECT_STRIDE == 0 else previous_ref)
        next_slot = (next_ref // SCENARIO_OBJECT_STRIDE
                     if next_ref % SCENARIO_OBJECT_STRIDE == 0 else next_ref)
        if previous_slot:
            write_u32(objects,
                      previous_slot * SCENARIO_OBJECT_STRIDE + 0x1CC, next_ref)
        elif (u32(header, SCENARIO_ACTIVE_HEAD_OFFSET) ==
              args.target_slot * SCENARIO_OBJECT_STRIDE):
            write_u32(header, SCENARIO_ACTIVE_HEAD_OFFSET, next_ref)
        if next_slot:
            write_u32(objects,
                      next_slot * SCENARIO_OBJECT_STRIDE + 0x1C8, previous_ref)
        lifecycle_head = u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET)
        write_u32(objects, target_base + 0x1C8, 0)
        write_u32(objects, target_base + 0x1CC, lifecycle_head)
        if lifecycle_head:
            lifecycle_slot = lifecycle_head // SCENARIO_OBJECT_STRIDE
            write_u32(objects,
                      lifecycle_slot * SCENARIO_OBJECT_STRIDE + 0x1C8,
                      args.target_slot * SCENARIO_OBJECT_STRIDE)
        write_u32(header, SCENARIO_LIFECYCLE_HEAD_OFFSET,
                  args.target_slot * SCENARIO_OBJECT_STRIDE)
        write_u32(objects, target_base + 0x18, 0)
        write_u32(objects, target_base + 0xA0,
                  u32(objects, target_base + 0xA0) | 4)

    map_records = list(template.records)
    map_records[0] = replace_trc_record(header_record, bytes(header))
    map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        objects_record, bytes(objects))
    write_trc_archive(map_path, map_records, template.directory_slots)

    base = TrcArchive(base_replay_path)
    base_replay = next((record for record in base.records
                        if record.name.casefold() == "replay"), None)
    if base_replay is None or len(base_replay.data) < REPLAY_HEADER_SIZE:
        raise ValueError("base replay has no valid Replay payload")
    replay_header = bytearray(base_replay.data[:REPLAY_HEADER_SIZE])
    replay_header[0x5F] = args.source_owner
    # Replay mode 5 preserves serialized owner-0..7 scenario units and does
    # not spawn the ordinary faction starting group over their pool slots.
    # That makes the declared caster/target in this diagnostic snapshot the
    # actual units addressed by the packet in both executables.
    replay_header[0x87] = 5
    map_name = f"Maps\\{map_path.name}".encode("ascii")
    replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
    replay_header[0x1FB:0x1FB + min(len(map_name), 259)] = map_name[:259]

    source_offset = args.source_slot * SCENARIO_OBJECT_STRIDE
    target_offset = args.target_slot * SCENARIO_OBJECT_STRIDE
    target_x = i32(objects, target_base + 0xB8)
    target_y = i32(objects, target_base + 0xBC)
    if args.world_x is not None:
        target_x = args.world_x
    if args.world_y is not None:
        target_y = args.world_y
    if args.target == "self":
        action_target = source_offset
    elif args.target in ("unit", "lifecycle"):
        action_target = target_offset
    else:
        action_target = 0
    if args.target == "none":
        target_x = 0
        target_y = 0
    replay_payload = bytes(replay_header)
    replay_payload += make_packet(
        args.command_frame, 0, args.source_owner, 0x09,
        args.action, source_offset, action_target, target_x, target_y)
    replay_payload += make_packet(
        args.end_frame, 1, args.source_owner, 0x13)
    replay_records = append_trc_record(map_records, "Replay", replay_payload, 2)
    write_trc_archive(replay_path, replay_records, template.directory_slots)

    print(f"map={map_path}")
    print(f"replay={replay_path}")
    print(f"action={args.action} source={args.source_slot}:{source_type}/P{args.source_owner} "
          f"target={args.target_slot}:{target_type}/P{args.target_owner} mode={args.target} "
          f"xy={target_x},{target_y}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
