#!/usr/bin/env python3
"""Prototype exact 60->144 polyphase, indexed-sprite motion interpolation."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

import cv2
import numpy as np


TOOL = Path(__file__).with_name("unit_sprite_assets.py")
SPEC = importlib.util.spec_from_file_location("unit_sprite_assets", TOOL)
assert SPEC is not None and SPEC.loader is not None
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


PHASE_COUNT = 12  # LCM timebase for 60 Hz input and 144 Hz output.
UPSCALE = 12


def raster_points(
    points: list[tuple[int, int, int]],
    left: int,
    top: int,
    width: int,
    height: int,
    shadow: bool,
) -> tuple[np.ndarray, np.ndarray]:
    mask = np.zeros((height, width), dtype=np.uint8)
    tokens = np.zeros((height, width), dtype=np.uint8)
    for x, y, token in points:
        if (token == 1) != shadow:
            continue
        px = x - left
        py = y - top
        if 0 <= px < width and 0 <= py < height:
            mask[py, px] = 255
            tokens[py, px] = token
    return mask, tokens


def high_resolution_feature(mask: np.ndarray, tokens: np.ndarray) -> np.ndarray:
    high_mask = cv2.resize(
        mask, None, fx=UPSCALE, fy=UPSCALE, interpolation=cv2.INTER_NEAREST
    )
    high_tokens = cv2.resize(
        tokens, None, fx=UPSCALE, fy=UPSCALE, interpolation=cv2.INTER_NEAREST
    )
    distance = cv2.distanceTransform(high_mask, cv2.DIST_L2, 5)
    if float(distance.max()) > 0:
        distance *= 160.0 / float(distance.max())
    # Token boundaries are useful motion landmarks, but silhouette distance
    # remains dominant so palette changes cannot tear limbs apart.
    token_edges = cv2.Laplacian(high_tokens, cv2.CV_16S, ksize=3)
    token_edges = np.clip(np.abs(token_edges), 0, 255).astype(np.uint8)
    return np.where(
        high_mask != 0,
        np.clip(64.0 + distance + token_edges.astype(np.float32) * 0.12, 0, 255),
        0,
    ).astype(np.uint8)


def dense_flow(source: np.ndarray, target: np.ndarray) -> np.ndarray:
    algorithm = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    algorithm.setFinestScale(0)
    algorithm.setGradientDescentIterations(40)
    algorithm.setVariationalRefinementIterations(10)
    algorithm.setVariationalRefinementAlpha(15.0)
    algorithm.setVariationalRefinementDelta(5.0)
    algorithm.setVariationalRefinementGamma(10.0)
    return algorithm.calc(source, target, None)


def warp(image: np.ndarray, flow: np.ndarray, fraction: float, interpolation: int) -> np.ndarray:
    height, width = image.shape
    grid_x, grid_y = np.meshgrid(
        np.arange(width, dtype=np.float32),
        np.arange(height, dtype=np.float32),
    )
    return cv2.remap(
        image,
        grid_x - flow[:, :, 0] * fraction,
        grid_y - flow[:, :, 1] * fraction,
        interpolation,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=0,
    )


def interpolate_class(
    source_mask: np.ndarray,
    source_tokens: np.ndarray,
    target_mask: np.ndarray,
    target_tokens: np.ndarray,
    shadow: bool,
) -> list[dict[tuple[int, int], int]]:
    high_source_mask = cv2.resize(
        source_mask, None, fx=UPSCALE, fy=UPSCALE, interpolation=cv2.INTER_NEAREST
    )
    high_target_mask = cv2.resize(
        target_mask, None, fx=UPSCALE, fy=UPSCALE, interpolation=cv2.INTER_NEAREST
    )
    high_source_tokens = cv2.resize(
        source_tokens, None, fx=UPSCALE, fy=UPSCALE, interpolation=cv2.INTER_NEAREST
    )
    high_target_tokens = cv2.resize(
        target_tokens, None, fx=UPSCALE, fy=UPSCALE, interpolation=cv2.INTER_NEAREST
    )
    source_feature = high_resolution_feature(source_mask, source_tokens)
    target_feature = high_resolution_feature(target_mask, target_tokens)
    forward = dense_flow(source_feature, target_feature)
    reverse = dense_flow(target_feature, source_feature)
    used_tokens = sorted(set(int(v) for v in source_tokens.flat if v) |
                         set(int(v) for v in target_tokens.flat if v))
    kernel = np.ones((3, 3), dtype=np.uint8)
    result: list[dict[tuple[int, int], int]] = []
    for phase in range(1, PHASE_COUNT):
        fraction = phase / PHASE_COUNT
        source_alpha = warp(
            high_source_mask, forward, fraction, cv2.INTER_LINEAR
        ).astype(np.float32) / 255.0
        target_alpha = warp(
            high_target_mask, reverse, 1.0 - fraction, cv2.INTER_LINEAR
        ).astype(np.float32) / 255.0
        confidence = source_alpha * (1.0 - fraction) + target_alpha * fraction
        hard = np.where(confidence >= 0.34, 255, 0).astype(np.uint8)
        hard = cv2.morphologyEx(hard, cv2.MORPH_CLOSE, kernel)
        low_coverage = cv2.resize(
            hard, (source_mask.shape[1], source_mask.shape[0]),
            interpolation=cv2.INTER_AREA,
        )
        occupied = low_coverage >= 88

        best_score = np.full(source_mask.shape, -1.0, dtype=np.float32)
        best_token = np.zeros(source_mask.shape, dtype=np.uint8)
        for token in used_tokens:
            source_one = np.where(high_source_tokens == token, 255, 0).astype(np.uint8)
            target_one = np.where(high_target_tokens == token, 255, 0).astype(np.uint8)
            source_score = warp(source_one, forward, fraction, cv2.INTER_LINEAR)
            target_score = warp(target_one, reverse, 1.0 - fraction, cv2.INTER_LINEAR)
            combined = (
                source_score.astype(np.float32) * (1.0 - fraction) +
                target_score.astype(np.float32) * fraction
            )
            low_score = cv2.resize(
                combined, (source_mask.shape[1], source_mask.shape[0]),
                interpolation=cv2.INTER_AREA,
            )
            selected = low_score > best_score
            best_score[selected] = low_score[selected]
            best_token[selected] = token
        if shadow:
            best_token[occupied] = 1
        dots: dict[tuple[int, int], int] = {}
        for y, x in np.argwhere(occupied & (best_token != 0)):
            dots[(int(x), int(y))] = int(best_token[y, x])
        result.append(dots)
    return result


def precision_frames_from_points(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
) -> list[dict[tuple[int, int], int]]:
    """Build the precision bank for already resolved normal/flipped pixels."""
    all_points = source_points + target_points
    margin = 8
    left = min(x for x, _y, _token in all_points) - margin
    top = min(y for _x, y, _token in all_points) - margin
    right = max(x for x, _y, _token in all_points) + margin + 1
    bottom = max(y for _x, y, _token in all_points) + margin + 1
    width = right - left
    height = bottom - top
    class_frames = []
    for shadow in (True, False):
        source_mask, source_tokens = raster_points(
            source_points, left, top, width, height, shadow
        )
        target_mask, target_tokens = raster_points(
            target_points, left, top, width, height, shadow
        )
        class_frames.append(interpolate_class(
            source_mask, source_tokens, target_mask, target_tokens, shadow
        ))
    maximum_components = max(
        1,
        len(assets.dot_components(
            {(x, y): token for x, y, token in source_points}, False
        )),
        len(assets.dot_components(
            {(x, y): token for x, y, token in target_points}, False
        )),
    )
    result = []
    for shadow, body in zip(*class_frames):
        world_body = {(x + left, y + top): token for (x, y), token in body.items()}
        world_body = assets.connect_excess_body_components(
            world_body, maximum_components
        )
        combined = {(x + left, y + top): token for (x, y), token in shadow.items()}
        combined.update(world_body)
        result.append(combined)
    return result


def precision_frames(source: object, target: object) -> list[dict[tuple[int, int], int]]:
    return precision_frames_from_points(
        assets.frame_points(source), assets.frame_points(target)
    )


def write_strip(
    unit: object,
    source: object,
    target: object,
    middle: list[dict[tuple[int, int], int]],
    output: Path,
    scale: int,
) -> None:
    frames = [assets.endpoint_dots(source), *middle, assets.endpoint_dots(target)]
    points = [(x, y, token) for dots in frames for (x, y), token in dots.items()]
    left = min(x for x, _y, _token in points)
    top = min(y for _x, y, _token in points)
    right = max(x for x, _y, _token in points) + 1
    bottom = max(y for _x, y, _token in points) + 1
    margin = 6
    cell_width = right - left + margin * 2
    cell_height = bottom - top + margin * 2
    width = cell_width * len(frames)
    canvas = bytearray(width * cell_height * 4)
    assets.fill_checkerboard(canvas, width, cell_height)
    for index, dots in enumerate(frames):
        assets.draw_morph_dots(
            canvas, width, cell_height, dots, unit.palette,
            index * cell_width + margin - left, margin - top,
        )
    scaled_width, scaled_height, rgba = assets.scale_rgba_nearest(
        bytes(canvas), width, cell_height, scale
    )
    assets.write_rgba_png(output, scaled_width, scaled_height, rgba)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--group", type=int, required=True)
    parser.add_argument("--source", type=int, required=True)
    parser.add_argument("--target", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=5)
    args = parser.parse_args()
    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    group = unit.groups[args.group]
    middle = precision_frames(group[args.source], group[args.target])
    if len(middle) != PHASE_COUNT - 1:
        raise ValueError("precision prototype did not produce eleven phases")
    write_strip(
        unit, group[args.source], group[args.target], middle,
        args.output, args.scale,
    )
    print(
        f"precision polyphase transition g{args.group} "
        f"{args.source}->{args.target}: phases={PHASE_COUNT} "
        f"middle={len(middle)} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
