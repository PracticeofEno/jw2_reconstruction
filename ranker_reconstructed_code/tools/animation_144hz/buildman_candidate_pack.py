#!/usr/bin/env python3
"""Generate three crisp BuildMan animation candidates for every cyclic transition."""

from __future__ import annotations

import argparse
import binascii
import hashlib
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


PROFILES = (
    ("A_original", "원본 도트 대응 우선"),
    ("B_balanced", "실루엣과 동작 균형"),
    ("C_motion", "관절 호와 가림 순서 강조"),
)


def clean_frames(
    source: object,
    target: object,
    generated: list[dict[tuple[int, int], int]],
    valid_tokens: set[int],
) -> tuple[list[dict[tuple[int, int], int]], int]:
    endpoint_components = max(
        1,
        len(assets.dot_components(assets.endpoint_dots(source), False)),
        len(assets.dot_components(assets.endpoint_dots(target), False)),
    )
    cleaned = []
    for dots in generated:
        if not dots or any(token not in valid_tokens for token in dots.values()):
            raise ValueError("candidate contains empty or non-TRC-palette dots")
        repaired = assets.connect_excess_body_components(dots, endpoint_components)
        if len(assets.dot_components(repaired, False)) > endpoint_components:
            raise ValueError("candidate contains excess disconnected body pieces")
        cleaned.append(repaired)
    if len(cleaned) != assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
        raise ValueError("candidate transition does not contain five middle frames")
    return cleaned, endpoint_components


def generate_profile(
    profile: str,
    source: object,
    target: object,
    group_frames: list[object],
    source_index: int,
    target_index: int,
) -> tuple[list[dict[tuple[int, int], int]], str]:
    source_points = assets.frame_points(source)
    target_points = assets.frame_points(target)
    if profile == "A_original":
        frames, mode, _coverage = assets.build_hybrid_morph_frames(
            source, target, group_frames, source_index, target_index
        )
        return frames, f"original_first:{mode}"
    if profile == "B_balanced":
        return assets.coherent_distance_field_morph_frames_from_points(
            source_points, target_points
        ), "balanced_distance_field"
    if profile == "C_motion":
        single = assets.should_use_single_silhouette_visual_morph(source, target)
        return assets.coherent_optical_flow_morph_frames_from_points(
            source_points, target_points, single
        ), "motion_optical_flow_single" if single else "motion_optical_flow"
    raise ValueError(f"unknown profile {profile}")


def write_strip(
    output: Path,
    unit: object,
    source: object,
    target: object,
    generated: list[dict[tuple[int, int], int]],
    scale: int,
) -> tuple[int, int, int, int]:
    dots_frames = [assets.endpoint_dots(source), *generated, assets.endpoint_dots(target)]
    points = [
        (x, y, token)
        for dots in dots_frames
        for (x, y), token in dots.items()
    ]
    left = min(x for x, _y, _token in points)
    top = min(y for _x, y, _token in points)
    right = max(x for x, _y, _token in points) + 1
    bottom = max(y for _x, y, _token in points) + 1
    margin = 7
    cell_width = right - left + margin * 2
    cell_height = bottom - top + margin * 2
    width = cell_width * len(dots_frames)
    canvas = bytearray(width * cell_height * 4)
    assets.fill_checkerboard(canvas, width, cell_height)
    for index, dots in enumerate(dots_frames):
        assets.draw_morph_dots(
            canvas,
            width,
            cell_height,
            dots,
            unit.palette,
            index * cell_width + margin - left,
            margin - top,
        )
    scaled_width, scaled_height, rgba = assets.scale_rgba_nearest(
        bytes(canvas), width, cell_height, scale
    )
    assets.write_rgba_png(output, scaled_width, scaled_height, rgba)
    return scaled_width, scaled_height, cell_width * scale, cell_height * scale


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--balanced-overrides", type=Path, required=True)
    parser.add_argument("--motion-overrides", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=5)
    args = parser.parse_args()

    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    balanced = assets.load_keyframe_overrides(archive, args.balanced_overrides)
    motion = assets.load_keyframe_overrides(archive, args.motion_overrides)
    valid_tokens = {1} | {
        token
        for group in unit.groups
        for frame in group
        for _x, _y, token in assets.frame_points(frame)
        if token > 1
    }
    entries: list[dict[str, object]] = []
    for group_index, group_frames in enumerate(unit.groups):
        if not group_frames:
            continue
        for source_index, source in enumerate(group_frames):
            target_index = (source_index + 1) % len(group_frames)
            target = group_frames[target_index]
            candidate_records = []
            for profile, label in PROFILES:
                override_set = balanced if profile == "B_balanced" else motion
                override = override_set.get(
                    (0, group_index, source_index, target_index)
                )
                if override is not None and profile != "A_original":
                    generated = override
                    mode = "authored_balanced" if profile == "B_balanced" else "authored_motion"
                else:
                    generated, mode = generate_profile(
                        profile,
                        source,
                        target,
                        group_frames,
                        source_index,
                        target_index,
                    )
                generated, endpoint_components = clean_frames(
                    source, target, generated, valid_tokens
                )
                output = (
                    args.output_dir
                    / profile
                    / f"group_{group_index:02d}"
                    / f"{source_index:04d}_to_{target_index:04d}.png"
                )
                width, height, cell_width, cell_height = write_strip(
                    output, unit, source, target, generated, args.scale
                )
                candidate_records.append({
                    "code": profile[0],
                    "profile": profile,
                    "label": label,
                    "mode": mode,
                    "image": str(output.resolve()),
                    "width": width,
                    "height": height,
                    "cell_width": cell_width,
                    "cell_height": cell_height,
                    "body_component_limit": endpoint_components,
                    "maximum_body_components": max(
                        len(assets.dot_components(frame, False))
                        for frame in generated
                    ),
                    "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                })
            entries.append({
                "key": f"g{group_index:02d}_{source_index:04d}_to_{target_index:04d}",
                "group": group_index,
                "source": source_index,
                "target": target_index,
                "candidates": candidate_records,
            })

    document = {
        "schema": 1,
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "source_archive": args.archive.name,
        "source_archive_sha256": hashlib.sha256(archive.data).hexdigest(),
        "source_archive_crc32": f"{binascii.crc32(archive.data) & 0xffffffff:08x}",
        "original_frame_count": unit.image_count,
        "transition_count": len(entries),
        "candidate_count": len(entries) * len(PROFILES),
        "frames_per_candidate": 7,
        "intermediate_frames_per_candidate": 5,
        "profiles": [
            {"code": profile[0], "profile": profile, "label": label}
            for profile, label in PROFILES
        ],
        "transitions": entries,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"BuildMan candidates valid transitions={len(entries)} "
        f"candidates={len(entries) * len(PROFILES)} "
        f"display_frames={len(entries) * len(PROFILES) * 7}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
