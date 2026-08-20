#!/usr/bin/env python3
"""Clone a death replay with one active unit carrying a dropped item effect."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


PRIMARY_CAMERA_X_OFFSET = 0x1410
PRIMARY_CAMERA_Y_OFFSET = 0x1414
ACTIVE_UNIT_HEAD_OFFSET = 0x143C
SCENARIO_OBJECT_STRIDE = 0x1D0
SCENARIO_OBJECT_RECORD = 7
PRIMARY_EQUIPMENT_OFFSET = 0x40
WORLD_BAR_SELECTION_OFFSET = 0x08
WORLD_BAR_SELECTION_FLAG = 0x80
NEXT_ACTIVE_OFFSET = 0x1CC
REPLAY_MAP_PATH_OFFSET = 0x1FB
REPLAY_MAP_PATH_BYTES = 260


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def active_slots(header: bytes, objects: bytes) -> list[int]:
    result: list[int] = []
    seen: set[int] = set()
    raw = u32(header, ACTIVE_UNIT_HEAD_OFFSET)
    while raw != 0:
        if raw % SCENARIO_OBJECT_STRIDE != 0:
            raise ValueError(f"unaligned active-unit reference: {raw:#x}")
        slot = raw // SCENARIO_OBJECT_STRIDE
        base = slot * SCENARIO_OBJECT_STRIDE
        if slot in seen or base + SCENARIO_OBJECT_STRIDE > len(objects):
            raise ValueError(f"invalid active-unit chain at slot {slot}")
        seen.add(slot)
        result.append(slot)
        raw = u32(objects, base + NEXT_ACTIVE_OFFSET)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--effect-id", type=int, default=13)
    parser.add_argument("--active-index", type=int, default=0)
    parser.add_argument("--camera-x", type=int, default=0)
    parser.add_argument("--camera-y", type=int, default=0)
    parser.add_argument("--world-bar-selected", action="store_true")
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
    replay_index = next((
        index for index, record in enumerate(records)
        if record.name.casefold() == "replay"), None)
    if replay_index is None:
        raise ValueError("source archive has no Replay record")
    if SCENARIO_OBJECT_RECORD >= replay_index:
        raise ValueError("source archive has no scenario object record")

    header = bytearray(records[0].data)
    objects = bytearray(records[SCENARIO_OBJECT_RECORD].data)
    slots = active_slots(header, objects)
    if args.active_index < 0 or args.active_index >= len(slots):
        raise ValueError(
            f"active index {args.active_index} outside 0..{len(slots) - 1}")
    selected_slot = slots[args.active_index]
    write_u32(header, PRIMARY_CAMERA_X_OFFSET, args.camera_x)
    write_u32(header, PRIMARY_CAMERA_Y_OFFSET, args.camera_y)
    write_u32(objects, selected_slot * SCENARIO_OBJECT_STRIDE +
              PRIMARY_EQUIPMENT_OFFSET, args.effect_id)
    if args.world_bar_selected:
        selection_offset = (selected_slot * SCENARIO_OBJECT_STRIDE +
                            WORLD_BAR_SELECTION_OFFSET)
        write_u32(objects, selection_offset,
                  u32(objects, selection_offset) | WORLD_BAR_SELECTION_FLAG)
    records[0] = replace_trc_record(records[0], bytes(header))
    records[SCENARIO_OBJECT_RECORD] = replace_trc_record(
        records[SCENARIO_OBJECT_RECORD], bytes(objects))

    game_root = source.parent.parent
    map_output = (game_root / "Maps" / f"{output.stem}.trk").resolve()
    write_trc_archive(
        map_output, records[:replay_index], archive.directory_slots)

    replay_payload = bytearray(records[replay_index].data)
    map_relative = f"Maps\\{map_output.name}".encode("cp949")
    if len(map_relative) >= REPLAY_MAP_PATH_BYTES:
        raise ValueError("replacement replay map path is too long")
    replay_payload[
        REPLAY_MAP_PATH_OFFSET:
        REPLAY_MAP_PATH_OFFSET + REPLAY_MAP_PATH_BYTES] = (
            map_relative + b"\0" * (REPLAY_MAP_PATH_BYTES - len(map_relative)))
    records[replay_index] = replace_trc_record(
        records[replay_index], bytes(replay_payload))
    write_trc_archive(output, records, archive.directory_slots)

    print(
        "EQUIPPED_DEATH_REPLAY_WRITTEN "
        f"source={source} output={output} map={map_output} "
        f"active_index={args.active_index} slot={selected_slot} "
        f"effect={args.effect_id} selected={args.world_bar_selected} "
        f"camera={args.camera_x},{args.camera_y}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
