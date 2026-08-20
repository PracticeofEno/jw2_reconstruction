#!/usr/bin/env python3
"""Compare original/rebuild gameplay pixels while excluding Ranker's UI."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


def inside_ranker_800_world(x: int, y: int, world_height: int) -> bool:
    """Return the audited world-render region of the logical 800x600 client."""
    if y >= world_height:
        return False
    if x < 250 and y < 80:
        return False
    if x > 530 and y > 390:
        return False
    return True


def validated_diff_path(path: Path) -> Path:
    resolved = path.resolve()
    repository_root = Path(__file__).resolve().parents[3]
    png_root = (repository_root / "debug_artifacts" / "png").resolve()
    try:
        resolved.relative_to(png_root)
    except ValueError as error:
        raise ValueError(
            f"diagnostic PNG must stay under {png_root}: {resolved}") from error
    return resolved


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("original", type=Path)
    parser.add_argument("rebuild", type=Path)
    parser.add_argument("--diff-output", type=Path)
    parser.add_argument("--amplify", type=int, default=8)
    parser.add_argument("--world-height", type=int, default=444)
    parser.add_argument(
        "--crop-padding", type=int,
        help="also save original/rebuild/diff crops around a non-empty bbox")
    args = parser.parse_args()

    original = Image.open(args.original).convert("RGB")
    rebuild = Image.open(args.rebuild).convert("RGB")
    if original.size != rebuild.size:
        raise ValueError(
            f"capture sizes differ: original={original.size} rebuild={rebuild.size}")
    if original.size != (800, 600):
        raise ValueError(
            f"ranker-800-world mask requires 800x600 captures: {original.size}")
    if args.amplify < 1:
        raise ValueError("--amplify must be positive")
    if not 1 <= args.world_height <= original.height:
        raise ValueError(
            f"invalid world height {args.world_height} for image height "
            f"{original.height}")
    if args.crop_padding is not None and args.crop_padding < 0:
        raise ValueError("--crop-padding must be non-negative")

    original_pixels = original.load()
    rebuild_pixels = rebuild.load()
    diff = Image.new("RGB", original.size)
    diff_pixels = diff.load()
    compared = 0
    differing = 0
    maximum_channel_delta = 0
    total_channel_delta = 0
    min_x = original.width
    min_y = original.height
    max_x = -1
    max_y = -1

    for y in range(original.height):
        for x in range(original.width):
            if not inside_ranker_800_world(x, y, args.world_height):
                continue
            compared += 1
            left = original_pixels[x, y]
            right = rebuild_pixels[x, y]
            delta = tuple(abs(a - b) for a, b in zip(left, right))
            local_max = max(delta)
            maximum_channel_delta = max(maximum_channel_delta, local_max)
            total_channel_delta += sum(delta)
            if local_max != 0:
                differing += 1
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
                diff_pixels[x, y] = tuple(
                    min(255, value * args.amplify) for value in delta)

    bbox = None if differing == 0 else [min_x, min_y, max_x, max_y]
    result = {
        "original": str(args.original.resolve()),
        "rebuild": str(args.rebuild.resolve()),
        "size": list(original.size),
        "mask": "ranker-world-ui-excluded",
        "world_height": args.world_height,
        "compared_pixels": compared,
        "exact_pixels": compared - differing,
        "differing_pixels": differing,
        "exact_percent": 100.0 * (compared - differing) / compared,
        "maximum_channel_delta": maximum_channel_delta,
        "mean_channel_delta": total_channel_delta / (compared * 3),
        "difference_bbox": bbox,
    }
    if args.diff_output is not None:
        output = validated_diff_path(args.diff_output)
        output.parent.mkdir(parents=True, exist_ok=True)
        diff.save(output)
        result["diff_output"] = str(output)
        if bbox is not None and args.crop_padding is not None:
            padding = args.crop_padding
            crop_box = (
                max(0, min_x - padding),
                max(0, min_y - padding),
                min(original.width, max_x + padding + 1),
                min(original.height, max_y + padding + 1),
            )
            crop_paths = {}
            for label, image in (
                    ("original", original),
                    ("rebuild", rebuild),
                    ("diff", diff)):
                crop_path = output.with_name(
                    f"{output.stem}_{label}_crop{output.suffix}")
                crop_path = validated_diff_path(crop_path)
                image.crop(crop_box).save(crop_path)
                crop_paths[label] = str(crop_path)
            result["crop_box"] = list(crop_box)
            result["crop_outputs"] = crop_paths
    print(json.dumps(result, indent=2))
    return 0 if differing == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
