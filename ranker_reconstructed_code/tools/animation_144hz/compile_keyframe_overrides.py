#!/usr/bin/env python3

from __future__ import annotations

import argparse
import binascii
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


TOOL = Path(__file__).with_name("unit_sprite_assets.py")
SPEC = importlib.util.spec_from_file_location("unit_sprite_assets", TOOL)
assert SPEC is not None and SPEC.loader is not None
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


KEYFRAME_SPECS = (
    (0, 0, 2, 3, "BuildMan_g0_0002_to_0003_rgba.png"),
    (0, 0, 5, 6, "BuildMan_g0_0005_to_0006_rgba.png"),
    (0, 0, 8, 9, "BuildMan_g0_0008_to_0009_rgba.png"),
    (0, 0, 11, 12, "BuildMan_g0_0011_to_0012_rgba.png"),
    (0, 0, 14, 0, "BuildMan_g0_0014_to_0000_rgba.png"),
)


def body_points(frame: object) -> list[tuple[int, int, int]]:
    return [point for point in assets.frame_points(frame) if point[2] > 1]


def point_bounds(
    points: list[tuple[int, int, int]],
) -> tuple[int, int, int, int]:
    return (
        min(x for x, _y, _token in points),
        min(y for _x, y, _token in points),
        max(x for x, _y, _token in points) + 1,
        max(y for _x, y, _token in points) + 1,
    )


def extract_figures(image: Image.Image) -> list[Image.Image]:
    rgba = np.asarray(image.convert("RGBA"))
    mask = (rgba[:, :, 3] >= 96).astype(np.uint8)
    count, labels, stats, _centroids = cv2.connectedComponentsWithStats(
        mask, connectivity=8
    )
    components = [
        (label, tuple(int(value) for value in stats[label]))
        for label in range(1, count)
        if int(stats[label, cv2.CC_STAT_AREA]) >= 100
    ]
    components.sort(key=lambda item: item[1][cv2.CC_STAT_LEFT])
    if len(components) != assets.VISUAL_ARCHIVE_INTERVAL_COUNT + 1:
        raise ValueError(
            f"expected seven connected figures, found {len(components)}"
        )
    figures: list[Image.Image] = []
    for label, stat in components:
        left, top, width, height, _area = stat
        crop = rgba[top:top + height, left:left + width].copy()
        component_mask = labels[
            top:top + height, left:left + width
        ] == label
        crop[~component_mask] = 0
        figures.append(Image.fromarray(crop, "RGBA"))
    return figures


def used_palette(unit: object) -> list[tuple[int, tuple[int, int, int]]]:
    tokens = sorted({
        token
        for group in unit.groups
        for frame in group
        for _x, _y, token in assets.frame_points(frame)
        if token > 1
    })
    return [
        (token, assets.palette_rgba(unit.palette, token)[:3])
        for token in tokens
    ]


def nearest_palette_token(
    red: int,
    green: int,
    blue: int,
    palette: list[tuple[int, tuple[int, int, int]]],
) -> int:
    # Green carries most of the BuildMan tunic identity; perceived-colour
    # weighting also prevents dark olive pixels from drifting into skin hues.
    return min(
        palette,
        key=lambda item: (
            2 * (red - item[1][0]) ** 2
            + 4 * (green - item[1][1]) ** 2
            + 3 * (blue - item[1][2]) ** 2
        ),
    )[0]


def quantize_figure(
    figure: Image.Image,
    source_bounds: tuple[int, int, int, int],
    target_bounds: tuple[int, int, int, int],
    palette: list[tuple[int, tuple[int, int, int]]],
    step: int,
) -> tuple[dict[tuple[int, int], int], dict[tuple[int, int], int]]:
    source_left, source_top, source_right, source_bottom = source_bounds
    target_left, target_top, target_right, target_bottom = target_bounds
    output_width = max(1, assets.lerp_coordinate(
        source_right - source_left, target_right - target_left, step
    ))
    output_height = max(1, assets.lerp_coordinate(
        source_bottom - source_top, target_bottom - target_top, step
    ))
    output_center_x = assets.lerp_coordinate(
        source_left + source_right - 1,
        target_left + target_right - 1,
        step,
    ) / 2.0
    output_bottom = assets.lerp_coordinate(
        source_bottom - 1, target_bottom - 1, step
    )
    output_left = int(round(output_center_x - (output_width - 1) / 2.0))
    output_top = output_bottom - output_height + 1

    resized = figure.resize(
        (output_width, output_height), Image.Resampling.LANCZOS
    )
    rgba = np.asarray(resized.convert("RGBA"))
    generated: dict[tuple[int, int], int] = {}
    coverage: dict[tuple[int, int], int] = {}
    token_cache: dict[tuple[int, int, int], int] = {}
    for y, x in np.argwhere(rgba[:, :, 3] >= 96):
        red, green, blue = (int(value) for value in rgba[y, x, :3])
        colour = red, green, blue
        token = token_cache.get(colour)
        if token is None:
            token = nearest_palette_token(red, green, blue, palette)
            token_cache[colour] = token
        point = output_left + int(x), output_top + int(y)
        generated[point] = token
        coverage[point] = int(rgba[y, x, 3])
    if not generated:
        raise ValueError("generated key pose became empty after quantization")
    return generated, coverage


def trim_connected_body(
    dots: dict[tuple[int, int], int],
    coverage: dict[tuple[int, int], int],
    target_count: int,
    maximum_components: int,
) -> dict[tuple[int, int], int]:
    """Match endpoint density without cutting limbs or shrinking pose bounds."""
    trimmed = dict(dots)
    while len(trimmed) > target_count:
        xs = [x for x, _y in trimmed]
        ys = [y for _x, y in trimmed]
        left, right = min(xs), max(xs)
        top, bottom = min(ys), max(ys)
        boundary_counts = {
            "left": sum(x == left for x, _y in trimmed),
            "right": sum(x == right for x, _y in trimmed),
            "top": sum(y == top for _x, y in trimmed),
            "bottom": sum(y == bottom for _x, y in trimmed),
        }

        def neighbor_count(point: tuple[int, int]) -> int:
            x, y = point
            return sum(
                (x + delta_x, y + delta_y) in trimmed
                for delta_y in (-1, 0, 1)
                for delta_x in (-1, 0, 1)
                if delta_x or delta_y
            )

        candidates = sorted(
            (
                point for point in trimmed
                if neighbor_count(point) < 8
            ),
            key=lambda point: (
                coverage.get(point, 255),
                -neighbor_count(point),
                point[1],
                point[0],
            ),
        )
        removed = False
        for point in candidates:
            x, y = point
            if ((x == left and boundary_counts["left"] == 1) or
                    (x == right and boundary_counts["right"] == 1) or
                    (y == top and boundary_counts["top"] == 1) or
                    (y == bottom and boundary_counts["bottom"] == 1)):
                continue
            token = trimmed.pop(point)
            if len(assets.dot_components(trimmed, False)) <= maximum_components:
                removed = True
                break
            trimmed[point] = token
        if not removed:
            break
    return trimmed


def compile_override(
    archive: object,
    assets_directory: Path,
    spec: tuple[int, int, int, int, str],
) -> dict[str, object]:
    unit_type, group_index, source_index, target_index, filename = spec
    unit = assets.decode_unit_record(archive, unit_type)
    frames = unit.groups[group_index]
    source = frames[source_index]
    target = frames[target_index]
    source_points = assets.frame_points(source)
    target_points = assets.frame_points(target)
    source_body = body_points(source)
    target_body = body_points(target)
    source_bounds = point_bounds(source_body)
    target_bounds = point_bounds(target_body)
    source_components = len(assets.dot_components(
        {(x, y): token for x, y, token in source_body}, False
    ))
    target_components = len(assets.dot_components(
        {(x, y): token for x, y, token in target_body}, False
    ))
    maximum_components = max(1, source_components, target_components)
    source_image = assets_directory / filename
    figures = extract_figures(Image.open(source_image))
    palette = used_palette(unit)
    source_body_count = len(source_body)
    target_body_count = len(target_body)
    body_frames: list[dict[tuple[int, int], int]] = []
    for step in range(1, assets.VISUAL_ARCHIVE_INTERVAL_COUNT):
        quantized, coverage = quantize_figure(
            figures[step], source_bounds, target_bounds, palette, step
        )
        connected = assets.connect_excess_body_components(
            quantized, maximum_components
        )
        target_count = assets.lerp_coordinate(
            source_body_count, target_body_count, step
        )
        body_frames.append(trim_connected_body(
            connected, coverage, target_count, maximum_components
        ))
    shadow_frames = assets.coherent_distance_field_class_frames(
        source_points, target_points, True
    )
    frames_json: list[list[list[int]]] = []
    for body, shadow in zip(body_frames, shadow_frames):
        if len(assets.dot_components(body, False)) > maximum_components:
            raise ValueError("compiled body contains excess disconnected pieces")
        combined = dict(shadow)
        combined.update(body)
        frames_json.append([
            [x, y, token]
            for (x, y), token in sorted(
                combined.items(), key=lambda item: (item[0][1], item[0][0])
            )
        ])
    return {
        "unit_type": unit_type,
        "group": group_index,
        "source": source_index,
        "target": target_index,
        "source_image": filename,
        "source_image_sha256": hashlib.sha256(source_image.read_bytes()).hexdigest(),
        "frames": frames_json,
    }


def write_override_preview(
    archive: object,
    override: dict[str, object],
    output: Path,
    scale: int = 6,
) -> None:
    unit_type = int(override["unit_type"])
    group_index = int(override["group"])
    source_index = int(override["source"])
    target_index = int(override["target"])
    unit = assets.decode_unit_record(archive, unit_type)
    originals = unit.groups[group_index]
    generated = [
        {(int(x), int(y)): int(token) for x, y, token in frame}
        for frame in override["frames"]
    ]
    all_points = (
        assets.frame_points(originals[source_index])
        + assets.frame_points(originals[target_index])
        + [
            (x, y, token)
            for frame in generated
            for (x, y), token in frame.items()
        ]
    )
    left = min(x for x, _y, _token in all_points)
    top = min(y for _x, y, _token in all_points)
    right = max(x for x, _y, _token in all_points) + 1
    bottom = max(y for _x, y, _token in all_points) + 1
    margin = 8
    cell_width = right - left + margin * 2
    cell_height = bottom - top + margin * 2
    width = cell_width * 7
    canvas = bytearray(width * cell_height * 4)
    assets.fill_checkerboard(canvas, width, cell_height)
    frames = [
        assets.endpoint_dots(originals[source_index]),
        *generated,
        assets.endpoint_dots(originals[target_index]),
    ]
    for index, dots in enumerate(frames):
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()
    archive = assets.TrcArchive(args.archive)
    overrides = [
        compile_override(archive, args.assets, spec)
        for spec in KEYFRAME_SPECS
    ]
    document = {
        "schema": 1,
        "source_archive": args.archive.name,
        "source_archive_sha256": hashlib.sha256(archive.data).hexdigest(),
        "source_archive_crc32": f"{binascii.crc32(archive.data) & 0xffffffff:08x}",
        "intermediate_frames_per_transition": (
            assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
        ),
        "overrides": overrides,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if args.preview_dir is not None:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
        for override in overrides:
            write_override_preview(
                archive,
                override,
                args.preview_dir / (
                    f"unit_{int(override['unit_type']):03d}_"
                    f"group_{int(override['group']):02d}_"
                    f"{int(override['source']):04d}_to_"
                    f"{int(override['target']):04d}.png"
                ),
            )
    print(
        f"compiled keyframe overrides={len(overrides)} "
        f"frames={len(overrides) * assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT} "
        f"bytes={args.output.stat().st_size}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
