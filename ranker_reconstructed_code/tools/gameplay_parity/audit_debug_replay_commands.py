#!/usr/bin/env python3
"""Inventory ordered gameplay commands used by a directory of Ranker replays."""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from inventory_gameplay_matrix import (
    REPLAY_HEADER_SIZE,
    REPLAY_PACKET_SIZE,
    TrcArchive,
)


HANDLERS: dict[int, tuple[str, str]] = {
    0x00: ("no-op marker", "0x004dca04"),
    0x01: ("unit production/refund", "0x004dca58"),
    0x02: ("basic unit order", "0x004dd55f"),
    0x03: ("equipment apply", "0x004dd9c0"),
    0x04: ("equipment toggle/fallback", "0x004dd9f3"),
    0x05: ("resource build/refund", "0x004dccee"),
    0x06: ("unit command bit", "0x004dda9a"),
    0x07: ("unit death mark", "0x004ddab6"),
    0x08: ("unit auxiliary vector", "0x004ddad3"),
    0x09: ("extended unit order", "0x004dd75e"),
    0x0A: ("forced order 0x21", "0x004dd827"),
    0x0B: ("unit status mask", "0x004dd83d"),
    0x0C: ("placement resource/refund", "0x004dcf81"),
    0x0D: ("nested command dispatcher", "0x004dd133"),
    0x0E: ("no-op marker", "0x004dca04"),
    0x0F: ("catch-up target", "0x004dde72"),
    0x10: ("consumed-packet acknowledgement", "0x004296f0"),
    0x11: ("legacy no-op", "0x004ddaf2"),
    0x12: ("no-op marker", "0x004dca04"),
    0x13: ("player inactive", "0x004ddaf3"),
    0x14: ("player relation", "0x004ddc2e"),
    0x15: ("player consensus", "0x004ddc5c"),
    0x16: ("modal pause", "0x004dde5d"),
    0x17: ("no-op marker", "0x004dca04"),
    0x18: ("no-op marker", "0x004dca04"),
    0x19: ("pending string action", "0x004dca05"),
    0x1A: ("production-cost order/refund", "0x004dd3b7"),
    0x1B: ("no-op marker", "0x004dca04"),
    0x1C: ("no-op marker", "0x004dca04"),
    0x1D: ("vote completion", "0x004ddbe1"),
}


def packet_rows(path: Path) -> tuple[list[tuple[int, ...]], int]:
    archive = TrcArchive(path)
    replay = next(
        (record for record in archive.records
         if record.name.casefold() == "replay"),
        None,
    )
    if replay is None:
        raise ValueError(f"Replay record missing from {path}")
    payload = replay.data[REPLAY_HEADER_SIZE:]
    packet_count, trailing = divmod(len(payload), REPLAY_PACKET_SIZE)
    return [
        struct.unpack_from("<IIIIIIIII", payload, index * REPLAY_PACKET_SIZE)
        for index in range(packet_count)
    ], trailing


def hex32(value: int) -> str:
    return f"0x{value:08x}"


def counter_rows(counter: Counter[int]) -> list[dict[str, Any]]:
    return [
        {"value": hex32(value), "count": count}
        for value, count in sorted(counter.items())
    ]


def build_inventory(paths: list[Path]) -> dict[str, Any]:
    subtype_counts: Counter[int] = Counter()
    subtype_replays: dict[int, set[str]] = defaultdict(set)
    command_counts: dict[int, Counter[tuple[int, bool]]] = defaultdict(Counter)
    mode_counts: dict[int, Counter[int]] = defaultdict(Counter)
    unit_zero_counts: Counter[int] = Counter()
    arg1_zero_counts: Counter[int] = Counter()
    arg2_zero_counts: Counter[int] = Counter()
    replay_rows: list[dict[str, Any]] = []
    first_frame: int | None = None
    last_frame: int | None = None
    total_packets = 0

    for path in paths:
        packets, trailing = packet_rows(path)
        replay_subtypes: Counter[int] = Counter()
        replay_first: int | None = None
        replay_last: int | None = None
        for marker, frame, sequence, packed, command, unit_offset, mode, arg1, arg2 in packets:
            del marker, sequence
            subtype = (packed >> 24) & 0xFF
            queued = (command & 0x80000000) != 0
            normalized = command & 0x7FFFFFFF
            subtype_counts[subtype] += 1
            replay_subtypes[subtype] += 1
            subtype_replays[subtype].add(path.name)
            command_counts[subtype][(normalized, queued)] += 1
            mode_counts[subtype][mode] += 1
            if unit_offset == 0:
                unit_zero_counts[subtype] += 1
            if arg1 == 0:
                arg1_zero_counts[subtype] += 1
            if arg2 == 0:
                arg2_zero_counts[subtype] += 1
            replay_first = frame if replay_first is None else min(replay_first, frame)
            replay_last = frame if replay_last is None else max(replay_last, frame)
        total_packets += len(packets)
        if replay_first is not None:
            first_frame = replay_first if first_frame is None else min(first_frame, replay_first)
            last_frame = replay_last if last_frame is None else max(last_frame, replay_last)
        replay_rows.append({
            "file": path.name,
            "packets": len(packets),
            "first_frame": replay_first,
            "last_frame": replay_last,
            "trailing_bytes": trailing,
            "subtypes": {
                f"0x{subtype:02x}": count
                for subtype, count in sorted(replay_subtypes.items())
            },
        })

    subtype_rows: list[dict[str, Any]] = []
    for subtype, count in sorted(subtype_counts.items()):
        label, original_handler = HANDLERS.get(
            subtype, ("unknown", "unmapped"))
        subtype_rows.append({
            "subtype": f"0x{subtype:02x}",
            "label": label,
            "original_handler": original_handler,
            "packets": count,
            "replay_count": len(subtype_replays[subtype]),
            "unit_offset_zero": unit_zero_counts[subtype],
            "arg1_zero": arg1_zero_counts[subtype],
            "arg2_zero": arg2_zero_counts[subtype],
            "modes": counter_rows(mode_counts[subtype]),
            "commands": [
                {
                    "command": hex32(command),
                    "queued": queued,
                    "count": command_count,
                }
                for (command, queued), command_count in
                sorted(command_counts[subtype].items())
            ],
        })

    return {
        "format": "ranker-debug-replay-command-inventory-v1",
        "replay_count": len(paths),
        "packet_count": total_packets,
        "first_frame": first_frame,
        "last_frame": last_frame,
        "unique_subtype_command_queue_combinations": sum(
            len(commands) for commands in command_counts.values()),
        "subtypes": subtype_rows,
        "replays": replay_rows,
    }


def markdown_report(inventory: dict[str, Any], replay_dir: Path) -> str:
    lines = [
        "# Debug replay command inventory",
        "",
        f"- Source: `{replay_dir.as_posix()}`",
        f"- Replays: {inventory['replay_count']}",
        f"- Ordered gameplay packets: {inventory['packet_count']}",
        f"- Frame range: {inventory['first_frame']}–{inventory['last_frame']}",
        "- Packet layout: 36 bytes; subtype=`packed_opcode[31:24]`, "
        "command=`packet+0x10`, unit=`packet+0x14`, arguments=`+0x18..+0x20`",
        "",
        "| Subtype | Meaning | Original handler | Packets | Replays | Commands |",
        "|---|---|---:|---:|---:|---|",
    ]
    for row in inventory["subtypes"]:
        commands = ", ".join(
            f"{command['command']}{' queued' if command['queued'] else ''}"
            f" ({command['count']})"
            for command in row["commands"]
        )
        lines.append(
            f"| {row['subtype']} | {row['label']} | "
            f"{row['original_handler']} | {row['packets']} | "
            f"{row['replay_count']} | {commands} |"
        )
    lines.extend([
        "",
        "The JSON companion contains per-replay counts, mode values and zero/non-zero "
        "argument coverage for every observed subtype/command combination.",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("replay_dir", type=Path)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--markdown-output", type=Path)
    args = parser.parse_args()

    paths = sorted(
        args.replay_dir.glob("*.ply"),
        key=lambda path: (int(path.stem) if path.stem.isdigit() else 1 << 31,
                          path.name.casefold()),
    )
    if not paths:
        raise ValueError(f"No .ply files found under {args.replay_dir}")
    inventory = build_inventory(paths)
    rendered_json = json.dumps(inventory, ensure_ascii=False, indent=2) + "\n"
    rendered_markdown = markdown_report(inventory, args.replay_dir)

    if args.json_output is not None:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(rendered_json, encoding="utf-8")
    if args.markdown_output is not None:
        args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_output.write_text(rendered_markdown, encoding="utf-8")
    if args.json_output is None and args.markdown_output is None:
        print(rendered_json, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
