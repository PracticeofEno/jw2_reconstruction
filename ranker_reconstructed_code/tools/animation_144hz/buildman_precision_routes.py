#!/usr/bin/env python3
"""Build 12-phase BuildMan turn candidates through original pose waypoints."""

from __future__ import annotations

import argparse
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


ROUTES = {
    (2, 3): [2, 5, 4, 3],
    (5, 6): [5, 8, 7, 6],
    (8, 9): [8, 11, 10, 9],
    (11, 12): [11, 14, 13, 12],
    (14, 0): [14, 11, 8, 5, 2, 1, 0],
}


def frame_dots(frame: object) -> dict[tuple[int, int], int]:
    return assets.endpoint_dots(frame)


def route_frames(
    group: list[object], route: list[int]
) -> tuple[list[dict[tuple[int, int], int]], list[dict[str, int | str]]]:
    pair_phases: dict[tuple[int, int], list[dict[tuple[int, int], int]]] = {}
    for source, target in zip(route, route[1:]):
        pair_phases[(source, target)] = precision.precision_frames(
            group[source], group[target]
        )

    segment_count = len(route) - 1
    result: list[dict[tuple[int, int], int]] = []
    samples: list[dict[str, int | str]] = []
    for global_phase in range(1, precision.PHASE_COUNT):
        scaled = global_phase * segment_count
        segment = scaled // precision.PHASE_COUNT
        local_phase = scaled % precision.PHASE_COUNT
        if local_phase == 0:
            frame_index = route[segment]
            dots = frame_dots(group[frame_index])
            samples.append({
                "global_phase": global_phase,
                "kind": "original_waypoint",
                "original_frame": frame_index,
            })
        else:
            source = route[segment]
            target = route[segment + 1]
            dots = pair_phases[(source, target)][local_phase - 1]
            samples.append({
                "global_phase": global_phase,
                "kind": "local_polyphase",
                "source": source,
                "target": target,
                "local_phase": local_phase,
            })
        result.append(dots)
    return result, samples


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
    records = []
    valid_tokens = {1}
    valid_tokens.update(
        point[2] for frame in group for point in assets.frame_points(frame)
    )
    for (source, target), route in ROUTES.items():
        middle, samples = route_frames(group, route)
        if len(middle) != precision.PHASE_COUNT - 1:
            raise ValueError("route did not produce eleven intermediate phases")
        if any(token not in valid_tokens for dots in middle for token in dots.values()):
            raise ValueError("route introduced a non-TRC palette token")
        maximum_components = max(
            len(assets.dot_components(frame_dots(group[index]), False))
            for index in route
        )
        if any(
            len(assets.dot_components(dots, False)) > maximum_components
            for dots in middle
        ):
            raise ValueError("route introduced an excess body component")
        output = args.output_dir / f"g00_{source:04d}_to_{target:04d}.png"
        precision.write_strip(
            unit, group[source], group[target], middle, output, args.scale
        )
        records.append({
            "group": 0,
            "source": source,
            "target": target,
            "route": route,
            "phase_count": precision.PHASE_COUNT,
            "intermediate_count": len(middle),
            "samples": samples,
            "image": str(output.resolve()),
        })
        print(f"precision route {source}->{target}: {route}", flush=True)
    document = {
        "schema": 2,
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "source_rate_hz": 60,
        "target_rate_hz": 144,
        "common_phase_rate_hz": 720,
        "source_phase_count": 12,
        "target_phase_step": 5,
        "transitions": records,
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"BuildMan precision routes complete transitions={len(records)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
