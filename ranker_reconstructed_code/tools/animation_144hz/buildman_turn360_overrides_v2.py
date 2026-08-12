#!/usr/bin/env python3
"""Pre-generate BuildMan's handedness-changing 360-degree inspection seams."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import importlib.util
import json
import sys
from pathlib import Path


DIRECTORY = Path(__file__).resolve().parent


def load_module(name: str, path: Path) -> object:
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


assets = load_module("unit_sprite_assets", DIRECTORY / "unit_sprite_assets.py")
precision = load_module(
    "precision_polyphase_prototype",
    DIRECTORY / "precision_polyphase_prototype.py",
)


def encoded_dots(dots: dict[tuple[int, int], int]) -> list[list[int]]:
    return [
        [x, y, token]
        for (x, y), token in sorted(
            dots.items(), key=lambda item: (item[0][1], item[0][0])
        )
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    group = unit.groups[0]
    if len(group) != 15:
        raise ValueError("BuildMan group 0 no longer has the expected 5x3 pose grid")

    # Clockwise front-start route: row 4 normal -> ... -> row 0 normal ->
    # row 1 flipped -> ... -> row 3 flipped -> row 4 normal.  Only the two
    # handedness-changing seams need banks beyond the normal transition JSON.
    seam_specs = []
    for action_phase in range(3):
        seam_specs.extend((
            (action_phase, 3 + action_phase, False, True),
            (9 + action_phase, 12 + action_phase, True, False),
        ))

    valid_tokens = {1}
    valid_tokens.update(
        point[2] for frame in group for point in assets.frame_points(frame)
    )
    overrides = []
    for source, target, source_flipped, target_flipped in seam_specs:
        print(
            f"analyzing 360 seam {source}{'F' if source_flipped else 'N'}"
            f"->{target}{'F' if target_flipped else 'N'}",
            flush=True,
        )
        source_points = assets.frame_points(group[source], source_flipped)
        target_points = assets.frame_points(group[target], target_flipped)
        middle = precision.precision_frames_from_points(
            source_points, target_points
        )
        if len(middle) != assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
            raise ValueError("360 seam produced the wrong phase count")
        if any(token not in valid_tokens for frame in middle for token in frame.values()):
            raise ValueError("360 seam introduced a non-TRC palette token")
        maximum_components = max(
            len(assets.dot_components(
                {(x, y): token for x, y, token in points}, False
            ))
            for points in (source_points, target_points)
        )
        if any(
            len(assets.dot_components(frame, False)) > maximum_components
            for frame in middle
        ):
            raise ValueError("360 seam introduced an excess body component")
        overrides.append({
            "unit_type": 0,
            "group": 0,
            "source": source,
            "target": target,
            "source_flipped": source_flipped,
            "target_flipped": target_flipped,
            "frames": [encoded_dots(frame) for frame in middle],
        })

    document = {
        "schema": 2,
        "purpose": "BuildMan 360-degree inspection handedness seams",
        "source_archive_sha256": hashlib.sha256(archive.data).hexdigest(),
        "source_archive_crc32": f"{binascii.crc32(archive.data) & 0xffffffff:08x}",
        "source_rate_hz": 60,
        "target_rate_hz": 144,
        "phase_count_per_transition": assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
        "intermediate_frames_per_transition": (
            assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
        ),
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "group": 0,
        "overrides": overrides,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(
        f"BuildMan 360 seam overrides complete transitions={len(overrides)} "
        f"output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
