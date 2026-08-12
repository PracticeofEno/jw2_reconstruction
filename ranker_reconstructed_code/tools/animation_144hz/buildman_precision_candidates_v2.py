#!/usr/bin/env python3
"""Generate BuildMan turn candidates on the exact 12-phase 60->144 clock."""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import sys
from pathlib import Path

from PIL import Image


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


ROUTES = {
    (2, 3): {
        "A": ("direction_first", [2, 5, 4, 3]),
        "B": ("action_first", [2, 1, 0, 3]),
        "C": ("diagonal", [2, 4, 3]),
    },
    (5, 6): {
        "A": ("direction_first", [5, 8, 7, 6]),
        "B": ("action_first", [5, 4, 3, 6]),
        "C": ("diagonal", [5, 7, 6]),
    },
    (8, 9): {
        "A": ("direction_first", [8, 11, 10, 9]),
        "B": ("action_first", [8, 7, 6, 9]),
        "C": ("diagonal", [8, 10, 9]),
    },
    (11, 12): {
        "A": ("direction_first", [11, 14, 13, 12]),
        "B": ("action_first", [11, 10, 9, 12]),
        "C": ("diagonal", [11, 13, 12]),
    },
    (14, 0): {
        "A": ("direction_first", [14, 11, 8, 5, 2, 1, 0]),
        "B": ("action_first", [14, 13, 12, 9, 6, 3, 0]),
        "C": ("diagonal", [14, 13, 10, 7, 4, 1, 0]),
    },
}


Dots = dict[tuple[int, int], int]


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


def sequence_metrics(frames: list[Dots]) -> dict[str, object]:
    silhouettes = [body_pixels(frame) for frame in frames]
    changes = [
        len(source.symmetric_difference(target))
        for source, target in zip(silhouettes, silhouettes[1:])
    ]
    centers = [centroid(silhouette) for silhouette in silhouettes]
    speeds = [
        math.dist(source, target)
        for source, target in zip(centers, centers[1:])
    ]
    accelerations = [
        abs(source - target) for source, target in zip(speeds, speeds[1:])
    ]
    mean_change = sum(changes) / max(1, len(changes))
    return {
        "body_symmetric_difference": changes,
        "max_to_mean_body_change": (
            max(changes, default=0) / mean_change if mean_change else 0.0
        ),
        "centroid_step": speeds,
        "maximum_centroid_step": max(speeds, default=0.0),
        "maximum_centroid_acceleration": max(accelerations, default=0.0),
    }


def pair_frames(
    group: list[object], source: int, target: int,
    cache: dict[tuple[int, int], list[Dots]],
) -> list[Dots]:
    key = (source, target)
    if key in cache:
        return cache[key]
    reverse_key = (target, source)
    if reverse_key in cache:
        cache[key] = list(reversed(cache[reverse_key]))
        return cache[key]
    print(f"analyzing original pose pair {source}->{target}", flush=True)
    cache[key] = precision.precision_frames(group[source], group[target])
    return cache[key]


def resample_route(
    group: list[object], route: list[int],
    cache: dict[tuple[int, int], list[Dots]],
) -> tuple[list[Dots], list[dict[str, int | str]]]:
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=5)
    args = parser.parse_args()

    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    group = unit.groups[0]
    valid_tokens = {1}
    valid_tokens.update(
        point[2] for frame in group for point in assets.frame_points(frame)
    )
    cache: dict[tuple[int, int], list[Dots]] = {}
    transitions = []
    for (source, target), routes in ROUTES.items():
        candidates = []
        for code, (mode, route) in routes.items():
            middle, samples = resample_route(group, route, cache)
            if len(middle) != precision.PHASE_COUNT - 1:
                raise ValueError("candidate did not produce eleven phases")
            if any(
                token not in valid_tokens
                for dots in middle for token in dots.values()
            ):
                raise ValueError("candidate introduced a non-TRC palette token")
            maximum_components = max(
                len(assets.dot_components(endpoint(group[index]), False))
                for index in route
            )
            if any(
                len(assets.dot_components(dots, False)) > maximum_components
                for dots in middle
            ):
                raise ValueError("candidate introduced an excess body component")
            output = args.output_dir / (
                f"g00_{source:04d}_to_{target:04d}_{code}.png"
            )
            precision.write_strip(
                unit, group[source], group[target], middle, output, args.scale
            )
            with Image.open(output) as image:
                cell_width = image.width // (precision.PHASE_COUNT + 1)
                cell_height = image.height
            frames = [endpoint(group[source]), *middle, endpoint(group[target])]
            candidates.append({
                "code": code,
                "mode": mode,
                "route": route,
                "samples": samples,
                "image": str(output.resolve()),
                "cell_width": cell_width,
                "cell_height": cell_height,
                "metrics": sequence_metrics(frames),
            })
            print(f"candidate {source}->{target} {code}: {route}", flush=True)
        transitions.append({
            "key": f"0:0:{source}:{target}",
            "group": 0,
            "source": source,
            "target": target,
            "candidates": candidates,
        })

    document = {
        "schema": 2,
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "source_rate_hz": 60,
        "target_rate_hz": 144,
        "common_phase_rate_hz": 720,
        "source_phase_count": 12,
        "target_phase_step": 5,
        "phase_count_per_transition": 12,
        "intermediate_count_per_transition": 11,
        "transitions": transitions,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        f"BuildMan precision v2 candidates complete "
        f"transitions={len(transitions)} candidates={sum(len(t['candidates']) for t in transitions)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
