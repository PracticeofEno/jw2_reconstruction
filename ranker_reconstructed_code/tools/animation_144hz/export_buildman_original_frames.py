#!/usr/bin/env python3
"""Export every original BuildMan sprite frame directly from Jw2_09.trc."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw


DIRECTORY = Path(__file__).resolve().parent


def load_assets() -> object:
    path = DIRECTORY / "unit_sprite_assets.py"
    spec = importlib.util.spec_from_file_location("unit_sprite_assets", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


assets = load_assets()


def render_frame(
    unit: object, frame: object, scale: int, checkerboard: bool,
) -> Image.Image:
    dots = assets.endpoint_dots(frame)
    left = min(x for x, _y in dots)
    top = min(y for _x, y in dots)
    right = max(x for x, _y in dots) + 1
    bottom = max(y for _x, y in dots) + 1
    margin = 4
    width = right - left + margin * 2
    height = bottom - top + margin * 2
    canvas = bytearray(width * height * 4)
    if checkerboard:
        assets.fill_checkerboard(canvas, width, height)
    assets.draw_morph_dots(
        canvas, width, height, dots, unit.palette,
        margin - left, margin - top,
    )
    scaled_width, scaled_height, rgba = assets.scale_rgba_nearest(
        bytes(canvas), width, height, scale
    )
    return Image.frombytes("RGBA", (scaled_width, scaled_height), rgba)


def write_contact_sheet(
    images: list[Image.Image], group: int, output: Path, columns: int,
) -> None:
    label_height = 24
    cell_width = max(image.width for image in images) + 12
    cell_height = max(image.height for image in images) + label_height + 8
    columns = max(1, min(columns, len(images)))
    rows = math.ceil(len(images) / columns)
    sheet = Image.new(
        "RGBA", (cell_width * columns, cell_height * rows),
        (28, 32, 30, 255),
    )
    draw = ImageDraw.Draw(sheet)
    for index, image in enumerate(images):
        column = index % columns
        row = index // columns
        x = column * cell_width + (cell_width - image.width) // 2
        y = row * cell_height + label_height
        sheet.alpha_composite(image, (x, y))
        draw.text(
            (column * cell_width + 5, row * cell_height + 5),
            f"g{group:02d} f{index:04d}",
            fill=(255, 255, 255, 255),
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=6)
    parser.add_argument("--columns", type=int, default=10)
    args = parser.parse_args()

    archive = assets.TrcArchive(args.archive)
    unit = assets.decode_unit_record(archive, 0)
    if unit.unit_name != "BuildMan":
        raise ValueError(f"unit type 0 is no longer BuildMan: {unit.unit_name}")

    args.output.mkdir(parents=True, exist_ok=True)
    manifest_groups = []
    total_frames = 0
    for group_index, frames in enumerate(unit.groups):
        if not frames:
            manifest_groups.append({
                "group": group_index,
                "frame_count": 0,
                "folder": None,
                "contact_sheet": None,
            })
            continue
        group_folder = args.output / f"group_{group_index:02d}"
        group_folder.mkdir(parents=True, exist_ok=True)
        transparent_folder = group_folder / "transparent_body"
        transparent_folder.mkdir(parents=True, exist_ok=True)
        checker_images = []
        frame_entries = []
        for frame_index, frame in enumerate(frames):
            transparent = render_frame(unit, frame, args.scale, False)
            checker = render_frame(unit, frame, args.scale, True)
            filename = f"BuildMan_g{group_index:02d}_f{frame_index:04d}.png"
            checker.save(group_folder / filename)
            transparent.save(transparent_folder / filename)
            checker_images.append(checker)
            frame_entries.append({
                "frame": frame_index,
                "file": filename,
                "transparent_body_file": f"transparent_body/{filename}",
                "width": frame.width,
                "height": frame.height,
                "offset_x": frame.offset_x,
                "offset_y": frame.offset_y,
                "opaque_pixel_count": len(assets.endpoint_dots(frame)),
            })
        sheet_name = f"BuildMan_group_{group_index:02d}_all_frames.png"
        write_contact_sheet(
            checker_images, group_index, args.output / sheet_name, args.columns
        )
        manifest_groups.append({
            "group": group_index,
            "frame_count": len(frames),
            "folder": group_folder.name,
            "contact_sheet": sheet_name,
            "frames": frame_entries,
        })
        total_frames += len(frames)
        print(
            f"exported BuildMan group {group_index:02d}: {len(frames)} frames",
            flush=True,
        )

    manifest = {
        "schema": 1,
        "source": str(args.archive),
        "source_sha256": hashlib.sha256(archive.data).hexdigest(),
        "unit_type": 0,
        "unit_name": unit.unit_name,
        "frame_kind": "original TRC indexed sprites; no generated intermediates",
        "png_scale": args.scale,
        "total_frames": total_frames,
        "groups": manifest_groups,
    }
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (args.output / "README.txt").write_text(
        "Build Man original animation frames\n"
        "\n"
        "All images in this folder were decoded directly from Jw2_09.trc.\n"
        "No 144Hz intermediate frame is included.\n"
        "Each group_XX folder contains checkerboard individual PNG files.\n"
        "Its transparent_body subfolder contains body-only transparent PNGs;\n"
        "token-1 ground shadows require a background and are visible in the\n"
        "checkerboard PNGs and group contact sheets.\n"
        "BuildMan_group_XX_all_frames.png shows that group's frames together.\n"
        f"Total original frames: {total_frames}\n",
        encoding="utf-8",
    )
    print(
        f"BuildMan original export complete frames={total_frames} "
        f"output={args.output}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
