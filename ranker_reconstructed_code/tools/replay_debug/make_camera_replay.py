#!/usr/bin/env python3
"""Clone a replay while changing only its embedded primary camera."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


PRIMARY_CAMERA_X_OFFSET = 0x1410
PRIMARY_CAMERA_Y_OFFSET = 0x1414
REPLAY_MAP_PATH_OFFSET = 0x1FB
REPLAY_MAP_PATH_BYTES = 260
REPLAY_HEADER_SIZE = 0x20FF
REPLAY_PACKET_SIZE = 0x24
PREVIOUS_VISIBILITY_LAYER_RECORD = 14
VISIBILITY_LAYER_STRIDE = 0x100


def camera_record(record, x: int, y: int, replace_trc_record):
    header = bytearray(record.data)
    required = PRIMARY_CAMERA_Y_OFFSET + 4
    if len(header) < required:
        raise ValueError(
            f"primary record is too short for camera fields: {len(header):#x}")
    struct.pack_into("<ii", header, PRIMARY_CAMERA_X_OFFSET, x, y)
    return replace_trc_record(record, bytes(header))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("x", type=int)
    parser.add_argument("y", type=int)
    parser.add_argument("--end-frame", type=int)
    parser.add_argument(
        "--brush-edge", nargs=3, type=int,
        metavar=("TILE_X", "TILE_Y", "BRUSH_ID"))
    args = parser.parse_args()

    tool_dir = Path(__file__).resolve().parent
    parity_dir = tool_dir.parent / "gameplay_parity"
    sys.path.insert(0, str(parity_dir))
    from inventory_gameplay_matrix import (  # pylint: disable=import-error
        TrcArchive,
        replace_trc_record,
        write_trc_archive,
    )

    source = args.source.resolve()
    output = args.output.resolve()
    archive = TrcArchive(source)
    records = list(archive.records)
    records[0] = camera_record(
        records[0], args.x, args.y, replace_trc_record)
    if args.brush_edge is not None:
        tile_x, tile_y, brush_id = args.brush_edge
        if not (0 <= tile_x < VISIBILITY_LAYER_STRIDE and
                0 <= tile_y < VISIBILITY_LAYER_STRIDE):
            raise ValueError("brush-edge tile must be inside the 256x256 grid")
        if not (1 <= brush_id <= 0xFF):
            raise ValueError("brush id must be inside 1..255")
        if PREVIOUS_VISIBILITY_LAYER_RECORD >= len(records):
            raise ValueError("source archive has no FOGBGI visibility layer")
        visibility = bytearray(records[PREVIOUS_VISIBILITY_LAYER_RECORD].data)
        offset = (tile_y * VISIBILITY_LAYER_STRIDE + tile_x) * 4
        if offset + 4 > len(visibility):
            raise ValueError("source BGI visibility layer is truncated")
        # A real owner-0 visible structure cell is 0x380c0000 | type.  The
        # remembered class-10 form keeps occupancy, owner and explored bits
        # while current visibility bit 27 is clear: 0x300c0000 | type.
        struct.pack_into("<I", visibility, offset, 0x300C0000 | brush_id)
        records[PREVIOUS_VISIBILITY_LAYER_RECORD] = replace_trc_record(
            records[PREVIOUS_VISIBILITY_LAYER_RECORD], bytes(visibility))

    replay_index = next((
        index for index, record in enumerate(records)
        if record.name.casefold() == "replay"), None)
    map_output = None
    if replay_index is not None:
        replay_payload = bytearray(records[replay_index].data)
        raw_map_path = replay_payload[
            REPLAY_MAP_PATH_OFFSET:
            REPLAY_MAP_PATH_OFFSET + REPLAY_MAP_PATH_BYTES].split(b"\0", 1)[0]
        map_relative = raw_map_path.decode("cp949").replace("\\", "/")
        game_root = source.parent.parent
        map_source = (game_root / map_relative).resolve()
        if map_source.is_file():
            map_output = (game_root / "Maps" /
                          f"{output.stem}.trk").resolve()
            map_archive = TrcArchive(map_source)
            map_records = list(map_archive.records)
            map_records[0] = camera_record(
                map_records[0], args.x, args.y, replace_trc_record)
            if args.brush_edge is not None:
                tile_x, tile_y, brush_id = args.brush_edge
                if PREVIOUS_VISIBILITY_LAYER_RECORD >= len(map_records):
                    raise ValueError("source map has no FOGBGI visibility layer")
                visibility = bytearray(
                    map_records[PREVIOUS_VISIBILITY_LAYER_RECORD].data)
                offset = (tile_y * VISIBILITY_LAYER_STRIDE + tile_x) * 4
                if offset + 4 > len(visibility):
                    raise ValueError("source map BGI visibility layer is truncated")
                struct.pack_into(
                    "<I", visibility, offset, 0x300C0000 | brush_id)
                map_records[PREVIOUS_VISIBILITY_LAYER_RECORD] = replace_trc_record(
                    map_records[PREVIOUS_VISIBILITY_LAYER_RECORD], bytes(visibility))
            write_trc_archive(
                map_output, map_records, map_archive.directory_slots)

            replacement = f"Maps\\{map_output.name}".encode("cp949")
            if len(replacement) >= REPLAY_MAP_PATH_BYTES:
                raise ValueError("replacement replay map path is too long")
            replay_payload[
                REPLAY_MAP_PATH_OFFSET:
                REPLAY_MAP_PATH_OFFSET + REPLAY_MAP_PATH_BYTES] = (
                    replacement + b"\0" *
                    (REPLAY_MAP_PATH_BYTES - len(replacement)))
        if args.end_frame is not None:
            if args.end_frame < 0:
                raise ValueError("end frame must be non-negative")
            packet_bytes = len(replay_payload) - REPLAY_HEADER_SIZE
            if packet_bytes < 0 or packet_bytes % REPLAY_PACKET_SIZE != 0:
                raise ValueError("Replay record has a malformed packet stream")
            terminal_offsets = [
                offset
                for offset in range(
                    REPLAY_HEADER_SIZE, len(replay_payload), REPLAY_PACKET_SIZE)
                if replay_payload[offset + 0x0F] == 0x13
            ]
            if not terminal_offsets:
                raise ValueError("Replay record has no terminal packet")
            struct.pack_into(
                "<I", replay_payload, terminal_offsets[-1] + 4,
                args.end_frame)
        records[replay_index] = replace_trc_record(
            records[replay_index], bytes(replay_payload))
    write_trc_archive(output, records, archive.directory_slots)
    print(f"CAMERA_REPLAY_WRITTEN source={source} output={output} "
          f"map={map_output} camera={args.x},{args.y} "
          f"end_frame={args.end_frame} brush_edge={args.brush_edge}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
