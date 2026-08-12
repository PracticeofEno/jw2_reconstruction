#!/usr/bin/env python3
"""Compile every valid BuildMan group-0 transition from its 2-D pose graph."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import importlib.util
import json
import math
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


Dots = dict[tuple[int, int], int]
GROUP = 0
ROW_WIDTH = 3


def endpoint(frame: object) -> Dots:
    return assets.endpoint_dots(frame)


def body_pixels(dots: Dots) -> set[tuple[int, int]]:
    return {point for point, token in dots.items() if token != 1}


def centroid(points: set[tuple[int, int]]) -> tuple[float, float]:
    if not points:
        return 0.0, 0.0
    return (
        sum(point[0] for point in points) / len(points),
        sum(point[1] for point in points) / len(points),
    )


def route_for(source: int, target: int) -> list[int]:
    source_row, source_phase = divmod(source, ROW_WIDTH)
    target_row, target_phase = divmod(target, ROW_WIDTH)
    route = [source]
    row_step = 1 if target_row > source_row else -1
    for row in range(source_row + row_step, target_row + row_step, row_step):
        route.append(row * ROW_WIDTH + source_phase)
    phase_step = 1 if target_phase > source_phase else -1
    for phase in range(source_phase + phase_step, target_phase + phase_step, phase_step):
        route.append(target_row * ROW_WIDTH + phase)
    return route


def pair_frames(
    group: list[object], source: int, target: int,
    cache: dict[tuple[int, int], list[Dots]],
) -> list[Dots]:
    key = (source, target)
    if key in cache:
        return cache[key]
    reverse = (target, source)
    if reverse in cache:
        cache[key] = list(reversed(cache[reverse]))
        return cache[key]
    print(f"analyzing adjacent original poses {source}->{target}", flush=True)
    cache[key] = precision.precision_frames(group[source], group[target])
    return cache[key]


def resample_route(
    group: list[object], route: list[int],
    cache: dict[tuple[int, int], list[Dots]],
) -> tuple[list[Dots], list[dict[str, int | str]]]:
    if len(route) < 2:
        raise ValueError("a transition route needs two endpoints")
    for source, target in zip(route, route[1:]):
        pair_frames(group, source, target, cache)

    segment_count = len(route) - 1
    middle = []
    samples = []
    for global_phase in range(1, precision.PHASE_COUNT):
        scaled = global_phase * segment_count
        segment = scaled // precision.PHASE_COUNT
        local_phase = scaled % precision.PHASE_COUNT
        if local_phase == 0:
            original = route[segment]
            middle.append(endpoint(group[original]))
            samples.append({
                "global_phase": global_phase,
                "kind": "original_waypoint",
                "original_frame": original,
            })
            continue
        source = route[segment]
        target = route[segment + 1]
        middle.append(pair_frames(group, source, target, cache)[local_phase - 1])
        samples.append({
            "global_phase": global_phase,
            "kind": "local_polyphase",
            "source": source,
            "target": target,
            "local_phase": local_phase,
        })
    return middle, samples


def metrics(frames: list[Dots]) -> dict[str, float | int]:
    bodies = [body_pixels(frame) for frame in frames]
    changes = [
        len(source.symmetric_difference(target))
        for source, target in zip(bodies, bodies[1:])
    ]
    centers = [centroid(body) for body in bodies]
    speeds = [
        math.dist(source, target)
        for source, target in zip(centers, centers[1:])
    ]
    mean_change = sum(changes) / max(1, len(changes))
    return {
        "maximum_body_change": max(changes, default=0),
        "maximum_to_mean_body_change": (
            max(changes, default=0) / mean_change if mean_change else 0.0
        ),
        "maximum_centroid_step": max(speeds, default=0.0),
        "maximum_centroid_acceleration": max(
            (abs(a - b) for a, b in zip(speeds, speeds[1:])), default=0.0
        ),
    }


def encoded_dots(dots: Dots) -> list[list[int]]:
    return [
        [x, y, token]
        for (x, y), token in sorted(dots.items(), key=lambda item: (item[0][1], item[0][0]))
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    group = unit.groups[GROUP]
    if len(group) != 15:
        raise ValueError("BuildMan group 0 no longer has the expected 5x3 pose grid")
    transitions = [
        (source, target)
        for group_index, source, target in assets.animation_transition_pairs(unit)
        if group_index == GROUP
    ]
    forbidden = {(2, 3), (5, 6), (8, 9), (11, 12), (14, 0)}
    if forbidden.intersection(transitions):
        raise ValueError("flat direction-row boundary survived pose-graph recovery")

    valid_tokens = {1}
    valid_tokens.update(
        point[2] for frame in group for point in assets.frame_points(frame)
    )
    pair_cache: dict[tuple[int, int], list[Dots]] = {}
    overrides = []
    diagnostics = []
    for ordinal, (source, target) in enumerate(transitions, 1):
        route = route_for(source, target)
        middle, samples = resample_route(group, route, pair_cache)
        if len(middle) != assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
            raise ValueError("precision route produced the wrong phase count")
        if any(
            token not in valid_tokens
            for frame in middle for token in frame.values()
        ):
            raise ValueError("precision route introduced a non-TRC palette token")
        maximum_components = max(
            len(assets.dot_components(endpoint(group[index]), False))
            for index in route
        )
        if any(
            len(assets.dot_components(frame, False)) > maximum_components
            for frame in middle
        ):
            raise ValueError("precision route introduced an excess body component")
        overrides.append({
            "unit_type": 0,
            "group": GROUP,
            "source": source,
            "target": target,
            "frames": [encoded_dots(frame) for frame in middle],
        })
        frames = [endpoint(group[source]), *middle, endpoint(group[target])]
        diagnostics.append({
            "source": source,
            "target": target,
            "route": route,
            "samples": samples,
            "metrics": metrics(frames),
        })
        if ordinal % 20 == 0 or ordinal == len(transitions):
            print(f"compiled {ordinal}/{len(transitions)} valid transitions", flush=True)

    source_sha256 = hashlib.sha256(archive.data).hexdigest()
    source_crc32 = binascii.crc32(archive.data) & 0xFFFFFFFF
    document = {
        "schema": 2,
        "source_archive_sha256": source_sha256,
        "source_archive_crc32": f"{source_crc32:08x}",
        "source_rate_hz": 60,
        "target_rate_hz": 144,
        "common_phase_rate_hz": 720,
        "phase_count_per_transition": assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
        "intermediate_frames_per_transition": (
            assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
        ),
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "group": GROUP,
        "pose_grid": {"direction_rows": 5, "action_phases": ROW_WIDTH},
        "forbidden_flat_boundaries": [list(pair) for pair in sorted(forbidden)],
        "overrides": overrides,
    }
    manifest = {
        "schema": 2,
        "source_archive_sha256": source_sha256,
        "source_archive_crc32": f"{source_crc32:08x}",
        "source_rate_hz": 60,
        "target_rate_hz": 144,
        "common_phase_rate_hz": 720,
        "simulation_source_interval_ms": 45,
        "runtime_144hz_quantized_phases_at_45ms": [0, 2, 4, 6, 7, 9, 11, 12],
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "group": GROUP,
        "transition_count": len(diagnostics),
        "adjacent_pose_analysis_count": len({
            tuple(sorted(pair)) for pair in pair_cache
        }),
        "transitions": diagnostics,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"BuildMan precision overrides complete transitions={len(overrides)} "
        f"adjacent_analyses={manifest['adjacent_pose_analysis_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
