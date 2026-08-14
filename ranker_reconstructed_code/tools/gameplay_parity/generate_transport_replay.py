#!/usr/bin/env python3
"""Generate an attached-passenger fixture and a deterministic unload replay."""

from __future__ import annotations

import argparse
from pathlib import Path

from generate_skill_replay import make_packet, write_u32
from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    SCENARIO_OBJECT_RECORD,
    SCENARIO_OBJECT_STRIDE,
    TrcArchive,
    append_trc_record,
    i32,
    replace_trc_record,
    u32,
    write_trc_archive,
)


def main() -> int:
    root_default = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=root_default)
    parser.add_argument("--output-stem", default="(2) GP Transport Unload")
    parser.add_argument("--carrier-map", default="(2) GP Skill A18 U052.trk")
    parser.add_argument("--carrier-slot", type=int, default=38)
    parser.add_argument("--passenger-map", default="11.trk")
    parser.add_argument("--passenger-template-slot", type=int, default=14)
    parser.add_argument("--passenger-slot", type=int, default=12)
    parser.add_argument("--command-frame", type=int, default=31)
    parser.add_argument("--end-frame", type=int, default=180)
    parser.add_argument("--base-replay", default="error1.ply")
    args = parser.parse_args()

    root = args.root.resolve()
    game = root / "RankerOCPV_Win"
    maps = game / "Maps"
    replays = game / "Replays"
    carrier_archive = TrcArchive(maps / args.carrier_map)
    passenger_archive = TrcArchive(maps / args.passenger_map)
    objects_record = carrier_archive.record(SCENARIO_OBJECT_RECORD)
    objects = bytearray(objects_record.data)
    passenger_objects = passenger_archive.record(SCENARIO_OBJECT_RECORD).data

    carrier_base = args.carrier_slot * SCENARIO_OBJECT_STRIDE
    passenger_base = args.passenger_slot * SCENARIO_OBJECT_STRIDE
    template_base = args.passenger_template_slot * SCENARIO_OBJECT_STRIDE
    carrier_type = u32(objects, carrier_base)
    if carrier_type != 52:
        raise ValueError(f"expected Phantom carrier type 52, got {carrier_type}")
    previous_ref = u32(objects, passenger_base + 0x1C8)
    next_ref = u32(objects, passenger_base + 0x1CC)
    objects[passenger_base:passenger_base + SCENARIO_OBJECT_STRIDE] = \
        passenger_objects[template_base:template_base + SCENARIO_OBJECT_STRIDE]
    write_u32(objects, passenger_base + 0x1C8, previous_ref)
    write_u32(objects, passenger_base + 0x1CC, next_ref)

    carrier_offset = args.carrier_slot * SCENARIO_OBJECT_STRIDE
    passenger_offset = args.passenger_slot * SCENARIO_OBJECT_STRIDE
    carrier_x = i32(objects, carrier_base + 0xB8)
    carrier_y = i32(objects, carrier_base + 0xBC)
    write_u32(objects, passenger_base + 0x04, 0)
    write_u32(objects, passenger_base + 0x08,
              u32(objects, passenger_base + 0x08) & 0x0F)
    write_u32(objects, passenger_base + 0x4C, 0)
    write_u32(objects, passenger_base + 0x60, 0x45)
    write_u32(objects, passenger_base + 0x68, carrier_offset)
    write_u32(objects, passenger_base + 0x74, 0)
    for offset in (0x84, 0x88, 0x8C, 0x90, 0xD8, 0xDC, 0xE0, 0xE4):
        write_u32(objects, passenger_base + offset, 0)
    write_u32(objects, passenger_base + 0x9C, 0)
    write_u32(objects, passenger_base + 0xA0, 0x80)
    for offset, value in ((0xB8, carrier_x), (0xBC, carrier_y),
                          (0xC0, carrier_x & ~0x1F),
                          (0xC4, carrier_y & ~0x1F),
                          (0xD0, carrier_x), (0xD4, carrier_y)):
        write_u32(objects, passenger_base + offset, value)
    write_u32(objects, passenger_base + 0x124, 0)
    write_u32(objects, carrier_base + 0x4C, 1)

    map_path = maps / f"{args.output_stem}.trk"
    map_records = list(carrier_archive.records)
    map_records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        objects_record, bytes(objects))
    write_trc_archive(map_path, map_records, carrier_archive.directory_slots)

    base_archive = TrcArchive(replays / args.base_replay)
    base_replay = next((record for record in base_archive.records
                        if record.name.casefold() == "replay"), None)
    if base_replay is None or len(base_replay.data) < REPLAY_HEADER_SIZE:
        raise ValueError("base replay has no valid Replay payload")
    replay_header = bytearray(base_replay.data[:REPLAY_HEADER_SIZE])
    replay_header[0x5F] = 0
    replay_header[0x87] = 5
    map_name = f"Maps\\{map_path.name}".encode("ascii")
    replay_header[0x1FB:0x1FB + 260] = b"\0" * 260
    replay_header[0x1FB:0x1FB + min(len(map_name), 259)] = map_name[:259]
    unload_x = carrier_x + 96
    unload_y = carrier_y + 64
    payload = bytes(replay_header)
    payload += make_packet(
        args.command_frame, 0, 0, 0x02, 0x24, passenger_offset,
        carrier_offset, unload_x, unload_y)
    payload += make_packet(args.end_frame, 1, 0, 0x13)
    replay_records = append_trc_record(map_records, "Replay", payload, 2)
    replay_path = replays / f"{args.output_stem}.ply"
    write_trc_archive(replay_path, replay_records,
                      carrier_archive.directory_slots)

    print(f"map={map_path}")
    print(f"replay={replay_path}")
    print(f"carrier={args.carrier_slot}:U{carrier_type} cargo=1")
    print(f"passenger={args.passenger_slot}:U{u32(objects, passenger_base)} "
          f"state=0x45 target=0x{carrier_offset:x}")
    print(f"unload={unload_x},{unload_y}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
