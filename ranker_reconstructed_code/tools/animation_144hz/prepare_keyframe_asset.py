#!/usr/bin/env python3
"""Turn an imagegen keyframe strip into a strict seven-figure RGBA asset."""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np
from PIL import Image


FIGURE_COUNT = 7
MINIMUM_FIGURE_AREA = 100


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--background-distance", type=float, default=10.0)
    args = parser.parse_args()

    rgb = np.asarray(Image.open(args.input).convert("RGB"))
    border = np.concatenate((rgb[0], rgb[-1], rgb[:, 0], rgb[:, -1]), axis=0)
    background = np.median(border.astype(np.int16), axis=0)
    distance = np.linalg.norm(rgb.astype(np.int16) - background, axis=2)
    foreground = (distance > args.background_distance).astype(np.uint8)
    count, labels, stats, _centroids = cv2.connectedComponentsWithStats(
        foreground, connectivity=8
    )
    figures = [
        label
        for label in range(1, count)
        if int(stats[label, cv2.CC_STAT_AREA]) >= MINIMUM_FIGURE_AREA
    ]
    figures.sort(key=lambda label: int(stats[label, cv2.CC_STAT_LEFT]))
    if len(figures) != FIGURE_COUNT:
        raise ValueError(f"expected {FIGURE_COUNT} figures, found {len(figures)}")

    alpha = np.isin(labels, figures).astype(np.uint8) * 255
    rgba = np.dstack((rgb, alpha))
    rgba[alpha == 0, :3] = 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(args.output, optimize=True)
    areas = [int(stats[label, cv2.CC_STAT_AREA]) for label in figures]
    print(
        f"prepared {args.output.name}: figures={len(figures)} "
        f"areas={areas} background={background.astype(int).tolist()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
