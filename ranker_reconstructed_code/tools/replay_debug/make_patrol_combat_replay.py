#!/usr/bin/env python3
"""Turn one move/patrol batch unit into a durable hostile patrol target."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys


PRIMARY_CAMERA_X_OFFSET = 0x1410
PRIMARY_CAMERA_Y_OFFSET = 0x1414
REPLAY_MAP_PATH_OFFSET = 0x1FB
REPLAY_MAP_PATH_BYTES = 260
SCENARIO_OBJECT_RECORD = 7
SCENARIO_OBJECT_STRIDE = 0x1D0


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-slot", type=int, default=40)
    parser.add_argument("--source-script-bits", type=lambda value: int(value, 0),
                        default=0x02)
    parser.add_argument("--enemy-slot", type=int, default=42)
    parser.add_argument("--enemy-owner", type=int, default=1)
    parser.add_argument("--enemy-health", type=int, default=1_000_000)
    parser.add_argument("--camera-x", type=int, default=0)
    parser.add_argument("--camera-y", type=int, default=0)
    parser.add_argument("--enemy-x", type=int)
    parser.add_argument("--enemy-y", type=int)
    args = parser.parse_args()

    tool_dir = Path(__file__).resolve().parent
    parity_dir = tool_dir.parent / "gameplay_parity"
    sys.path.insert(0, str(parity_dir))
    from inventory_gameplay_matrix import (  # pylint: disable=import-error
        REPLAY_HEADER_SIZE,
        TrcArchive,
        replace_trc_record,
        write_trc_archive,
    )

    source = args.source.resolve()
    output = args.output.resolve()
    archive = TrcArchive(source)
    records = list(archive.records)
    replay_index = next((
        index for index, record in enumerate(records)
        if record.name.casefold() == "replay"), None)
    if replay_index is None:
        raise ValueError("source archive has no Replay record")
    if SCENARIO_OBJECT_RECORD >= replay_index:
        raise ValueError("source archive has no scenario object record")
    if not 0 <= args.enemy_owner < 8:
        raise ValueError("enemy owner must be a player slot from 0 through 7")

    header = bytearray(records[0].data)
    objects = bytearray(records[SCENARIO_OBJECT_RECORD].data)
    source_base = args.source_slot * SCENARIO_OBJECT_STRIDE
    enemy_base = args.enemy_slot * SCENARIO_OBJECT_STRIDE
    if source_base + SCENARIO_OBJECT_STRIDE > len(objects):
        raise ValueError("source slot is outside the scenario object pool")
    if enemy_base + SCENARIO_OBJECT_STRIDE > len(objects):
        raise ValueError("enemy slot is outside the scenario object pool")
    source_type = struct.unpack_from("<I", objects, source_base)[0]
    enemy_type = struct.unpack_from("<I", objects, enemy_base)[0]
    write_u32(header, PRIMARY_CAMERA_X_OFFSET, args.camera_x)
    write_u32(header, PRIMARY_CAMERA_Y_OFFSET, args.camera_y)
    write_u32(objects, enemy_base + 0x04, args.enemy_owner)
    write_u32(objects, enemy_base + 0x10, args.enemy_health)
    write_u32(objects, enemy_base + 0x18, args.enemy_health)
    write_u32(objects, enemy_base + 0x60, 1)
    write_u32(objects, enemy_base + 0x68, 0)
    write_u32(objects, enemy_base + 0xA0, 1)
    write_u32(objects, source_base + 0xE8, args.source_script_bits)
    if (args.enemy_x is None) != (args.enemy_y is None):
        raise ValueError("specify both --enemy-x and --enemy-y")
    if args.enemy_x is not None:
        for offset in (0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4):
            value = args.enemy_x if offset % 8 == 0 else args.enemy_y
            write_u32(objects, enemy_base + offset, value)
    records[0] = replace_trc_record(records[0], bytes(header))
    records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        records[SCENARIO_OBJECT_RECORD], bytes(objects))

    game_root = source.parent.parent
    map_output = (game_root / "Maps" / f"{output.stem}.trk").resolve()
    write_trc_archive(
        map_output, records[:replay_index], archive.directory_slots)

    replay_payload = bytearray(records[replay_index].data)
    if len(replay_payload) < REPLAY_HEADER_SIZE:
        raise ValueError("Replay record is shorter than its fixed header")
    replacement = f"Maps\\{map_output.name}".encode("ascii")
    if len(replacement) >= REPLAY_MAP_PATH_BYTES:
        raise ValueError("replacement replay map path is too long")
    replay_payload[
        REPLAY_MAP_PATH_OFFSET:
        REPLAY_MAP_PATH_OFFSET + REPLAY_MAP_PATH_BYTES] = (
            replacement + b"\0" * (REPLAY_MAP_PATH_BYTES - len(replacement)))

    packet_stream = replay_payload[REPLAY_HEADER_SIZE:]
    if len(packet_stream) % 0x24 != 0:
        raise ValueError("Replay packet stream is not 36-byte aligned")
    enemy_reference = args.enemy_slot * SCENARIO_OBJECT_STRIDE
    packets: list[bytearray] = []
    removed = 0
    for offset in range(0, len(packet_stream), 0x24):
        packet = bytearray(packet_stream[offset:offset + 0x24])
        packed_opcode = struct.unpack_from("<I", packet, 0x0C)[0]
        subtype = (packed_opcode >> 24) & 0xFF
        unit_reference = struct.unpack_from("<I", packet, 0x14)[0]
        if subtype != 0x13 and unit_reference == enemy_reference:
            removed += 1
            continue
        write_u32(packet, 0x08, len(packets))
        packets.append(packet)
    if removed == 0:
        raise ValueError("enemy slot had no move/patrol packet to remove")
    replay_payload = bytearray(replay_payload[:REPLAY_HEADER_SIZE])
    replay_payload.extend(b"".join(packets))
    records[replay_index] = replace_trc_record(
        records[replay_index], bytes(replay_payload))
    write_trc_archive(output, records, archive.directory_slots)

    print(
        "PATROL_COMBAT_REPLAY_WRITTEN "
        f"source={source} output={output} map={map_output} "
        f"patrol_source_slot={args.source_slot} source_type={source_type} "
        f"source_script_bits=0x{args.source_script_bits:X} "
        f"enemy_slot={args.enemy_slot} enemy_type={enemy_type} "
        f"enemy_owner={args.enemy_owner} health={args.enemy_health} "
        f"removed_packets={removed} camera={args.camera_x},{args.camera_y}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
