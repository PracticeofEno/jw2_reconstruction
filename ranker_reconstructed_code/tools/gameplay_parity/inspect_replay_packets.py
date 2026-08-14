#!/usr/bin/env python3
"""Print decoded 36-byte gameplay packets from a Ranker replay archive."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from inventory_gameplay_matrix import REPLAY_HEADER_SIZE, TrcArchive


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("replay", type=Path)
    parser.add_argument("--subtype", type=parse_int, action="append")
    parser.add_argument("--command", type=parse_int, action="append")
    parser.add_argument("--queued", action="store_true")
    parser.add_argument("--frame-from", type=parse_int)
    parser.add_argument("--frame-to", type=parse_int)
    parser.add_argument("--limit", type=parse_int, default=200)
    args = parser.parse_args()

    archive = TrcArchive(args.replay)
    replay = next((record for record in archive.records
                   if record.name.casefold() == "replay"), None)
    if replay is None:
        raise ValueError(f"Replay record missing from {args.replay}")
    payload = replay.data[REPLAY_HEADER_SIZE:]
    packet_count = len(payload) // 0x24
    shown = 0
    for index in range(packet_count):
        words = struct.unpack_from("<IIIIIIIII", payload, index * 0x24)
        marker, frame, sequence, packed, command, unit_offset, arg0, arg1, arg2 = words
        subtype = (packed >> 24) & 0xFF
        owner = packed & 0xFF
        if args.subtype is not None and subtype not in args.subtype:
            continue
        if args.command is not None and command not in args.command:
            continue
        if args.queued and (command & 0x80000000) == 0:
            continue
        if args.frame_from is not None and frame < args.frame_from:
            continue
        if args.frame_to is not None and frame > args.frame_to:
            continue
        print(
            f"index={index} frame={frame} sequence={sequence} "
            f"owner={owner} subtype=0x{subtype:02x} command=0x{command:08x} "
            f"unit_offset=0x{unit_offset:08x} arg0=0x{arg0:08x} "
            f"arg1=0x{arg1:08x} arg2=0x{arg2:08x} marker={marker}"
        )
        shown += 1
        if shown >= args.limit:
            break
    print(f"shown={shown} packet_count={packet_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
