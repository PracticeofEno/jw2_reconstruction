#!/usr/bin/env python3
"""Recover BuildMan's actual direction-row animation graph from its definition."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path


TOOL = Path(__file__).with_name("unit_sprite_assets.py")
SPEC = importlib.util.spec_from_file_location("unit_sprite_assets", TOOL)
assert SPEC is not None and SPEC.loader is not None
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


FRAME_TABLE_BASE = 0x140C
FRAME_TABLE_STRIDE = 0x100
ROW_TABLE_BASE = 0x2248
ROW_TABLE_STRIDE = 0x20
PRIMARY_DIRECTION_ROWS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 4, 3, 2)
PRIMARY_DIRECTION_FLIPS = (
    False, False, False, False, False, False, False, False, False,
    False, False, False, False, False, False, True, True, True,
)


def animation_period(table: list[int], frame_count: int) -> int:
    """Stop at the stable reset tail written after each real sequence."""
    prefix = [value for value in table if value < frame_count]
    if not prefix:
        return 0
    tail_value = prefix[-1]
    tail_start = len(prefix) - 1
    while tail_start > 0 and prefix[tail_start - 1] == tail_value:
        tail_start -= 1
    # Long repeated tails are table padding, while short holds inside an
    # animation are intentional timing. Preserve one reset sample only.
    if len(prefix) - tail_start >= 8:
        return max(1, tail_start + 1)
    return len(prefix)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    groups = []
    all_edges: set[tuple[int, int, int]] = set()
    for group_index, frames in enumerate(unit.groups):
        if not frames:
            continue
        frame_count = len(frames)
        frame_table = [
            assets.read_u32(
                unit.definition,
                FRAME_TABLE_BASE + group_index * FRAME_TABLE_STRIDE + index * 4,
            )
            for index in range(64)
        ]
        # A definition row is meaningful only when its base can address this
        # image group. Duplicate row bases are kept as direction aliases.
        definition_rows = [
            assets.read_u32(
                unit.definition,
                ROW_TABLE_BASE + group_index * ROW_TABLE_STRIDE + row * 4,
            )
            for row in range(8)
        ]
        direction_records = []
        for direction, (resource_row, flipped) in enumerate(zip(
            PRIMARY_DIRECTION_ROWS, PRIMARY_DIRECTION_FLIPS
        )):
            row_slot = min(resource_row, 7)
            row_base = definition_rows[row_slot]
            if row_base >= frame_count:
                continue
            available = frame_count - row_base
            period = animation_period(frame_table, available)
            sequence = [
                frame_table[index] + row_base
                for index in range(period)
                if frame_table[index] < available
            ]
            if not sequence:
                continue
            edges = []
            for source, target in zip(sequence, sequence[1:] + sequence[:1]):
                if source == target:
                    continue
                edge = (group_index, source, target)
                all_edges.add(edge)
                edges.append({"source": source, "target": target})
            direction_records.append({
                "direction_index": direction,
                "resource_row": resource_row,
                "row_slot": row_slot,
                "row_base": row_base,
                "flipped": flipped,
                "period": period,
                "sequence": sequence,
                "edges": edges,
            })
        groups.append({
            "group": group_index,
            "image_count": frame_count,
            "definition_row_bases": definition_rows,
            "directions": direction_records,
        })
    document = {
        "schema": 1,
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "source_archive": args.archive.name,
        "original_image_count": unit.image_count,
        "groups": groups,
        "unique_runtime_edges": [
            {"group": group, "source": source, "target": target}
            for group, source, target in sorted(all_edges)
        ],
        "unique_runtime_edge_count": len(all_edges),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"BuildMan pose graph groups={len(groups)} "
        f"images={unit.image_count} runtime_edges={len(all_edges)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
