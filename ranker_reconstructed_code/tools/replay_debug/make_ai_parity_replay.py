#!/usr/bin/env python3
"""Build a no-input replay that exercises one Computer-owned player slot."""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path


PLAYER_RECORD = 3
PLAYER_LOCAL_OWNER_OFFSET = 0x00
PLAYER_STATE_BASE = 0xF4
PLAYER_STATE_STRIDE = 4
PLAYER_SLOT_COUNT = 8
PLAYER_STATE_HUMAN = 0
PLAYER_STATE_COMPUTER = 1
PLAYER_STATE_DISABLED = 0x14
REPLAY_LOCAL_OWNER_OFFSET = 0x5F
REPLAY_MODE_OFFSET = 0x87
REPLAY_MAP_PATH_OFFSET = 0x1FB
REPLAY_MAP_PATH_BYTES = 260
SCENARIO_OBJECT_RECORD = 7
SCENARIO_OBJECT_STRIDE = 0x1D0


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def make_packet(frame: int, sequence: int, owner: int, subtype: int) -> bytes:
    packet = bytearray(0x24)
    struct.pack_into(
        "<IIIIIIIII", packet, 0,
        1, frame, sequence, (subtype << 24) | owner,
        0, 0, 0, 0, 0)
    return bytes(packet)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[3])
    parser.add_argument("--template-map", default="(2) Direction v1.3.trk")
    parser.add_argument("--base-replay", default="error1.ply")
    parser.add_argument("--output-stem", default="DebugReplay_AI_Parity")
    parser.add_argument("--end-frame", type=int, default=1000)
    parser.add_argument("--replay-mode", type=int, default=1)
    parser.add_argument("--human-owner", type=int, default=0)
    parser.add_argument("--computer-owner", type=int, default=1)
    args = parser.parse_args()

    if args.end_frame < 1:
        raise ValueError("end frame must be positive")
    if not 0 <= args.replay_mode <= 0xFF:
        raise ValueError("replay mode must fit in one byte")
    if not (0 <= args.human_owner < PLAYER_SLOT_COUNT and
            0 <= args.computer_owner < PLAYER_SLOT_COUNT):
        raise ValueError("owners must be player slots from 0 through 7")
    if args.human_owner == args.computer_owner:
        raise ValueError("human and Computer owners must differ")

    tool_dir = Path(__file__).resolve().parent
    parity_dir = tool_dir.parent / "gameplay_parity"
    sys.path.insert(0, str(parity_dir))
    from inventory_gameplay_matrix import (  # pylint: disable=import-error
        REPLAY_HEADER_SIZE,
        TrcArchive,
        append_trc_record,
        replace_trc_record,
        write_trc_archive,
    )

    root = args.root.resolve()
    game = root / "RankerOCPV_Win"
    template_path = game / "Maps" / args.template_map
    base_replay_path = game / "Replays" / args.base_replay
    map_path = game / "Maps" / f"{args.output_stem}.trk"
    replay_path = game / "Replays" / f"{args.output_stem}.ply"

    template = TrcArchive(template_path)
    map_records = list(template.records)
    if PLAYER_RECORD >= len(map_records):
        raise ValueError("template map has no player record")
    players = bytearray(map_records[PLAYER_RECORD].data)
    required_player_bytes = (
        PLAYER_STATE_BASE + PLAYER_SLOT_COUNT * PLAYER_STATE_STRIDE)
    if len(players) < required_player_bytes:
        raise ValueError("template player record is truncated")
    write_u32(players, PLAYER_LOCAL_OWNER_OFFSET, args.human_owner)
    for owner in range(PLAYER_SLOT_COUNT):
        state = PLAYER_STATE_DISABLED
        if owner == args.human_owner:
            state = PLAYER_STATE_HUMAN
        elif owner == args.computer_owner:
            state = PLAYER_STATE_COMPUTER
        write_u32(
            players, PLAYER_STATE_BASE + owner * PLAYER_STATE_STRIDE, state)
    map_records[PLAYER_RECORD] = replace_trc_record(
        map_records[PLAYER_RECORD], bytes(players))

    objects = map_records[SCENARIO_OBJECT_RECORD].data
    owner_counts = Counter()
    for base in range(0, len(objects), SCENARIO_OBJECT_STRIDE):
        if base + SCENARIO_OBJECT_STRIDE > len(objects):
            break
        unit_type, owner = struct.unpack_from("<II", objects, base)
        active_flags = struct.unpack_from("<I", objects, base + 0xA0)[0]
        if unit_type and (active_flags & 1):
            owner_counts[owner] += 1

    write_trc_archive(map_path, map_records, template.directory_slots)

    base_replay = TrcArchive(base_replay_path)
    replay_record = next((
        record for record in base_replay.records
        if record.name.casefold() == "replay"), None)
    if replay_record is None or len(replay_record.data) < REPLAY_HEADER_SIZE:
        raise ValueError("base replay has no valid Replay payload")
    replay_header = bytearray(replay_record.data[:REPLAY_HEADER_SIZE])
    replay_header[REPLAY_LOCAL_OWNER_OFFSET] = args.human_owner
    # Mode 1 follows the ordinary match start path and creates each active
    # faction's starting group.  This is important here: ordinary maps mostly
    # serialize neutral scenery, so mode 5 would leave the Computer slot with
    # nothing to manage.
    replay_header[REPLAY_MODE_OFFSET] = args.replay_mode
    map_name = f"Maps\\{map_path.name}".encode("ascii")
    if len(map_name) >= REPLAY_MAP_PATH_BYTES:
        raise ValueError("generated map path is too long")
    replay_header[
        REPLAY_MAP_PATH_OFFSET:
        REPLAY_MAP_PATH_OFFSET + REPLAY_MAP_PATH_BYTES] = (
            map_name + b"\0" * (REPLAY_MAP_PATH_BYTES - len(map_name)))
    replay_payload = bytes(replay_header) + make_packet(
        args.end_frame, 0, args.human_owner, 0x13)
    replay_records = append_trc_record(
        map_records, "Replay", replay_payload, 2)
    write_trc_archive(
        replay_path, replay_records, template.directory_slots)

    print(
        "AI_PARITY_REPLAY_WRITTEN "
        f"map={map_path} replay={replay_path} end_frame={args.end_frame} "
        f"human_owner={args.human_owner} "
        f"computer_owner={args.computer_owner} mode={args.replay_mode} "
        f"active_owner_counts={dict(sorted(owner_counts.items()))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
