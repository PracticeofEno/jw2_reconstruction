#!/usr/bin/env python3
"""Compare completed original/rebuilt logical RGB565 world buffers."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct


def components(pixel: int) -> tuple[int, int, int]:
    return ((pixel >> 11) & 0x1F, (pixel >> 5) & 0x3F, pixel & 0x1F)


def validate_diagnostic_png(path: Path, repository_root: Path) -> None:
    png_root = (repository_root / "debug_artifacts" / "png").resolve()
    try:
        path.resolve().relative_to(png_root)
    except ValueError as error:
        raise ValueError(
            f"diagnostic PNG must stay below {png_root}: {path}") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("original")
    parser.add_argument("rebuild")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--world-height", type=int, default=444)
    parser.add_argument("--output-json")
    parser.add_argument("--diff-output")
    parser.add_argument("--original-output")
    parser.add_argument("--rebuild-output")
    parser.add_argument("--amplify", type=int, default=8)
    args = parser.parse_args()

    original_path = Path(args.original).resolve()
    rebuild_path = Path(args.rebuild).resolve()
    original_bytes = original_path.read_bytes()
    rebuild_bytes = rebuild_path.read_bytes()
    expected_size = args.width * args.height * 2
    if not 1 <= args.world_height <= args.height:
        raise ValueError(
            f"invalid world height {args.world_height} for surface "
            f"height {args.height}")
    if len(original_bytes) != expected_size or len(rebuild_bytes) != expected_size:
        raise ValueError(
            f"expected {expected_size} bytes for {args.width}x{args.height}; "
            f"original={len(original_bytes)} rebuild={len(rebuild_bytes)}")
    original = struct.unpack(f"<{args.width * args.height}H", original_bytes)
    rebuild = struct.unpack(f"<{args.width * args.height}H", rebuild_bytes)

    pair_counts: collections.Counter[tuple[int, int]] = collections.Counter()
    component_deltas: collections.Counter[tuple[int, int, int]] = (
        collections.Counter())
    samples: list[dict[str, int | list[int]]] = []
    compared = 0
    exact = 0
    min_x = args.width
    min_y = args.height
    max_x = -1
    max_y = -1
    different_indices: list[int] = []
    for y in range(args.world_height):
        for x in range(args.width):
            if x < 250 and y < 80:
                continue
            if x > 530 and y > 390:
                continue
            index = y * args.width + x
            left = original[index]
            right = rebuild[index]
            compared += 1
            if left == right:
                exact += 1
                continue
            different_indices.append(index)
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
            pair_counts[(left, right)] += 1
            left_components = components(left)
            right_components = components(right)
            delta = tuple(
                right_components[i] - left_components[i] for i in range(3))
            component_deltas[delta] += 1
            if len(samples) < 32:
                samples.append({
                    "x": x,
                    "y": y,
                    "original": left,
                    "rebuild": right,
                    "delta": list(delta),
                })

    mismatch = compared - exact
    result: dict[str, object] = {
        "original": str(original_path),
        "rebuild": str(rebuild_path),
        "dimensions": [args.width, args.height],
        "mask": "ranker-world-ui-excluded",
        "world_height": args.world_height,
        "original_sha256": hashlib.sha256(original_bytes).hexdigest(),
        "rebuild_sha256": hashlib.sha256(rebuild_bytes).hexdigest(),
        "compared": compared,
        "exact": exact,
        "mismatch": mismatch,
        "exact_ratio": exact / compared if compared else 1.0,
        "difference_bbox": None if mismatch == 0 else
            [min_x, min_y, max_x + 1, max_y + 1],
        "component_deltas": [
            {"delta": list(delta), "count": count}
            for delta, count in component_deltas.most_common(20)
        ],
        "pixel_pairs": [
            {"original": left, "rebuild": right, "count": count}
            for (left, right), count in pair_counts.most_common(20)
        ],
        "samples": samples,
    }

    repository_root = Path(__file__).resolve().parents[3]
    pair_outputs = (
        (args.original_output, original, "original_output"),
        (args.rebuild_output, rebuild, "rebuild_output"),
    )
    for requested_path, pixels, result_key in pair_outputs:
        if not requested_path:
            continue
        from PIL import Image

        image_path = Path(requested_path).resolve()
        validate_diagnostic_png(image_path, repository_root)
        rgb = bytearray(args.width * args.height * 3)
        for index, pixel in enumerate(pixels):
            red, green, blue = components(pixel)
            base = index * 3
            rgb[base] = red * 255 // 31
            rgb[base + 1] = green * 255 // 63
            rgb[base + 2] = blue * 255 // 31
        image_path.parent.mkdir(parents=True, exist_ok=True)
        Image.frombytes("RGB", (args.width, args.height), bytes(rgb)).save(
            image_path)
        result[result_key] = str(image_path)
    if args.diff_output:
        from PIL import Image

        diff_path = Path(args.diff_output).resolve()
        validate_diagnostic_png(diff_path, repository_root)
        diff = bytearray(args.width * args.height * 3)
        for index in different_indices:
            left = components(original[index])
            right = components(rebuild[index])
            base = index * 3
            diff[base] = min(255, abs(right[0] - left[0]) * args.amplify * 8)
            diff[base + 1] = min(
                255, abs(right[1] - left[1]) * args.amplify * 4)
            diff[base + 2] = min(
                255, abs(right[2] - left[2]) * args.amplify * 8)
        diff_path.parent.mkdir(parents=True, exist_ok=True)
        Image.frombytes("RGB", (args.width, args.height), bytes(diff)).save(diff_path)
        result["diff_output"] = str(diff_path)

    encoded = json.dumps(result, indent=2)
    print(encoded)
    if args.output_json:
        output_json = Path(args.output_json).resolve()
        artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                         "replay_debug" / "artifacts").resolve()
        try:
            output_json.relative_to(artifact_root)
        except ValueError as error:
            raise ValueError(
                f"result JSON must stay below {artifact_root}: "
                f"{output_json}") from error
        output_json.parent.mkdir(parents=True, exist_ok=True)
        output_json.write_text(encoded + "\n", encoding="utf-8")
    return 0 if mismatch == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
