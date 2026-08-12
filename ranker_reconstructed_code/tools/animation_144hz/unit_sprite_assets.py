#!/usr/bin/env python3
"""Inspect and export Ranker unit sprites without modifying the original TRC.

The animation-144Hz work uses this tool as the authoritative asset inventory.
It intentionally parses JW2_09.TRC directly so exporting reference art cannot
alter the in-game resource store or any lockstep simulation state.
"""

from __future__ import annotations

import argparse
import binascii
import functools
import hashlib
import json
import math
import multiprocessing
import os
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


TRC_HEADER_BYTES = 0x20
TRC_DIRECTORY_ENTRY_BYTES = 0x20
UNIT_DEFINITION_BYTES = 0x24BC
UNIT_NAME_OFFSET = 0x10C
UNIT_NAME_BYTES = 0x40
UNIT_IMAGE_COUNT_OFFSET = 0x2214
UNIT_IMAGE_GROUPS = 14
UNIT_RECORD_COUNT = 0xAA
PALETTE_BYTES = 0x400
RESOURCE_HEADER_BYTES = 0x20
VISUAL_ARCHIVE_MAGIC = b"R144RFA\0"
VISUAL_ARCHIVE_VERSION = 2
# Exact 60 -> 144 polyphase timebase: 720 / 60 = 12 source phases and
# 720 / 144 = five phases per presentation sample.
VISUAL_ARCHIVE_INTERVAL_COUNT = 12
VISUAL_ARCHIVE_INTERMEDIATE_COUNT = 11
VISUAL_ARCHIVE_HEADER = struct.Struct("<8sIIIIQQQQ32sIIII")
VISUAL_ARCHIVE_DIRECTORY_ENTRY = struct.Struct("<HBBHHhhHHQIIII")
DEFAULT_KEYFRAME_OVERRIDES = (
    Path(__file__).resolve().parents[2]
    / "assets"
    / "animation_144hz"
    / "keyframes"
    / "buildman_precision_v2.json"
)


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def signed_u32(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


@dataclass(frozen=True)
class TrcEntry:
    name: str
    relative_offset: int
    original_size: int
    stored_size: int
    method: int


class TrcArchive:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < TRC_HEADER_BYTES or self.data[:4] != b"TRC\x1a":
            raise ValueError(f"not a TRC archive: {path}")
        self.directory_slots = read_u32(self.data, 4)
        self.active_entries = read_u32(self.data, 8)
        self.data_offset = read_u32(self.data, 12)
        directory_end = TRC_HEADER_BYTES + self.directory_slots * TRC_DIRECTORY_ENTRY_BYTES
        if self.active_entries > self.directory_slots or not (
            directory_end <= self.data_offset <= len(self.data)
        ):
            raise ValueError(f"invalid TRC directory: {path}")

    def entry(self, index: int) -> TrcEntry:
        if not 0 <= index < self.active_entries:
            raise IndexError(index)
        offset = TRC_HEADER_BYTES + index * TRC_DIRECTORY_ENTRY_BYTES
        raw_name = self.data[offset : offset + 12].split(b"\0", 1)[0]
        return TrcEntry(
            name=raw_name.decode("cp949", "replace"),
            relative_offset=read_u32(self.data, offset + 0x0C),
            original_size=read_u32(self.data, offset + 0x10),
            stored_size=read_u32(self.data, offset + 0x14),
            method=read_u16(self.data, offset + 0x1A),
        )

    def record(self, index: int) -> tuple[TrcEntry, bytes]:
        entry = self.entry(index)
        start = self.data_offset + entry.relative_offset
        end = start + entry.stored_size
        if start > len(self.data) or end > len(self.data):
            raise ValueError(f"record {index} extends past archive")
        stored = self.data[start:end]
        if entry.method == 0:
            payload = stored
        elif entry.method == 2:
            payload = zlib.decompress(stored)
        else:
            raise ValueError(f"record {index} has unsupported method {entry.method}")
        if len(payload) != entry.original_size:
            raise ValueError(
                f"record {index} size mismatch: {len(payload)} != {entry.original_size}"
            )
        return entry, payload


@dataclass(frozen=True)
class SpriteFrame:
    metadata: tuple[int, int, int, int, int, int]
    payload: bytes

    @property
    def width(self) -> int:
        return self.metadata[0]

    @property
    def height(self) -> int:
        return self.metadata[1]

    @property
    def offset_x(self) -> int:
        return signed_u32(self.metadata[2])

    @property
    def offset_y(self) -> int:
        return signed_u32(self.metadata[3])

    def decode_indices(self) -> bytearray:
        pixels = bytearray(self.width * self.height)
        cursor = 0
        for y in range(self.height):
            if cursor + 2 > len(self.payload):
                raise ValueError("truncated RLE row header")
            row_bytes = read_u16(self.payload, cursor)
            cursor += 2
            row_end = cursor + row_bytes
            if row_end > len(self.payload):
                raise ValueError("truncated RLE row")
            x = 0
            while x < self.width and cursor < row_end:
                token = self.payload[cursor]
                cursor += 1
                if token == 0:
                    if cursor >= row_end:
                        break
                    x += self.payload[cursor]
                    cursor += 1
                    continue
                if x < self.width:
                    pixels[y * self.width + x] = token
                x += 1
            cursor = row_end
        return pixels


@dataclass
class UnitRecord:
    type_id: int
    record_name: str
    unit_name: str
    definition: bytes
    palette: bytes
    groups: list[list[SpriteFrame]]
    record_bytes: int

    @property
    def image_count(self) -> int:
        return sum(len(group) for group in self.groups)


def decode_unit_record(archive: TrcArchive, type_id: int) -> UnitRecord:
    entry, payload = archive.record(type_id)
    definition = payload[:UNIT_DEFINITION_BYTES]
    if len(definition) < UNIT_DEFINITION_BYTES:
        definition = definition + bytes(UNIT_DEFINITION_BYTES - len(definition))
    raw_name = definition[UNIT_NAME_OFFSET : UNIT_NAME_OFFSET + UNIT_NAME_BYTES]
    unit_name = raw_name.split(b"\0", 1)[0].decode("cp949", "replace")
    counts = list(
        struct.unpack_from(f"<{UNIT_IMAGE_GROUPS}I", definition, UNIT_IMAGE_COUNT_OFFSET)
    )
    if not any(counts):
        return UnitRecord(
            type_id, entry.name, unit_name, definition, b"", [[] for _ in counts], len(payload)
        )
    cursor = UNIT_DEFINITION_BYTES
    if cursor + PALETTE_BYTES > len(payload):
        raise ValueError(f"unit {type_id} is missing its palette")
    palette = payload[cursor : cursor + PALETTE_BYTES]
    cursor += PALETTE_BYTES
    groups: list[list[SpriteFrame]] = []
    for count in counts:
        frames: list[SpriteFrame] = []
        for _ in range(count):
            if cursor + RESOURCE_HEADER_BYTES > len(payload):
                raise ValueError(f"unit {type_id} has a truncated resource header")
            metadata = struct.unpack_from("<6I", payload, cursor)
            payload_size = read_u32(payload, cursor + 0x18)
            cursor += RESOURCE_HEADER_BYTES
            end = cursor + payload_size
            if end > len(payload):
                raise ValueError(f"unit {type_id} has a truncated resource payload")
            frames.append(SpriteFrame(metadata, payload[cursor:end]))
            cursor = end
        groups.append(frames)
    return UnitRecord(
        type_id, entry.name, unit_name, definition, palette, groups, len(payload)
    )


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    if len(rgba) != width * height * 4:
        raise ValueError("RGBA buffer size does not match dimensions")
    scanlines = b"".join(
        b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height)
    )
    encoded = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(scanlines, 9))
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)


def palette_rgba(palette: bytes, index: int) -> tuple[int, int, int, int]:
    if index == 0:
        return 0, 0, 0, 0
    base = index * 4
    if base + 2 >= len(palette):
        return 255, 0, 255, 255
    return palette[base], palette[base + 1], palette[base + 2], 255


def draw_frame(
    canvas: bytearray,
    canvas_width: int,
    canvas_height: int,
    frame: SpriteFrame,
    palette: bytes,
    anchor_x: int,
    anchor_y: int,
) -> None:
    indices = frame.decode_indices()
    start_x = anchor_x + frame.offset_x
    start_y = anchor_y + frame.offset_y
    for y in range(frame.height):
        target_y = start_y + y
        if not 0 <= target_y < canvas_height:
            continue
        for x in range(frame.width):
            target_x = start_x + x
            if not 0 <= target_x < canvas_width:
                continue
            index = indices[y * frame.width + x]
            if index == 0:
                continue
            pixel = (target_y * canvas_width + target_x) * 4
            canvas[pixel : pixel + 4] = bytes(palette_rgba(palette, index))


def draw_frame_preview(
    canvas: bytearray,
    canvas_width: int,
    canvas_height: int,
    frame: SpriteFrame,
    palette: bytes,
    anchor_x: int,
    anchor_y: int,
) -> None:
    """Draw like the in-game unit renderer, including token-1 ground shadow."""
    indices = frame.decode_indices()
    start_x = anchor_x + frame.offset_x
    start_y = anchor_y + frame.offset_y
    for shadow_pass in (True, False):
        for y in range(frame.height):
            target_y = start_y + y
            if not 0 <= target_y < canvas_height:
                continue
            for x in range(frame.width):
                index = indices[y * frame.width + x]
                if index == 0 or (index == 1) != shadow_pass:
                    continue
                target_x = start_x + x
                if not 0 <= target_x < canvas_width:
                    continue
                pixel = (target_y * canvas_width + target_x) * 4
                if index == 1:
                    canvas[pixel + 0] //= 2
                    canvas[pixel + 1] //= 2
                    canvas[pixel + 2] //= 2
                else:
                    canvas[pixel:pixel + 4] = bytes(
                        palette_rgba(palette, index)
                    )


def frame_bounds(frames: Iterable[SpriteFrame]) -> tuple[int, int, int, int]:
    frames = list(frames)
    if not frames:
        return 0, 0, 1, 1
    left = min(frame.offset_x for frame in frames)
    top = min(frame.offset_y for frame in frames)
    right = max(frame.offset_x + frame.width for frame in frames)
    bottom = max(frame.offset_y + frame.height for frame in frames)
    return left, top, right, bottom


def write_contact_sheet(
    unit: UnitRecord, group_index: int, output: Path, columns: int, margin: int
) -> None:
    if not 0 <= group_index < len(unit.groups):
        raise ValueError(f"invalid group {group_index}")
    frames = unit.groups[group_index]
    if not frames:
        raise ValueError(f"unit {unit.type_id} group {group_index} is empty")
    left, top, right, bottom = frame_bounds(frames)
    cell_width = max(1, right - left) + margin * 2
    cell_height = max(1, bottom - top) + margin * 2
    columns = max(1, min(columns, len(frames)))
    rows = math.ceil(len(frames) / columns)
    width = cell_width * columns
    height = cell_height * rows
    canvas = bytearray(width * height * 4)
    for index, frame in enumerate(frames):
        column = index % columns
        row = index // columns
        anchor_x = column * cell_width + margin - left
        anchor_y = row * cell_height + margin - top
        draw_frame_preview(
            canvas, width, height, frame, unit.palette, anchor_x, anchor_y
        )
    write_rgba_png(output, width, height, canvas)


def fill_checkerboard(canvas: bytearray, width: int, height: int, cell: int = 8) -> None:
    colors = ((48, 64, 56, 255), (58, 76, 66, 255))
    for y in range(height):
        for x in range(width):
            color = colors[((x // cell) + (y // cell)) & 1]
            pixel = (y * width + x) * 4
            canvas[pixel : pixel + 4] = bytes(color)


def draw_transition_layer(
    canvas: bytearray,
    canvas_width: int,
    canvas_height: int,
    frame: SpriteFrame,
    palette: bytes,
    anchor_x: int,
    anchor_y: int,
    weight_31: int,
    shadow_only: bool = False,
) -> None:
    indices = frame.decode_indices()
    start_x = anchor_x + frame.offset_x
    start_y = anchor_y + frame.offset_y
    for y in range(frame.height):
        target_y = start_y + y
        if not 0 <= target_y < canvas_height:
            continue
        for x in range(frame.width):
            target_x = start_x + x
            if not 0 <= target_x < canvas_width:
                continue
            index = indices[y * frame.width + x]
            pixel = (target_y * canvas_width + target_x) * 4
            if shadow_only:
                if index == 1:
                    canvas[pixel + 0] //= 2
                    canvas[pixel + 1] //= 2
                    canvas[pixel + 2] //= 2
                continue
            if index <= 1 or weight_31 == 0:
                continue
            red, green, blue, _ = palette_rgba(palette, index)
            inverse = 31 - weight_31
            canvas[pixel + 0] = (canvas[pixel + 0] * inverse + red * weight_31) // 31
            canvas[pixel + 1] = (canvas[pixel + 1] * inverse + green * weight_31) // 31
            canvas[pixel + 2] = (canvas[pixel + 2] * inverse + blue * weight_31) // 31
            canvas[pixel + 3] = 255


def frame_points(
    frame: SpriteFrame, flipped: bool = False
) -> list[tuple[int, int, int]]:
    indices = frame.decode_indices()
    offset_x = signed_u32(frame.metadata[4] if flipped else frame.metadata[2])
    offset_y = signed_u32(frame.metadata[5] if flipped else frame.metadata[3])
    return [
        (
            offset_x + (frame.width - x if flipped else x),
            offset_y + y,
            index,
        )
        for y in range(frame.height)
        for x in range(frame.width)
        if (index := indices[y * frame.width + x]) != 0
    ]


def point_key(x: int, y: int) -> tuple[int, int]:
    return x, y


def build_pixel_correspondence(
    source: SpriteFrame,
    target: SpriteFrame,
    flipped: bool = False,
    source_points: list[tuple[int, int, int]] | None = None,
    target_points: list[tuple[int, int, int]] | None = None,
) -> list[tuple[int, int, int, int, int, int]]:
    """Pair opaque source/target dots without inventing colours or soft edges."""
    if source_points is None:
        source_points = frame_points(source, flipped)
    if target_points is None:
        target_points = frame_points(target, flipped)
    target_at = {
        point_key(x, y): index for index, (x, y, _token) in enumerate(target_points)
    }
    used = [False] * len(target_points)
    matches: list[int | None] = [None] * len(source_points)

    # First preserve dots that occupy the same silhouette coordinate.  Shadows
    # only pair with shadows; coloured body dots only pair with coloured dots.
    for source_index, (x, y, token) in enumerate(source_points):
        candidate = target_at.get(point_key(x, y))
        if candidate is None or used[candidate]:
            continue
        target_token = target_points[candidate][2]
        if (token == 1) != (target_token == 1):
            continue
        matches[source_index] = candidate
        used[candidate] = True

    def find_near(x: int, y: int, token: int, radius_limit: int,
                  require_same_token: bool) -> int | None:
        best: tuple[int, int] | None = None
        for radius in range(1, radius_limit + 1):
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    if max(abs(dx), abs(dy)) != radius:
                        continue
                    candidate = target_at.get(point_key(x + dx, y + dy))
                    if candidate is None or used[candidate]:
                        continue
                    target_token = target_points[candidate][2]
                    if (token == 1) != (target_token == 1):
                        continue
                    if require_same_token and token != target_token:
                        continue
                    score = dx * dx + dy * dy
                    if best is None or score < best[0]:
                        best = score, candidate
            if best is not None:
                return best[1]
        return None

    # Moving limbs usually retain their palette index.  A six-dot radius is
    # enough for one original animation tick without pulling distant parts
    # (for example a weapon and a boot) through one another.
    for source_index, (x, y, token) in enumerate(source_points):
        if matches[source_index] is not None:
            continue
        candidate = find_near(x, y, token, 6, True)
        if candidate is not None:
            matches[source_index] = candidate
            used[candidate] = True

    # Palette shading can change at a moving edge, so allow a final, strictly
    # local same-class pairing independent of palette index.
    for source_index, (x, y, token) in enumerate(source_points):
        if matches[source_index] is not None:
            continue
        candidate = find_near(x, y, token, 2, False)
        if candidate is not None:
            matches[source_index] = candidate
            used[candidate] = True

    result: list[tuple[int, int, int, int, int, int]] = []
    for source_index, (sx, sy, source_token) in enumerate(source_points):
        target_index = matches[source_index]
        if target_index is None:
            result.append((sx, sy, source_token, sx, sy, 0))
        else:
            tx, ty, target_token = target_points[target_index]
            result.append((sx, sy, source_token, tx, ty, target_token))
    for target_index, (tx, ty, target_token) in enumerate(target_points):
        if not used[target_index]:
            result.append((tx, ty, 0, tx, ty, target_token))
    return result


_BAYER_4X4 = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)


def dither_rank(x: int, y: int) -> int:
    return _BAYER_4X4[((y & 3) << 2) | (x & 3)]


def lerp_coordinate(source: int, target: int, step: int) -> int:
    numerator = (target - source) * step
    rounded = (
        numerator + VISUAL_ARCHIVE_INTERVAL_COUNT // 2
        if numerator >= 0
        else numerator - VISUAL_ARCHIVE_INTERVAL_COUNT // 2
    )
    # C++ integer division truncates toward zero.
    delta = abs(rounded) // VISUAL_ARCHIVE_INTERVAL_COUNT
    return source + (delta if rounded >= 0 else -delta)


def pixel_morph_dots(
    correspondence: list[tuple[int, int, int, int, int, int]], step: int
) -> dict[tuple[int, int], int]:
    threshold = (
        step * 16 + VISUAL_ARCHIVE_INTERVAL_COUNT // 2
    ) // VISUAL_ARCHIVE_INTERVAL_COUNT
    dots: dict[tuple[int, int], int] = {}
    for sx, sy, source_token, tx, ty, target_token in correspondence:
        if source_token and target_token:
            x = lerp_coordinate(sx, tx, step)
            y = lerp_coordinate(sy, ty, step)
            token = (
                target_token
                if step * 2 >= VISUAL_ARCHIVE_INTERVAL_COUNT
                else source_token
            )
        elif source_token:
            x, y, token = sx, sy, source_token
            if dither_rank(x, y) < threshold:
                continue
        else:
            x, y, token = tx, ty, target_token
            if dither_rank(x, y) >= threshold:
                continue
        existing = dots.get((x, y), 0)
        if existing <= 1 or token > 1:
            dots[(x, y)] = token
    return dots


def correspondence_coverage(
    correspondence: list[tuple[int, int, int, int, int, int]]
) -> float:
    source_count = sum(
        source_token != 0
        for _sx, _sy, source_token, _tx, _ty, _target_token in correspondence
    )
    target_count = sum(target_token != 0 for *_head, target_token in correspondence)
    matched = sum(
        source_token != 0 and target_token != 0
        for _sx, _sy, source_token, _tx, _ty, target_token in correspondence
    )
    return (2.0 * matched) / max(1, source_count + target_count)


def rounded_fraction(numerator: int, denominator: int) -> int:
    if denominator <= 0:
        raise ValueError("rounded fraction denominator must be positive")
    magnitude = (abs(numerator) + denominator // 2) // denominator
    return magnitude if numerator >= 0 else -magnitude


def point_class_centroid(
    points: list[tuple[int, int, int]], shadow: bool
) -> tuple[int, int, int] | None:
    selected = [
        (x, y) for x, y, token in points if (token == 1) == shadow
    ]
    if not selected:
        return None
    return (
        sum(x for x, _y in selected),
        sum(y for _x, y in selected),
        len(selected),
    )


def rounded_point_class_origin(
    points: list[tuple[int, int, int]], shadow: bool
) -> tuple[int, int] | None:
    centroid = point_class_centroid(points, shadow)
    if centroid is None:
        return None
    sum_x, sum_y, count = centroid
    return rounded_fraction(sum_x, count), rounded_fraction(sum_y, count)


def chamfer_distance_and_tokens(
    width: int,
    height: int,
    seeds: dict[tuple[int, int], int],
) -> tuple[list[int], list[int]]:
    """Approximate distance and nearest palette token on an integer grid."""
    infinite = (width + height + 1) * 4
    distance = [infinite] * (width * height)
    token = [0] * (width * height)
    for (x, y), seed_token in seeds.items():
        index = y * width + x
        distance[index] = 0
        token[index] = seed_token

    def update(index: int, candidate: int, weight: int) -> None:
        candidate_distance = distance[candidate] + weight
        if candidate_distance < distance[index]:
            distance[index] = candidate_distance
            token[index] = token[candidate]

    for y in range(height):
        row = y * width
        for x in range(width):
            index = row + x
            if x:
                update(index, index - 1, 3)
            if y:
                update(index, index - width, 3)
                if x:
                    update(index, index - width - 1, 4)
                if x + 1 < width:
                    update(index, index - width + 1, 4)
    for y in range(height - 1, -1, -1):
        row = y * width
        for x in range(width - 1, -1, -1):
            index = row + x
            if x + 1 < width:
                update(index, index + 1, 3)
            if y + 1 < height:
                update(index, index + width, 3)
                if x:
                    update(index, index + width - 1, 4)
                if x + 1 < width:
                    update(index, index + width + 1, 4)
    return distance, token


def signed_chamfer_field(
    width: int,
    height: int,
    occupied: dict[tuple[int, int], int],
) -> tuple[list[int], list[int]]:
    distance_to_inside, nearest_token = chamfer_distance_and_tokens(
        width, height, occupied
    )
    outside = {
        (x, y): 1
        for y in range(height)
        for x in range(width)
        if (x, y) not in occupied
    }
    distance_to_outside, _unused = chamfer_distance_and_tokens(
        width, height, outside
    )
    signed = [
        -distance_to_outside[index]
        if distance_to_inside[index] == 0
        else distance_to_inside[index]
        for index in range(width * height)
    ]
    return signed, nearest_token


def coherent_distance_field_class_frames(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
    shadow: bool,
) -> list[dict[tuple[int, int], int]]:
    """Morph a filled body/shadow silhouette without dithered pixel loss."""
    source_class = [
        point for point in source_points if (point[2] == 1) == shadow
    ]
    target_class = [
        point for point in target_points if (point[2] == 1) == shadow
    ]
    if not source_class and not target_class:
        return [{} for _ in range(VISUAL_ARCHIVE_INTERMEDIATE_COUNT)]

    def bounds(
        selected: list[tuple[int, int, int]]
    ) -> tuple[int, int, int, int]:
        return (
            min(x for x, _y, _token in selected),
            min(y for _x, y, _token in selected),
            max(x for x, _y, _token in selected) + 1,
            max(y for _x, y, _token in selected) + 1,
        )

    if not source_class:
        target_left, target_top, target_right, target_bottom = bounds(target_class)
        center_x = (target_left + target_right - 1) // 2
        center_y = (target_top + target_bottom - 1) // 2
        source_class = [(
            center_x, center_y,
            1 if shadow else target_class[len(target_class) // 2][2],
        )]
    if not target_class:
        source_left, source_top, source_right, source_bottom = bounds(source_class)
        center_x = (source_left + source_right - 1) // 2
        center_y = (source_top + source_bottom - 1) // 2
        target_class = [(
            center_x, center_y,
            1 if shadow else source_class[len(source_class) // 2][2],
        )]

    source_left, source_top, source_right, source_bottom = bounds(source_class)
    target_left, target_top, target_right, target_bottom = bounds(target_class)
    source_width = source_right - source_left
    source_height = source_bottom - source_top
    target_width = target_right - target_left
    target_height = target_bottom - target_top
    canonical_width = max(source_width, target_width)
    canonical_height = max(source_height, target_height)
    padding = 2
    width = canonical_width + padding * 2
    height = canonical_height + padding * 2

    def canonical_tokens(
        selected: list[tuple[int, int, int]],
        left: int,
        top: int,
        frame_width: int,
        frame_height: int,
    ) -> dict[tuple[int, int], int]:
        at = {(x - left, y - top): token for x, y, token in selected}
        result: dict[tuple[int, int], int] = {}
        for canonical_y in range(canonical_height):
            source_y = rounded_fraction(
                canonical_y * max(0, frame_height - 1),
                max(1, canonical_height - 1),
            )
            for canonical_x in range(canonical_width):
                source_x = rounded_fraction(
                    canonical_x * max(0, frame_width - 1),
                    max(1, canonical_width - 1),
                )
                token = at.get((source_x, source_y), 0)
                if token:
                    result[(canonical_x + padding, canonical_y + padding)] = token
        return result

    source_grid = canonical_tokens(
        source_class, source_left, source_top, source_width, source_height
    )
    target_grid = canonical_tokens(
        target_class, target_left, target_top, target_width, target_height
    )

    source_field, source_tokens = signed_chamfer_field(
        width, height, source_grid
    )
    target_field, target_tokens = signed_chamfer_field(
        width, height, target_grid
    )
    generated_frames: list[dict[tuple[int, int], int]] = []
    for step in range(1, VISUAL_ARCHIVE_INTERVAL_COUNT):
        output_left = lerp_coordinate(source_left, target_left, step)
        output_top = lerp_coordinate(source_top, target_top, step)
        output_width = max(1, lerp_coordinate(source_width, target_width, step))
        output_height = max(1, lerp_coordinate(source_height, target_height, step))
        prefer_target = step * 2 >= VISUAL_ARCHIVE_INTERVAL_COUNT
        palette_tokens = target_tokens if prefer_target else source_tokens
        generated: dict[tuple[int, int], int] = {}
        for output_y in range(output_height):
            canonical_y = rounded_fraction(
                output_y * max(0, canonical_height - 1),
                max(1, output_height - 1),
            ) + padding
            row = canonical_y * width
            for output_x in range(output_width):
                canonical_x = rounded_fraction(
                    output_x * max(0, canonical_width - 1),
                    max(1, output_width - 1),
                ) + padding
                index = row + canonical_x
                blended_distance = (
                    source_field[index] * (VISUAL_ARCHIVE_INTERVAL_COUNT - step)
                    + target_field[index] * step
                )
                if blended_distance > 0:
                    continue
                token = 1 if shadow else palette_tokens[index]
                if token == 0:
                    token = (
                        target_tokens[index]
                        if not prefer_target else source_tokens[index]
                    )
                if token != 0:
                    generated[(
                        output_left + output_x, output_top + output_y
                    )] = token
        generated_frames.append(generated)
    return generated_frames


def coherent_distance_field_morph_frames_from_points(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
) -> list[dict[tuple[int, int], int]]:
    shadow_frames = coherent_distance_field_class_frames(
        source_points, target_points, True
    )
    body_frames = coherent_distance_field_class_frames(
        source_points, target_points, False
    )
    maximum_body_components = max(
        1,
        len(dot_components({(x, y): token for x, y, token in source_points}, False)),
        len(dot_components({(x, y): token for x, y, token in target_points}, False)),
    )
    body_frames = [
        connect_excess_body_components(frame, maximum_body_components)
        for frame in body_frames
    ]
    result: list[dict[tuple[int, int], int]] = []
    for shadow, body in zip(shadow_frames, body_frames):
        combined = dict(shadow)
        combined.update(body)
        result.append(combined)
    return result


def dot_components(
    dots: dict[tuple[int, int], int], shadow: bool
) -> list[set[tuple[int, int]]]:
    remaining = {
        point for point, token in dots.items() if (token == 1) == shadow
    }
    components: list[set[tuple[int, int]]] = []
    while remaining:
        start = remaining.pop()
        component = {start}
        pending = [start]
        while pending:
            x, y = pending.pop()
            for delta_y in (-1, 0, 1):
                for delta_x in (-1, 0, 1):
                    neighbor = (x + delta_x, y + delta_y)
                    if neighbor in remaining:
                        remaining.remove(neighbor)
                        component.add(neighbor)
                        pending.append(neighbor)
        components.append(component)
    return sorted(components, key=len, reverse=True)


def integer_line(
    start: tuple[int, int], end: tuple[int, int]
) -> list[tuple[int, int]]:
    """Return an inclusive one-pixel Bresenham line between two dots."""
    x, y = start
    end_x, end_y = end
    delta_x = abs(end_x - x)
    step_x = 1 if x < end_x else -1
    delta_y = -abs(end_y - y)
    step_y = 1 if y < end_y else -1
    error = delta_x + delta_y
    result: list[tuple[int, int]] = []
    while True:
        result.append((x, y))
        if x == end_x and y == end_y:
            return result
        doubled_error = error * 2
        if doubled_error >= delta_y:
            error += delta_y
            x += step_x
        if doubled_error <= delta_x:
            error += delta_x
            y += step_y


def connect_excess_body_components(
    dots: dict[tuple[int, int], int], maximum_components: int
) -> dict[tuple[int, int], int]:
    """Reconnect only body fragments not present in either endpoint pose."""
    repaired = dict(dots)
    maximum_components = max(1, maximum_components)
    components = dot_components(repaired, False)
    while len(components) > maximum_components:
        best: tuple[int, tuple[int, int], tuple[int, int]] | None = None
        for left_index, left_component in enumerate(components):
            for right_component in components[left_index + 1:]:
                for left in left_component:
                    for right in right_component:
                        distance = max(
                            abs(left[0] - right[0]), abs(left[1] - right[1])
                        )
                        candidate = (distance, left, right)
                        if best is None or candidate < best:
                            best = candidate
        if best is None:
            break
        _distance, left, right = best
        path = integer_line(left, right)
        left_token = repaired[left]
        right_token = repaired[right]
        for index, point in enumerate(path[1:-1], start=1):
            repaired[point] = (
                left_token if index * 2 <= len(path) - 1 else right_token
            )
        new_components = dot_components(repaired, False)
        if len(new_components) >= len(components):
            raise ValueError("body component repair did not join a fragment")
        components = new_components
    return repaired


def coherent_rank_transport_class_frames(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
    shadow: bool,
) -> list[dict[tuple[int, int], int]]:
    """Move relative silhouette ranks together so opaque dots stay clustered."""
    source_class = sorted(
        (point for point in source_points if (point[2] == 1) == shadow),
        key=lambda point: (point[1], point[0]),
    )
    target_class = sorted(
        (point for point in target_points if (point[2] == 1) == shadow),
        key=lambda point: (point[1], point[0]),
    )
    if not source_class and not target_class:
        return [{} for _ in range(VISUAL_ARCHIVE_INTERMEDIATE_COUNT)]
    if not source_class:
        target_origin = rounded_point_class_origin(target_points, shadow)
        assert target_origin is not None
        source_class = [(
            target_origin[0], target_origin[1],
            1 if shadow else target_class[len(target_class) // 2][2],
        )]
    if not target_class:
        source_origin = rounded_point_class_origin(source_points, shadow)
        assert source_origin is not None
        target_class = [(
            source_origin[0], source_origin[1],
            1 if shadow else source_class[len(source_class) // 2][2],
        )]

    count = max(len(source_class), len(target_class))

    def resampled(
        selected: list[tuple[int, int, int]], index: int
    ) -> tuple[int, int, int]:
        if count <= 1 or len(selected) <= 1:
            return selected[0]
        selected_index = rounded_fraction(
            index * (len(selected) - 1), count - 1
        )
        return selected[selected_index]

    result: list[dict[tuple[int, int], int]] = []
    for step in range(1, VISUAL_ARCHIVE_INTERVAL_COUNT):
        generated: dict[tuple[int, int], int] = {}
        for index in range(count):
            sx, sy, source_token = resampled(source_class, index)
            tx, ty, target_token = resampled(target_class, index)
            x = lerp_coordinate(sx, tx, step)
            y = lerp_coordinate(sy, ty, step)
            token = (
                target_token
                if step * 2 >= VISUAL_ARCHIVE_INTERVAL_COUNT
                else source_token
            )
            existing = generated.get((x, y), 0)
            if existing <= 1 or token > 1:
                generated[(x, y)] = token

        # Close only one-dot scanline cracks created by integer rounding.  It
        # preserves separate limbs and equipment instead of filling the whole
        # bounding box between unrelated components.
        additions: dict[tuple[int, int], int] = {}
        rows: dict[int, list[int]] = {}
        for x, y in generated:
            rows.setdefault(y, []).append(x)
        for y, xs in rows.items():
            ordered = sorted(set(xs))
            for left, right in zip(ordered, ordered[1:]):
                if right - left != 2:
                    continue
                token = generated[(left, y)]
                if token == 1 and generated[(right, y)] != 1:
                    token = generated[(right, y)]
                additions[(left + 1, y)] = token
        generated.update(additions)
        result.append(generated)
    return result


def coherent_rank_transport_morph_frames_from_points(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
) -> list[dict[tuple[int, int], int]]:
    shadow_frames = coherent_rank_transport_class_frames(
        source_points, target_points, True
    )
    body_frames = coherent_rank_transport_class_frames(
        source_points, target_points, False
    )
    result: list[dict[tuple[int, int], int]] = []
    for shadow, body in zip(shadow_frames, body_frames):
        combined = dict(shadow)
        combined.update(body)
        result.append(combined)
    return result


def coherent_optical_flow_class_frames(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
    shadow: bool,
    single_silhouette: bool = False,
) -> list[dict[tuple[int, int], int]]:
    """Warp both endpoint silhouettes and quantize back to hard palette dots."""
    try:
        import cv2
        import numpy as np
    except ImportError:
        return coherent_distance_field_class_frames(
            source_points, target_points, shadow
        )

    source_class = [
        point for point in source_points if (point[2] == 1) == shadow
    ]
    target_class = [
        point for point in target_points if (point[2] == 1) == shadow
    ]
    if not source_class and not target_class:
        return [{} for _ in range(VISUAL_ARCHIVE_INTERMEDIATE_COUNT)]
    if not source_class:
        target_origin = rounded_point_class_origin(target_points, shadow)
        assert target_origin is not None
        source_class = [(
            target_origin[0], target_origin[1],
            1 if shadow else target_class[len(target_class) // 2][2],
        )]
    if not target_class:
        source_origin = rounded_point_class_origin(source_points, shadow)
        assert source_origin is not None
        target_class = [(
            source_origin[0], source_origin[1],
            1 if shadow else source_class[len(source_class) // 2][2],
        )]

    def bounds(
        selected: list[tuple[int, int, int]]
    ) -> tuple[int, int, int, int]:
        return (
            min(x for x, _y, _token in selected),
            min(y for _x, y, _token in selected),
            max(x for x, _y, _token in selected) + 1,
            max(y for _x, y, _token in selected) + 1,
        )

    source_left, source_top, source_right, source_bottom = bounds(source_class)
    target_left, target_top, target_right, target_bottom = bounds(target_class)
    source_width = source_right - source_left
    source_height = source_bottom - source_top
    target_width = target_right - target_left
    target_height = target_bottom - target_top
    margin = 8
    canonical_width = max(source_width, target_width)
    canonical_height = max(source_height, target_height)
    width = canonical_width + margin * 2
    height = canonical_height + margin * 2

    def raster(
        selected: list[tuple[int, int, int]],
        frame_left: int,
        frame_top: int,
        frame_width: int,
        frame_height: int,
    ) -> tuple[object, object]:
        at = {
            (x - frame_left, y - frame_top): token
            for x, y, token in selected
        }
        mask = np.zeros((height, width), dtype=np.uint8)
        tokens = np.zeros((height, width), dtype=np.uint8)
        for canonical_y in range(canonical_height):
            source_y = rounded_fraction(
                canonical_y * max(0, frame_height - 1),
                max(1, canonical_height - 1),
            )
            for canonical_x in range(canonical_width):
                source_x = rounded_fraction(
                    canonical_x * max(0, frame_width - 1),
                    max(1, canonical_width - 1),
                )
                token = at.get((source_x, source_y), 0)
                if token:
                    y = canonical_y + margin
                    x = canonical_x + margin
                    mask[y, x] = 255
                    tokens[y, x] = token
        return mask, tokens

    source_mask, source_token_image = raster(
        source_class, source_left, source_top, source_width, source_height
    )
    target_mask, target_token_image = raster(
        target_class, target_left, target_top, target_width, target_height
    )

    # Distance-weighted interiors give Farneback stable gradients while the
    # final alpha is still quantized to an entirely opaque pixel-art mask.
    def flow_image(mask: object) -> object:
        if not np.any(mask):
            return mask
        distance = cv2.distanceTransform(mask, cv2.DIST_L2, 3)
        peak = float(distance.max())
        if peak <= 0.0:
            return mask
        return np.clip(64.0 + distance * (191.0 / peak), 0, 255).astype(
            np.uint8
        ) * (mask != 0)

    source_flow_image = flow_image(source_mask)
    target_flow_image = flow_image(target_mask)
    flow_options = dict(
        pyr_scale=0.5,
        levels=5,
        winsize=31,
        iterations=5,
        poly_n=7,
        poly_sigma=1.5,
        flags=cv2.OPTFLOW_FARNEBACK_GAUSSIAN,
    )
    forward_flow = cv2.calcOpticalFlowFarneback(
        source_flow_image, target_flow_image, None, **flow_options
    )
    reverse_flow = cv2.calcOpticalFlowFarneback(
        target_flow_image, source_flow_image, None, **flow_options
    )
    grid_x, grid_y = np.meshgrid(
        np.arange(width, dtype=np.float32),
        np.arange(height, dtype=np.float32),
    )

    def warp(image: object, flow: object, fraction: float,
             interpolation: int) -> object:
        map_x = grid_x - flow[:, :, 0] * fraction
        map_y = grid_y - flow[:, :, 1] * fraction
        return cv2.remap(
            image, map_x, map_y, interpolation,
            borderMode=cv2.BORDER_CONSTANT, borderValue=0,
        )

    kernel = np.ones((3, 3), dtype=np.uint8)
    result: list[dict[tuple[int, int], int]] = []
    for step in range(1, VISUAL_ARCHIVE_INTERVAL_COUNT):
        fraction = step / VISUAL_ARCHIVE_INTERVAL_COUNT
        source_alpha = warp(
            source_mask, forward_flow, fraction, cv2.INTER_LINEAR
        ).astype(np.float32)
        target_alpha = warp(
            target_mask, reverse_flow, 1.0 - fraction, cv2.INTER_LINEAR
        ).astype(np.float32)
        source_tokens = warp(
            source_token_image, forward_flow, fraction, cv2.INTER_NEAREST
        )
        target_tokens = warp(
            target_token_image, reverse_flow, 1.0 - fraction,
            cv2.INTER_NEAREST,
        )
        if single_silhouette:
            # Large view-direction changes must never expose the source and
            # target outlines at once.  Warp one authoritative opaque pose on
            # each side of the midpoint; both warps approach the same middle
            # geometry, avoiding doubled tails, legs, weapons, and outlines.
            use_target = step * 2 > VISUAL_ARCHIVE_INTERVAL_COUNT
            selected_alpha = target_alpha if use_target else source_alpha
            selected_tokens = target_tokens if use_target else source_tokens
            hard_mask = np.where(
                selected_alpha >= 96.0, 255, 0
            ).astype(np.uint8)
        else:
            blended_alpha = (
                source_alpha * (1.0 - fraction) + target_alpha * fraction
            )
            hard_mask = np.where(
                blended_alpha >= 80.0, 255, 0
            ).astype(np.uint8)
            prefer_target = target_alpha * fraction >= source_alpha * (
                1.0 - fraction
            )
            selected_tokens = np.where(
                prefer_target, target_tokens, source_tokens
            )
            selected_tokens = np.where(
                selected_tokens != 0,
                selected_tokens,
                np.where(prefer_target, source_tokens, target_tokens),
            )
        hard_mask = cv2.morphologyEx(hard_mask, cv2.MORPH_CLOSE, kernel)
        if not shadow:
            # Nearest-neighbour token remapping can leave a one-dot colour
            # hole at a moving edge even though the warped mask is solid.
            # Grow only palette indices into those holes; alpha stays defined
            # entirely by the hard silhouette above.
            dilated_tokens = cv2.dilate(selected_tokens, kernel)
            selected_tokens = np.where(
                selected_tokens != 0, selected_tokens, dilated_tokens
            )
        output_left = lerp_coordinate(source_left, target_left, step)
        output_top = lerp_coordinate(source_top, target_top, step)
        output_width = max(1, lerp_coordinate(source_width, target_width, step))
        output_height = max(1, lerp_coordinate(source_height, target_height, step))
        canonical_mask = hard_mask[
            margin:margin + canonical_height,
            margin:margin + canonical_width,
        ]
        canonical_tokens = selected_tokens[
            margin:margin + canonical_height,
            margin:margin + canonical_width,
        ]
        output_mask = cv2.resize(
            canonical_mask, (output_width, output_height),
            interpolation=cv2.INTER_NEAREST,
        )
        output_tokens = cv2.resize(
            canonical_tokens, (output_width, output_height),
            interpolation=cv2.INTER_NEAREST,
        )
        generated: dict[tuple[int, int], int] = {}
        for y, x in np.argwhere(output_mask != 0):
            token = 1 if shadow else int(output_tokens[y, x])
            if token:
                generated[(
                    output_left + int(x), output_top + int(y)
                )] = token
        result.append(generated)
    return result


def coherent_optical_flow_morph_frames_from_points(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
    single_silhouette: bool = False,
) -> list[dict[tuple[int, int], int]]:
    shadow_frames = coherent_optical_flow_class_frames(
        source_points, target_points, True, single_silhouette
    )
    body_frames = coherent_optical_flow_class_frames(
        source_points, target_points, False, single_silhouette
    )
    result: list[dict[tuple[int, int], int]] = []
    for shadow, body in zip(shadow_frames, body_frames):
        combined = dict(shadow)
        combined.update(body)
        result.append(combined)
    return result


def coherent_endpoint_morph_dots_from_points(
    source_points: list[tuple[int, int, int]],
    target_points: list[tuple[int, int, int]],
    step: int,
) -> dict[tuple[int, int], int]:
    """Move an intact endpoint pose when per-dot matching would fragment it."""
    use_target = step * 2 >= VISUAL_ARCHIVE_INTERVAL_COUNT
    selected = target_points if use_target else source_points
    result: dict[tuple[int, int], int] = {}
    for shadow in (True, False):
        source_centroid = point_class_centroid(source_points, shadow)
        target_centroid = point_class_centroid(target_points, shadow)
        shift_x = 0
        shift_y = 0
        if source_centroid is not None and target_centroid is not None:
            source_sum_x, source_sum_y, source_count = source_centroid
            target_sum_x, target_sum_y, target_count = target_centroid
            denominator = (
                source_count * target_count * VISUAL_ARCHIVE_INTERVAL_COUNT
            )
            delta_x = (
                target_sum_x * source_count - source_sum_x * target_count
            )
            delta_y = (
                target_sum_y * source_count - source_sum_y * target_count
            )
            if use_target:
                remaining = VISUAL_ARCHIVE_INTERVAL_COUNT - step
                shift_x = rounded_fraction(-delta_x * remaining, denominator)
                shift_y = rounded_fraction(-delta_y * remaining, denominator)
            else:
                shift_x = rounded_fraction(delta_x * step, denominator)
                shift_y = rounded_fraction(delta_y * step, denominator)
        for x, y, token in selected:
            if (token == 1) != shadow:
                continue
            result[(x + shift_x, y + shift_y)] = token
    return result


def coherent_endpoint_morph_dots(
    source: SpriteFrame, target: SpriteFrame, step: int
) -> dict[tuple[int, int], int]:
    return coherent_endpoint_morph_dots_from_points(
        frame_points(source), frame_points(target), step
    )


def sparse_run_count(dots: dict[tuple[int, int], int]) -> int:
    runs = 0
    prior_x: int | None = None
    prior_y: int | None = None
    for x, y in sorted(dots, key=lambda point: (point[1], point[0])):
        if prior_y != y or prior_x is None or x != prior_x + 1:
            runs += 1
        prior_x = x
        prior_y = y
    return runs


def endpoint_dots(frame: SpriteFrame) -> dict[tuple[int, int], int]:
    return {(x, y): token for x, y, token in frame_points(frame)}


@functools.lru_cache(maxsize=None)
def frame_visual_features(frame: SpriteFrame) -> tuple[float, ...]:
    points = [
        (x, y) for x, y, token in frame_points(frame) if token > 1
    ]
    if not points:
        return (0.0,) * 8
    count = len(points)
    center_x = sum(x for x, _y in points) / count
    center_y = sum(y for _x, y in points) / count
    left = min(x for x, _y in points)
    top = min(y for _x, y in points)
    width = max(x for x, _y in points) - left + 1
    height = max(y for _x, y in points) - top + 1
    covariance_xx = sum((x - center_x) ** 2 for x, _y in points) / count
    covariance_yy = sum((y - center_y) ** 2 for _x, y in points) / count
    covariance_xy = sum(
        (x - center_x) * (y - center_y) for x, y in points
    ) / count
    covariance_trace = max(1.0, covariance_xx + covariance_yy)
    return (
        math.log(max(1, width)),
        math.log(max(1, height)),
        math.log(max(1, count)),
        count / max(1, width * height),
        (covariance_xx - covariance_yy) / covariance_trace,
        2.0 * covariance_xy / covariance_trace,
        covariance_xx / max(1, width * width),
        covariance_yy / max(1, height * height),
    )


_VISUAL_FEATURE_WEIGHTS = (3.0, 3.0, 1.0, 0.5, 2.0, 2.0, 2.0, 2.0)


def visual_feature_distance(
    left: tuple[float, ...], right: tuple[float, ...]
) -> float:
    return sum(
        weight * (left_value - right_value) ** 2
        for weight, left_value, right_value in zip(
            _VISUAL_FEATURE_WEIGHTS, left, right
        )
    )


def should_use_single_silhouette_visual_morph(
    source: SpriteFrame, target: SpriteFrame
) -> bool:
    return visual_feature_distance(
        frame_visual_features(source), frame_visual_features(target)
    ) >= 1.0


def is_local_view_direction_boundary(
    frames: list[SpriteFrame], source_index: int, target_index: int
) -> bool:
    """Detect a view change between otherwise closely spaced animation poses."""
    frame_count = len(frames)
    if frame_count < 4 or source_index == target_index:
        return False
    if not (0 <= source_index < frame_count and 0 <= target_index < frame_count):
        return False

    if (source_index + 1) % frame_count == target_index:
        source_outer = (source_index - 1) % frame_count
        target_outer = (target_index + 1) % frame_count
    elif (source_index - 1) % frame_count == target_index:
        source_outer = (source_index + 1) % frame_count
        target_outer = (target_index - 1) % frame_count
    else:
        return False

    current_distance = visual_feature_distance(
        frame_visual_features(frames[source_index]),
        frame_visual_features(frames[target_index]),
    )
    source_side_distance = visual_feature_distance(
        frame_visual_features(frames[source_outer]),
        frame_visual_features(frames[source_index]),
    )
    target_side_distance = visual_feature_distance(
        frame_visual_features(frames[target_index]),
        frame_visual_features(frames[target_outer]),
    )
    local_motion_distance = max(source_side_distance, target_side_distance)
    return (
        current_distance >= 0.05
        and current_distance >= local_motion_distance * 4.0 + 0.02
    )


def select_original_bridge_frames(
    frames: list[SpriteFrame], source_index: int, target_index: int
) -> list[SpriteFrame] | None:
    """Choose five evenly spaced artist poses along the index direction."""
    if source_index == target_index:
        return None
    direction = 1 if target_index > source_index else -1
    candidate_indices = list(range(
        source_index + direction, target_index, direction
    ))
    if len(candidate_indices) < VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
        return None
    span = abs(target_index - source_index)
    selected_indices = [
        source_index + direction * rounded_fraction(
            span * step, VISUAL_ARCHIVE_INTERVAL_COUNT
        )
        for step in range(1, VISUAL_ARCHIVE_INTERVAL_COUNT)
    ]
    if (len(set(selected_indices)) != VISUAL_ARCHIVE_INTERMEDIATE_COUNT or
            any(index not in candidate_indices for index in selected_indices)):
        raise ValueError("evenly spaced original bridge is not strictly monotonic")
    return [frames[index] for index in selected_indices]


def build_hybrid_morph_frames(
    source: SpriteFrame,
    target: SpriteFrame,
    candidate_frames: list[SpriteFrame] | None = None,
    source_index: int | None = None,
    target_index: int | None = None,
) -> tuple[list[dict[tuple[int, int], int]], str, float]:
    source_points = frame_points(source)
    target_points = frame_points(target)
    correspondence = build_pixel_correspondence(
        source, target, source_points=source_points, target_points=target_points
    )
    coverage = correspondence_coverage(correspondence)
    midpoint_step = VISUAL_ARCHIVE_INTERVAL_COUNT // 2
    midpoint = pixel_morph_dots(correspondence, midpoint_step)
    endpoint_runs = max(
        sparse_run_count({(x, y): token for x, y, token in source_points}),
        sparse_run_count({(x, y): token for x, y, token in target_points}),
    )
    excess_midpoint_runs = max(0, sparse_run_count(midpoint) - endpoint_runs)
    fragmented = (
        coverage < 0.70
        or excess_midpoint_runs > max(12, len(midpoint) // 12)
    )
    if not fragmented:
        return [
            midpoint if step == midpoint_step
            else pixel_morph_dots(correspondence, step)
            for step in range(1, VISUAL_ARCHIVE_INTERVAL_COUNT)
        ], "pixel_correspondence", coverage
    if (candidate_frames is not None and source_index is not None and
            target_index is not None):
        original_bridges = select_original_bridge_frames(
            candidate_frames, source_index, target_index
        )
        if original_bridges is not None:
            return [
                endpoint_dots(frame) for frame in original_bridges
            ], "original_bridge", coverage
        if (is_local_view_direction_boundary(
                candidate_frames, source_index, target_index) and
                not should_use_single_silhouette_visual_morph(source, target)):
            return (
                coherent_distance_field_morph_frames_from_points(
                    source_points, target_points
                ),
                "direction_distance_field",
                coverage,
            )
    single_silhouette = should_use_single_silhouette_visual_morph(
        source, target
    )
    return (
        coherent_optical_flow_morph_frames_from_points(
            source_points, target_points, single_silhouette
        ),
        (
            "direction_single_silhouette"
            if single_silhouette else "optical_flow"
        ),
        coverage,
    )


def draw_pixel_morph(
    canvas: bytearray,
    canvas_width: int,
    canvas_height: int,
    correspondence: list[tuple[int, int, int, int, int, int]],
    palette: bytes,
    anchor_x: int,
    anchor_y: int,
    step: int,
    interval_count: int,
) -> None:
    if interval_count != VISUAL_ARCHIVE_INTERVAL_COUNT:
        raise ValueError("pixel morph preview interval count must match the archive")
    dots = pixel_morph_dots(correspondence, step)
    draw_morph_dots(
        canvas, canvas_width, canvas_height, dots, palette, anchor_x, anchor_y
    )


def draw_morph_dots(
    canvas: bytearray,
    canvas_width: int,
    canvas_height: int,
    dots: dict[tuple[int, int], int],
    palette: bytes,
    anchor_x: int,
    anchor_y: int,
) -> None:

    # Ground shadow first, then the original opaque palette dots.
    for shadow_pass in (True, False):
        for (x, y), token in dots.items():
            if (token == 1) != shadow_pass:
                continue
            target_x = anchor_x + x
            target_y = anchor_y + y
            if not (0 <= target_x < canvas_width and 0 <= target_y < canvas_height):
                continue
            pixel = (target_y * canvas_width + target_x) * 4
            if token == 1:
                canvas[pixel + 0] //= 2
                canvas[pixel + 1] //= 2
                canvas[pixel + 2] //= 2
            else:
                canvas[pixel : pixel + 4] = bytes(palette_rgba(palette, token))


def write_transition_sheet(
    unit: UnitRecord,
    group_index: int,
    source_index: int,
    target_index: int,
    output: Path,
    margin: int,
    method: str,
) -> None:
    frames = unit.groups[group_index]
    if not 0 <= source_index < len(frames) or not 0 <= target_index < len(frames):
        raise ValueError("transition frame index is outside the selected group")
    source = frames[source_index]
    target = frames[target_index]
    hybrid_frames = (
        build_hybrid_morph_frames(
            source, target, frames, source_index, target_index
        )[0]
        if method == "hybrid-sharp" else None
    )
    if hybrid_frames is None:
        left, top, right, bottom = frame_bounds((source, target))
    else:
        all_points = frame_points(source) + frame_points(target) + [
            (x, y, token)
            for generated in hybrid_frames
            for (x, y), token in generated.items()
        ]
        left = min(x for x, _y, _token in all_points)
        top = min(y for _x, y, _token in all_points)
        right = max(x for x, _y, _token in all_points) + 1
        bottom = max(y for _x, y, _token in all_points) + 1
    cell_width = max(1, right - left) + margin * 2
    cell_height = max(1, bottom - top) + margin * 2
    interval_count = 6
    width = cell_width * (interval_count + 1)
    height = cell_height
    canvas = bytearray(width * height * 4)
    fill_checkerboard(canvas, width, height)
    correspondence = build_pixel_correspondence(source, target)
    for step in range(interval_count + 1):
        anchor_x = step * cell_width + margin - left
        anchor_y = margin - top
        if method == "pixel-morph":
            draw_pixel_morph(
                canvas, width, height, correspondence, unit.palette,
                anchor_x, anchor_y, step, interval_count
            )
        elif method == "hybrid-sharp":
            if step == 0:
                draw_frame_preview(
                    canvas, width, height, source, unit.palette,
                    anchor_x, anchor_y
                )
            elif step == interval_count:
                draw_frame_preview(
                    canvas, width, height, target, unit.palette,
                    anchor_x, anchor_y
                )
            else:
                draw_morph_dots(
                    canvas, width, height, hybrid_frames[step - 1], unit.palette,
                    anchor_x, anchor_y
                )
        else:
            target_weight = (step * 31 + interval_count // 2) // interval_count
            source_weight = 31 - target_weight
            shadow = target if target_weight >= source_weight else source
            draw_transition_layer(
                canvas, width, height, shadow, unit.palette, anchor_x, anchor_y, 0, True
            )
            draw_transition_layer(
                canvas, width, height, source, unit.palette,
                anchor_x, anchor_y, source_weight
            )
            draw_transition_layer(
                canvas, width, height, target, unit.palette,
                anchor_x, anchor_y, target_weight
            )
    write_rgba_png(output, width, height, canvas)


def load_pre_generated_transition(
    archive_path: Path,
    unit_type: int,
    group_index: int,
    source_index: int,
    target_index: int,
) -> list[SpriteFrame]:
    with archive_path.open("rb") as stream:
        header = stream.read(VISUAL_ARCHIVE_HEADER.size)
        if len(header) != VISUAL_ARCHIVE_HEADER.size:
            raise ValueError("animation archive header is truncated")
        values = VISUAL_ARCHIVE_HEADER.unpack(header)
        (
            magic, version, header_size, transition_count, directory_entry_size,
            directory_offset, _payload_offset, _payload_size, _source_size,
            _source_sha256, _source_crc32, _directory_crc32, interval_count,
            intermediate_count,
        ) = values
        if (magic != VISUAL_ARCHIVE_MAGIC or version != VISUAL_ARCHIVE_VERSION or
                header_size != VISUAL_ARCHIVE_HEADER.size or
                directory_entry_size != VISUAL_ARCHIVE_DIRECTORY_ENTRY.size or
                interval_count != VISUAL_ARCHIVE_INTERVAL_COUNT or
                intermediate_count != VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
            raise ValueError("animation archive header is incompatible")
        stream.seek(directory_offset)
        found: tuple[int, ...] | None = None
        requested = (unit_type, group_index, source_index, target_index, 0)
        for _ in range(transition_count):
            raw = stream.read(VISUAL_ARCHIVE_DIRECTORY_ENTRY.size)
            if len(raw) != VISUAL_ARCHIVE_DIRECTORY_ENTRY.size:
                raise ValueError("animation archive directory is truncated")
            entry = VISUAL_ARCHIVE_DIRECTORY_ENTRY.unpack(raw)
            key = (entry[0], entry[1], entry[3], entry[4], entry[2])
            if key == requested:
                found = entry
                break
            if key > requested:
                break
        if found is None:
            raise ValueError(
                f"pre-generated transition not found: unit={unit_type} "
                f"group={group_index} source={source_index} target={target_index}"
            )
        (
            _type_id, _group, _flags, _source, _target, left, top,
            width, height, data_offset, stored_size, uncompressed_size,
            raw_crc32, _flip_transform,
        ) = found
        stream.seek(data_offset)
        stored = stream.read(stored_size)
        if len(stored) != stored_size:
            raise ValueError("animation transition payload is truncated")
    raw_transition = zlib.decompress(stored)
    if (len(raw_transition) != uncompressed_size or
            (binascii.crc32(raw_transition) & 0xFFFFFFFF) != raw_crc32):
        raise ValueError("animation transition payload failed validation")
    frames: list[SpriteFrame] = []
    cursor = 0
    for _ in range(VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
        if cursor + 4 > len(raw_transition):
            raise ValueError("animation transition frame table is truncated")
        frame_size = read_u32(raw_transition, cursor)
        cursor += 4
        end = cursor + frame_size
        if end > len(raw_transition):
            raise ValueError("animation transition frame is truncated")
        metadata = (
            width, height, left & 0xFFFFFFFF, top & 0xFFFFFFFF, 0, 0
        )
        frame = SpriteFrame(metadata, raw_transition[cursor:end])
        frame.decode_indices()
        frames.append(frame)
        cursor = end
    if cursor != len(raw_transition):
        raise ValueError("animation transition has trailing data")
    return frames


def scale_rgba_nearest(
    rgba: bytes, width: int, height: int, scale: int
) -> tuple[int, int, bytes]:
    if scale <= 1:
        return width, height, rgba
    output_width = width * scale
    output_height = height * scale
    output = bytearray(output_width * output_height * 4)
    for y in range(height):
        source_row = rgba[y * width * 4:(y + 1) * width * 4]
        expanded_row = b"".join(
            source_row[x:x + 4] * scale
            for x in range(0, len(source_row), 4)
        )
        for repeat in range(scale):
            start = ((y * scale + repeat) * output_width) * 4
            output[start:start + len(expanded_row)] = expanded_row
    return output_width, output_height, bytes(output)


def write_pre_generated_transition_sheet(
    source_archive: TrcArchive,
    animation_archive: Path,
    unit_type: int,
    group_index: int,
    source_index: int,
    target_index: int,
    output: Path,
    margin: int,
    scale: int,
) -> None:
    unit = decode_unit_record(source_archive, unit_type)
    if not 0 <= group_index < len(unit.groups):
        raise ValueError(f"invalid group {group_index}")
    originals = unit.groups[group_index]
    if not 0 <= source_index < len(originals) or not 0 <= target_index < len(originals):
        raise ValueError("transition frame index is outside the selected group")
    generated = load_pre_generated_transition(
        animation_archive, unit_type, group_index, source_index, target_index
    )
    frames = [originals[source_index], *generated, originals[target_index]]
    left, top, right, bottom = frame_bounds(frames)
    cell_width = max(1, right - left) + margin * 2
    cell_height = max(1, bottom - top) + margin * 2
    width = cell_width * len(frames)
    height = cell_height
    canvas = bytearray(width * height * 4)
    fill_checkerboard(canvas, width, height)
    for index, frame in enumerate(frames):
        anchor_x = index * cell_width + margin - left
        anchor_y = margin - top
        draw_frame_preview(
            canvas, width, height, frame, unit.palette, anchor_x, anchor_y
        )
    scaled_width, scaled_height, scaled = scale_rgba_nearest(
        bytes(canvas), width, height, max(1, scale)
    )
    write_rgba_png(output, scaled_width, scaled_height, scaled)


def encode_sparse_morph_frame(
    dots: dict[tuple[int, int], int],
    left: int,
    top: int,
    width: int,
    height: int,
) -> bytes:
    rows: list[list[tuple[int, int]]] = [[] for _ in range(height)]
    for (x, y), token in dots.items():
        row = y - top
        column = x - left
        if not (0 <= row < height and 0 <= column < width):
            raise ValueError("generated morph dot escaped its transition bounds")
        rows[row].append((column, token))

    encoded = bytearray()
    for row in rows:
        row.sort()
        row_bytes = bytearray()
        column = 0
        for target_column, token in row:
            gap = target_column - column
            while gap > 0:
                skip = min(gap, 255)
                row_bytes.extend((0, skip))
                column += skip
                gap -= skip
            row_bytes.append(token)
            column += 1
        if len(row_bytes) > 0xFFFF:
            raise ValueError("generated morph row exceeds the archive RLE limit")
        encoded.extend(struct.pack("<H", len(row_bytes)))
        encoded.extend(row_bytes)
    return bytes(encoded)


def active_animation_table(unit: UnitRecord, table_group: int) -> list[int]:
    """Read one definition table while retaining a single loop-reset value."""
    table = [
        read_u32(
            unit.definition,
            0x140C + table_group * 0x100 + frame * 4,
        )
        for frame in range(64)
    ]
    period = len(table)
    tail_value = table[-1]
    tail_start = period - 1
    while tail_start > 0 and table[tail_start - 1] == tail_value:
        tail_start -= 1
    if period - tail_start >= 8:
        period = tail_start + 1
    return table[:period]


def animation_direction_row_bases(
    unit: UnitRecord, group_index: int
) -> list[int]:
    """Return the eight original 1-based direction row entries for a group.

    The on-disk tables deliberately overlap at their boundary: the base points
    one dword before direction 1 and row indices are 1..8. Including index 0
    imports the previous group's final row (for example it manufactured a
    false BuildMan group-1 row at frame 12).
    """
    frame_count = len(unit.groups[group_index])
    return sorted({
        read_u32(
            unit.definition,
            0x2248 + group_index * 0x20 + direction * 4,
        )
        for direction in range(1, 9)
        if read_u32(
            unit.definition,
            0x2248 + group_index * 0x20 + direction * 4,
        ) < frame_count
    })


def animation_temporal_sequences(
    unit: UnitRecord,
) -> list[tuple[int, tuple[int, ...], bool, str]]:
    """Return original animation strips without ever joining directions.

    The bool marks an explicit loop whose last table value resets to its first
    value. Repeated entries are retained because they are artist-authored hold
    timing and provide useful look-ahead to motion compensation.
    """
    sequences: list[tuple[int, tuple[int, ...], bool, str]] = []
    for group_index, frames in enumerate(unit.groups):
        frame_count = len(frames)
        if frame_count <= 1:
            continue
        row_bases = animation_direction_row_bases(unit, group_index)
        table_groups = {group_index}
        if group_index == 1:
            table_groups.add(0)
        for table_group in sorted(table_groups):
            table = active_animation_table(unit, table_group)
            for row_base in row_bases:
                resolved = [
                    value + row_base
                    if value != 0xFFFFFFFF and value + row_base < frame_count
                    else None
                    for value in table
                ]
                segment: list[int] = []
                segment_number = 0
                for value in resolved + [None]:
                    if value is not None:
                        segment.append(value)
                        continue
                    if len(segment) >= 2:
                        looped = segment[0] == segment[-1]
                        cycle = segment[:-1] if looped else segment
                        if len(cycle) >= 2:
                            sequences.append((
                                group_index,
                                tuple(cycle),
                                looped,
                                f"table_{table_group:02d}_row_{row_base:04d}_"
                                f"segment_{segment_number:02d}",
                            ))
                    segment = []
                    segment_number += 1

        # A single-row group is also a direct construction/damage/one-shot
        # strip. This fallback covers artist frames not referenced by a sparse
        # definition table, but it never creates a direction boundary.
        if len(row_bases) <= 1:
            sequences.append((
                group_index,
                tuple(range(frame_count)),
                False,
                "single_row_progress_strip",
            ))
    return sequences


def animation_temporal_transition_pairs(
    unit: UnitRecord,
) -> list[tuple[int, int, int]]:
    """Return all actual within-direction animation edges for one unit."""
    pairs: set[tuple[int, int, int]] = set()
    for group, sequence, looped, _source in animation_temporal_sequences(unit):
        for source, target in zip(sequence, sequence[1:]):
            if source != target:
                pairs.add((group, source, target))
        if looped and sequence[-1] != sequence[0]:
            pairs.add((group, sequence[-1], sequence[0]))
    return sorted(pairs)


def animation_transition_pairs(unit: UnitRecord) -> list[tuple[int, int, int]]:
    """Return transitions reachable through the original frame/row tables.

    Image groups are direction rows, not flat animation cycles.  In particular,
    the last frame of one row must never be connected to the first frame of the
    next row merely because their resource indices are adjacent.
    """
    pairs: set[tuple[int, int, int]] = set()
    for group_index, frames in enumerate(unit.groups):
        frame_count = len(frames)
        if frame_count <= 1:
            continue

        table_groups = {group_index}
        # The original default-idle resolver drives image group 1 from table 0.
        if group_index == 1:
            table_groups.add(0)
        # Primary direction rows 0..8 are addressable. Row 8 deliberately
        # reaches the first dword after the nominal eight-entry stride, exactly
        # as original_unit_animation_frame_index does at runtime.
        row_bases = sorted({
            read_u32(
                unit.definition,
                0x2248 + group_index * 0x20 + direction * 4,
            )
            for direction in range(9)
        })
        row_bases = [base for base in row_bases if base < frame_count]
        for table_group in table_groups:
            table = [
                read_u32(
                    unit.definition,
                    0x140C + table_group * 0x100 + frame * 4,
                )
                for frame in range(64)
            ]
            # Definition tables are zero-padded. Keep the final reset zero but
            # discard a long stable padding tail.
            period = len(table)
            tail_value = table[-1]
            tail_start = period - 1
            while tail_start > 0 and table[tail_start - 1] == tail_value:
                tail_start -= 1
            if period - tail_start >= 8:
                period = tail_start + 1
            active_table = table[:period]

            resolved_rows: list[list[int | None]] = []
            for row_base in row_bases:
                resolved_rows.append([
                    value + row_base
                    if value != 0xFFFFFFFF and value + row_base < frame_count
                    else None
                    for value in active_table
                ])

            # Temporal animation edges stay inside one direction row.
            for resolved in resolved_rows:
                for source, target in zip(resolved, resolved[1:]):
                    if source is not None and target is not None and source != target:
                        pairs.add((group_index, source, target))

            # A direction can change while the action counter remains fixed.
            # Connect that same table phase between every addressable row; this
            # includes fast turns without manufacturing cross-row cycle edges.
            for phase in range(len(active_table)):
                phase_frames = sorted({
                    resolved[phase] for resolved in resolved_rows
                    if resolved[phase] is not None
                })
                for source in phase_frames:
                    for target in phase_frames:
                        if source != target:
                            pairs.add((group_index, source, target))

            # Direction and action counters can both advance on one simulation
            # tick. Cover the next table phase across every source/target row,
            # while still never treating adjacent storage indices as a cycle.
            for phase in range(len(active_table) - 1):
                source_frames = sorted({
                    resolved[phase] for resolved in resolved_rows
                    if resolved[phase] is not None
                })
                target_frames = sorted({
                    resolved[phase + 1] for resolved in resolved_rows
                    if resolved[phase + 1] is not None
                })
                for source in source_frames:
                    for target in target_frames:
                        if source != target:
                            pairs.add((group_index, source, target))

        # Non-directional records use their image group as a direct progress
        # strip (construction, damage, or one-shot sprites).
        if len(row_bases) <= 1:
            for source in range(frame_count - 1):
                pairs.add((group_index, source, source + 1))
    return sorted(pairs)


def build_archive_transition(
    source: SpriteFrame,
    target: SpriteFrame,
    flipped: bool,
    candidate_frames: list[SpriteFrame] | None = None,
    source_index: int | None = None,
    target_index: int | None = None,
    authored_frames: list[dict[tuple[int, int], int]] | None = None,
) -> tuple[int, int, int, int, bytes, str, float]:
    if flipped:
        raise ValueError("the archive stores normal frames and mirrors at runtime")
    if authored_frames is None:
        generated_frames, generation_mode, coverage = build_hybrid_morph_frames(
            source, target, candidate_frames, source_index, target_index
        )
    else:
        if len(authored_frames) != VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
            raise ValueError(
                "authored transition must contain "
                f"{VISUAL_ARCHIVE_INTERMEDIATE_COUNT} frames"
            )
        generated_frames = authored_frames
        generation_mode = "authored_keyframes"
        coverage = 1.0
    points = frame_points(source) + frame_points(target) + [
        (x, y, token)
        for generated in generated_frames
        for (x, y), token in generated.items()
    ]
    if not points:
        return 0, 0, 1, 1, b"".join(
            struct.pack("<I", 2) + b"\0\0"
            for _ in range(VISUAL_ARCHIVE_INTERMEDIATE_COUNT)
        ), "pixel_correspondence", 1.0
    left = min(x for x, _y, _token in points)
    top = min(y for _x, y, _token in points)
    right = max(x for x, _y, _token in points)
    bottom = max(y for _x, y, _token in points)
    width = right - left + 1
    height = bottom - top + 1
    if not (-0x8000 <= left <= 0x7FFF and -0x8000 <= top <= 0x7FFF and
            0 < width <= 0xFFFF and 0 < height <= 0xFFFF):
        raise ValueError("transition bounds do not fit the archive directory")

    payload = bytearray()
    for generated in generated_frames:
        frame_payload = encode_sparse_morph_frame(
            generated, left, top, width, height,
        )
        payload.extend(struct.pack("<I", len(frame_payload)))
        payload.extend(frame_payload)
    return left, top, width, height, bytes(payload), generation_mode, coverage


_ARCHIVE_WORKER_RECORDS: list[UnitRecord] | None = None
_ARCHIVE_WORKER_KEYFRAMES: dict[
    tuple[int, int, int, int], list[dict[tuple[int, int], int]]
] | None = None


def load_keyframe_overrides(
    archive: TrcArchive, path: Path | None
) -> dict[tuple[int, int, int, int], list[dict[tuple[int, int], int]]]:
    if path is None or not path.exists():
        return {}
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") not in (1, 2):
        raise ValueError("keyframe override schema is incompatible")
    source_sha256 = hashlib.sha256(archive.data).hexdigest()
    source_crc32 = f"{binascii.crc32(archive.data) & 0xffffffff:08x}"
    if (document.get("source_archive_sha256") != source_sha256 or
            document.get("source_archive_crc32") != source_crc32 or
            document.get("intermediate_frames_per_transition") !=
            VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
        raise ValueError("keyframe overrides do not match the source TRC")
    result: dict[
        tuple[int, int, int, int], list[dict[tuple[int, int], int]]
    ] = {}
    for override in document.get("overrides", []):
        key = (
            int(override["unit_type"]),
            int(override["group"]),
            int(override["source"]),
            int(override["target"]),
        )
        if key in result:
            raise ValueError(f"duplicate keyframe override: {key}")
        frames = override.get("frames", [])
        if len(frames) != VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
            raise ValueError(
                "keyframe override contains the wrong frame count: "
                f"{key}"
            )
        decoded: list[dict[tuple[int, int], int]] = []
        for frame in frames:
            dots: dict[tuple[int, int], int] = {}
            for raw_dot in frame:
                if len(raw_dot) != 3:
                    raise ValueError(f"invalid authored dot in override: {key}")
                x, y, token = (int(value) for value in raw_dot)
                if not (-0x8000 <= x <= 0x7fff and -0x8000 <= y <= 0x7fff and
                        0 < token <= 0xff):
                    raise ValueError(f"authored dot is outside archive range: {key}")
                dots[(x, y)] = token
            if not dots:
                raise ValueError(f"authored keyframe is empty: {key}")
            decoded.append(dots)
        result[key] = decoded
    return result


def initialize_archive_worker(
    archive_path: str, keyframe_overrides_path: str
) -> None:
    global _ARCHIVE_WORKER_RECORDS, _ARCHIVE_WORKER_KEYFRAMES
    worker_archive = TrcArchive(Path(archive_path))
    _ARCHIVE_WORKER_RECORDS = [
        decode_unit_record(worker_archive, type_id)
        for type_id in range(UNIT_RECORD_COUNT)
    ]
    _ARCHIVE_WORKER_KEYFRAMES = load_keyframe_overrides(
        worker_archive,
        Path(keyframe_overrides_path) if keyframe_overrides_path else None,
    )


def generate_archive_worker(
    spec: tuple[int, int, int, int, bool]
) -> tuple[int, int, int, int, bytes, int, int, str, float]:
    if (_ARCHIVE_WORKER_RECORDS is None or
            _ARCHIVE_WORKER_KEYFRAMES is None):
        raise RuntimeError("visual archive worker was not initialized")
    type_id, group, source_index, target_index, flipped = spec
    unit = _ARCHIVE_WORKER_RECORDS[type_id]
    (
        left, top, width, height, raw_payload, generation_mode, coverage
    ) = build_archive_transition(
        unit.groups[group][source_index],
        unit.groups[group][target_index],
        flipped,
        unit.groups[group],
        source_index,
        target_index,
        _ARCHIVE_WORKER_KEYFRAMES.get(
            (type_id, group, source_index, target_index)
        ),
    )
    return (
        left,
        top,
        width,
        height,
        zlib.compress(raw_payload, 9),
        len(raw_payload),
        binascii.crc32(raw_payload) & 0xFFFFFFFF,
        generation_mode,
        coverage,
    )


def write_visual_animation_archive(
    archive: TrcArchive,
    output: Path,
    manifest_output: Path | None,
    jobs: int,
    keyframe_overrides_path: Path | None,
    only_unit: int | None = None,
    only_group: int | None = None,
) -> None:
    records = [decode_unit_record(archive, type_id) for type_id in range(UNIT_RECORD_COUNT)]
    transition_specs = [
        (unit.type_id, group, source, target, False)
        for unit in records
        if only_unit is None or unit.type_id == only_unit
        for group, source, target in animation_transition_pairs(unit)
        if only_group is None or group == only_group
    ]
    transition_specs.sort()
    header_size = VISUAL_ARCHIVE_HEADER.size
    directory_size = len(transition_specs) * VISUAL_ARCHIVE_DIRECTORY_ENTRY.size
    directory_offset = header_size
    payload_offset = directory_offset + directory_size
    source_sha256 = hashlib.sha256(archive.data).digest()
    source_crc32 = binascii.crc32(archive.data) & 0xFFFFFFFF

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    directory = bytearray()
    unit_transition_counts = [0] * UNIT_RECORD_COUNT
    unit_guarded_transition_counts = [0] * UNIT_RECORD_COUNT
    uncompressed_total = 0
    stored_total = 0
    original_bridge_transition_count = 0
    authored_keyframe_transition_count = 0
    optical_flow_transition_count = 0
    direction_distance_field_transition_count = 0
    direction_single_silhouette_transition_count = 0
    minimum_correspondence_coverage = 1.0
    jobs = max(1, min(jobs, 8))
    if jobs == 1:
        global _ARCHIVE_WORKER_RECORDS, _ARCHIVE_WORKER_KEYFRAMES
        _ARCHIVE_WORKER_RECORDS = records
        _ARCHIVE_WORKER_KEYFRAMES = load_keyframe_overrides(
            archive, keyframe_overrides_path
        )
        generated = map(generate_archive_worker, transition_specs)
        pool = None
    else:
        context = multiprocessing.get_context("spawn")
        pool = context.Pool(
            processes=jobs,
            initializer=initialize_archive_worker,
            initargs=(
                str(archive.path.resolve()),
                str(keyframe_overrides_path.resolve())
                if keyframe_overrides_path is not None else "",
            ),
        )
        generated = pool.imap(generate_archive_worker, transition_specs, chunksize=8)

    pool_succeeded = False
    try:
        with temporary.open("w+b") as stream:
            stream.write(bytes(payload_offset))
            for transition_index, (spec, generated_transition) in enumerate(
                zip(transition_specs, generated)
            ):
                type_id, group, source_index, target_index, flipped = spec
                (
                    left,
                    top,
                    width,
                    height,
                    stored_payload,
                    raw_size,
                    raw_crc32,
                    generation_mode,
                    coverage,
                ) = generated_transition
                source_frame = records[type_id].groups[group][source_index]
                target_frame = records[type_id].groups[group][target_index]
                source_flip_origin_x = (
                    signed_u32(source_frame.metadata[4]) + source_frame.width +
                    source_frame.offset_x
                )
                target_flip_origin_x = (
                    signed_u32(target_frame.metadata[4]) + target_frame.width +
                    target_frame.offset_x
                )
                source_flip_delta_y = (
                    signed_u32(source_frame.metadata[5]) - source_frame.offset_y
                )
                target_flip_delta_y = (
                    signed_u32(target_frame.metadata[5]) - target_frame.offset_y
                )
                if (source_flip_origin_x, source_flip_delta_y) != (
                    target_flip_origin_x, target_flip_delta_y
                ):
                    raise ValueError(
                        "a transition crosses incompatible flip transforms"
                    )
                if not (-0x8000 <= source_flip_origin_x <= 0x7FFF and
                        -0x8000 <= source_flip_delta_y <= 0x7FFF):
                    raise ValueError("flip transform exceeds archive range")
                packed_flip_transform = (
                    (source_flip_origin_x & 0xFFFF) |
                    ((source_flip_delta_y & 0xFFFF) << 16)
                )
                data_offset = stream.tell()
                stream.write(stored_payload)
                directory.extend(VISUAL_ARCHIVE_DIRECTORY_ENTRY.pack(
                    type_id,
                    group,
                    1 if flipped else 0,
                    source_index,
                    target_index,
                    left,
                    top,
                    width,
                    height,
                    data_offset,
                    len(stored_payload),
                    raw_size,
                    raw_crc32,
                    packed_flip_transform,
                ))
                unit_transition_counts[type_id] += 1
                if generation_mode == "authored_keyframes":
                    authored_keyframe_transition_count += 1
                    unit_guarded_transition_counts[type_id] += 1
                elif generation_mode == "original_bridge":
                    original_bridge_transition_count += 1
                    unit_guarded_transition_counts[type_id] += 1
                elif generation_mode == "optical_flow":
                    optical_flow_transition_count += 1
                    unit_guarded_transition_counts[type_id] += 1
                elif generation_mode == "direction_distance_field":
                    direction_distance_field_transition_count += 1
                    unit_guarded_transition_counts[type_id] += 1
                elif generation_mode == "direction_single_silhouette":
                    direction_single_silhouette_transition_count += 1
                    unit_guarded_transition_counts[type_id] += 1
                minimum_correspondence_coverage = min(
                    minimum_correspondence_coverage, coverage
                )
                uncompressed_total += raw_size
                stored_total += len(stored_payload)
                if (transition_index + 1) % 1000 == 0:
                    print(
                        f"generated {transition_index + 1}/{len(transition_specs)} transitions",
                        flush=True,
                    )

            directory_crc32 = binascii.crc32(directory) & 0xFFFFFFFF
            payload_size = stream.tell() - payload_offset
            header = VISUAL_ARCHIVE_HEADER.pack(
                VISUAL_ARCHIVE_MAGIC,
                VISUAL_ARCHIVE_VERSION,
                header_size,
                len(transition_specs),
                VISUAL_ARCHIVE_DIRECTORY_ENTRY.size,
                directory_offset,
                payload_offset,
                payload_size,
                len(archive.data),
                source_sha256,
                source_crc32,
                directory_crc32,
                VISUAL_ARCHIVE_INTERVAL_COUNT,
                VISUAL_ARCHIVE_INTERMEDIATE_COUNT,
            )
            stream.seek(0)
            stream.write(header)
            stream.write(directory)
        pool_succeeded = True
    finally:
        if pool is not None:
            if pool_succeeded:
                pool.close()
            else:
                pool.terminate()
            pool.join()
    temporary.replace(output)

    if manifest_output is not None:
        manifest = {
            "schema": 1,
            "archive": output.name,
            "archive_bytes": output.stat().st_size,
            "source_archive": archive.path.name,
            "source_archive_bytes": len(archive.data),
            "source_archive_sha256": source_sha256.hex(),
            "source_archive_crc32": f"{source_crc32:08x}",
            "unit_filter": only_unit,
            "group_filter": only_group,
            "transition_count": len(transition_specs),
            "intermediate_frames_per_transition": VISUAL_ARCHIVE_INTERMEDIATE_COUNT,
            "generated_intermediate_frame_count": (
                len(transition_specs) * VISUAL_ARCHIVE_INTERMEDIATE_COUNT
            ),
            "quality_strategy": (
                "authored_keyframes_then_uniform_artist_bridge_then_"
                "direction_distance_field_then_direction_safe_quantized_"
                "optical_flow"
            ),
            "pixel_correspondence_transition_count": (
                len(transition_specs) - original_bridge_transition_count -
                authored_keyframe_transition_count -
                optical_flow_transition_count -
                direction_distance_field_transition_count -
                direction_single_silhouette_transition_count
            ),
            "authored_keyframe_transition_count": (
                authored_keyframe_transition_count
            ),
            "original_bridge_transition_count": original_bridge_transition_count,
            "optical_flow_transition_count": optical_flow_transition_count,
            "direction_distance_field_transition_count": (
                direction_distance_field_transition_count
            ),
            "direction_single_silhouette_transition_count": (
                direction_single_silhouette_transition_count
            ),
            "guarded_transition_count": (
                authored_keyframe_transition_count +
                original_bridge_transition_count + optical_flow_transition_count +
                direction_distance_field_transition_count +
                direction_single_silhouette_transition_count
            ),
            "minimum_correspondence_coverage": minimum_correspondence_coverage,
            "flipped_frame_behavior": (
                "mirror_pre_generated_frames_with_original_trc_offsets"
            ),
            "uncompressed_payload_bytes": uncompressed_total,
            "compressed_payload_bytes": stored_total,
            "runtime_generation_required": False,
            "missing_transition_behavior": "exact_original_target_frame",
            "units": [
                {
                    "type_id": record.type_id,
                    "unit_name": record.unit_name,
                    "transition_records": unit_transition_counts[record.type_id],
                    "guarded_transition_records": (
                        unit_guarded_transition_counts[record.type_id]
                    ),
                }
                for record in records
            ],
        }
        manifest_output.parent.mkdir(parents=True, exist_ok=True)
        manifest_output.write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    print(
        f"archive complete transitions={len(transition_specs)} "
        f"authored={authored_keyframe_transition_count} "
        f"bridges={original_bridge_transition_count} "
        f"optical_flow={optical_flow_transition_count} "
        f"direction_field={direction_distance_field_transition_count} "
        f"direction_safe={direction_single_silhouette_transition_count} "
        f"bytes={output.stat().st_size}",
        flush=True,
    )


def validate_visual_animation_archive(source: TrcArchive, archive_path: Path) -> None:
    data = archive_path.read_bytes()
    if len(data) < VISUAL_ARCHIVE_HEADER.size:
        raise ValueError("animation archive header is truncated")
    (
        magic, version, header_size, transition_count, directory_entry_size,
        directory_offset, payload_offset, payload_size, source_size,
        source_sha256, source_crc32, directory_crc32, interval_count,
        intermediate_count,
    ) = VISUAL_ARCHIVE_HEADER.unpack_from(data)
    directory_size = transition_count * VISUAL_ARCHIVE_DIRECTORY_ENTRY.size
    if (magic != VISUAL_ARCHIVE_MAGIC or version != VISUAL_ARCHIVE_VERSION or
            header_size != VISUAL_ARCHIVE_HEADER.size or
            directory_entry_size != VISUAL_ARCHIVE_DIRECTORY_ENTRY.size or
            directory_offset != header_size or
            payload_offset != directory_offset + directory_size or
            payload_size != len(data) - payload_offset or
            interval_count != VISUAL_ARCHIVE_INTERVAL_COUNT or
            intermediate_count != VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
        raise ValueError("animation archive header is incompatible")
    if (source_size != len(source.data) or
            source_sha256 != hashlib.sha256(source.data).digest() or
            source_crc32 != (binascii.crc32(source.data) & 0xFFFFFFFF)):
        raise ValueError("animation archive source identity does not match")
    directory = data[directory_offset:payload_offset]
    if (binascii.crc32(directory) & 0xFFFFFFFF) != directory_crc32:
        raise ValueError("animation archive directory CRC failed")

    prior_key: tuple[int, int, int, int, int] | None = None
    expected_payload_offset = payload_offset
    decoded_frames = 0
    decoded_bytes = 0
    for index in range(transition_count):
        raw = VISUAL_ARCHIVE_DIRECTORY_ENTRY.unpack_from(
            directory, index * VISUAL_ARCHIVE_DIRECTORY_ENTRY.size
        )
        (
            type_id, group, flags, source_frame, target_frame, _left, _top,
            width, height, data_offset, stored_size, uncompressed_size,
            raw_crc32, _flip_transform,
        ) = raw
        key = (type_id, group, source_frame, target_frame, flags)
        if (prior_key is not None and key <= prior_key):
            raise ValueError("animation archive directory is not strictly sorted")
        prior_key = key
        if (type_id >= UNIT_RECORD_COUNT or group >= UNIT_IMAGE_GROUPS or flags != 0 or
                width == 0 or height == 0 or data_offset != expected_payload_offset or
                stored_size == 0 or data_offset + stored_size > len(data)):
            raise ValueError(f"invalid animation directory entry {index}")
        stored = data[data_offset:data_offset + stored_size]
        raw_transition = zlib.decompress(stored)
        if (len(raw_transition) != uncompressed_size or
                (binascii.crc32(raw_transition) & 0xFFFFFFFF) != raw_crc32):
            raise ValueError(f"animation transition CRC failed at {index}")
        cursor = 0
        for _frame in range(VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
            if cursor + 4 > len(raw_transition):
                raise ValueError(f"truncated frame table at transition {index}")
            frame_size = read_u32(raw_transition, cursor)
            cursor += 4
            frame_end = cursor + frame_size
            if frame_end > len(raw_transition):
                raise ValueError(f"truncated frame at transition {index}")
            frame_cursor = cursor
            for _row in range(height):
                if frame_cursor + 2 > frame_end:
                    raise ValueError(f"truncated RLE row at transition {index}")
                row_size = read_u16(raw_transition, frame_cursor)
                frame_cursor += 2
                row_end = frame_cursor + row_size
                if row_end > frame_end:
                    raise ValueError(f"invalid RLE row at transition {index}")
                remaining = width
                while frame_cursor < row_end:
                    token = raw_transition[frame_cursor]
                    frame_cursor += 1
                    if token == 0:
                        if frame_cursor >= row_end:
                            raise ValueError(f"truncated RLE skip at transition {index}")
                        skip = raw_transition[frame_cursor]
                        frame_cursor += 1
                        if skip == 0 or skip > remaining:
                            raise ValueError(f"invalid RLE skip at transition {index}")
                        remaining -= skip
                    else:
                        if remaining == 0:
                            raise ValueError(f"RLE row overflow at transition {index}")
                        remaining -= 1
                frame_cursor = row_end
            if frame_cursor != frame_end:
                raise ValueError(f"RLE frame height mismatch at transition {index}")
            cursor = frame_end
            decoded_frames += 1
        if cursor != len(raw_transition):
            raise ValueError(f"transition has trailing data at {index}")
        decoded_bytes += len(raw_transition)
        expected_payload_offset += stored_size
    if expected_payload_offset != len(data):
        raise ValueError("animation archive payload has trailing data")
    print(
        f"archive valid transitions={transition_count} "
        f"frames={decoded_frames} raw_bytes={decoded_bytes} "
        f"archive_bytes={len(data)}",
        flush=True,
    )


def inventory(archive: TrcArchive, output: Path) -> None:
    records = [decode_unit_record(archive, type_id) for type_id in range(UNIT_RECORD_COUNT)]
    decoded_frame_counts: list[int] = []
    opaque_dot_counts: list[int] = []
    for record in records:
        decoded_frames = 0
        opaque_dots = 0
        for group in record.groups:
            for frame in group:
                pixels = frame.decode_indices()
                if len(pixels) != frame.width * frame.height:
                    raise ValueError(
                        f"unit {record.type_id} produced an invalid decoded frame size"
                    )
                decoded_frames += 1
                opaque_dots += sum(index != 0 for index in pixels)
        decoded_frame_counts.append(decoded_frames)
        opaque_dot_counts.append(opaque_dots)
    total_original_images = sum(record.image_count for record in records)
    total_decoded_images = sum(decoded_frame_counts)
    if total_decoded_images != total_original_images:
        raise ValueError("not every original frame passed RLE validation")
    manifest = {
        "schema": 2,
        "source_archive": archive.path.name,
        "source_archive_sha256": hashlib.sha256(archive.data).hexdigest(),
        "unit_definition_count": UNIT_RECORD_COUNT,
        "records_with_images": sum(record.image_count != 0 for record in records),
        "total_original_images": total_original_images,
        "total_rle_validated_images": total_decoded_images,
        "visual_frame_synthesis": {
            "mode": "offline_pre_generated_compressed_archive",
            "archive": "Jw2_09_144hz.rfa",
            "minimum_render_fps": 61,
            "target_render_fps": 144,
            "source_to_target_phase_intervals": VISUAL_ARCHIVE_INTERVAL_COUNT,
            "intermediate_frames_per_transition": (
                VISUAL_ARCHIVE_INTERMEDIATE_COUNT
            ),
            "eligible_unit_records": sum(record.image_count != 0 for record in records),
            "eligible_original_frames": total_original_images,
            "coverage": (
                "all within-direction sequential, definition-table, and "
                "explicit loop-reset transitions"
            ),
            "direction_interpolation": False,
            "runtime_generation_required": False,
            "missing_transition_behavior": "exact_original_target_frame",
            "authoritative_trc_modified": False,
        },
        "group_totals": [
            sum(len(record.groups[group]) for record in records)
            for group in range(UNIT_IMAGE_GROUPS)
        ],
        "units": [
            {
                "type_id": record.type_id,
                "record_name": record.record_name,
                "unit_name": record.unit_name,
                "record_bytes": record.record_bytes,
                "image_count": record.image_count,
                "image_group_counts": [len(group) for group in record.groups],
                "rle_validated_frame_count": decoded_frame_counts[record.type_id],
                "opaque_dot_count": opaque_dot_counts[record.type_id],
                "pre_generated_transition_coverage": (
                    "within_direction_temporal_graph"
                    if record.image_count else "no_images"
                ),
                "intermediate_frames_per_transition": (
                    VISUAL_ARCHIVE_INTERMEDIATE_COUNT if record.image_count else 0
                ),
            }
            for record in records
        ],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    commands = parser.add_subparsers(dest="command", required=True)

    inventory_parser = commands.add_parser("inventory")
    inventory_parser.add_argument("--output", type=Path, required=True)

    sheet_parser = commands.add_parser("contact-sheet")
    sheet_parser.add_argument("--unit", type=int, required=True)
    sheet_parser.add_argument("--group", type=int, required=True)
    sheet_parser.add_argument("--output", type=Path, required=True)
    sheet_parser.add_argument("--columns", type=int, default=10)
    sheet_parser.add_argument("--margin", type=int, default=4)

    transition_parser = commands.add_parser("transition-sheet")
    transition_parser.add_argument("--unit", type=int, required=True)
    transition_parser.add_argument("--group", type=int, required=True)
    transition_parser.add_argument("--source", type=int, required=True)
    transition_parser.add_argument("--target", type=int, required=True)
    transition_parser.add_argument("--output", type=Path, required=True)
    transition_parser.add_argument("--margin", type=int, default=6)
    transition_parser.add_argument(
        "--method", choices=("pixel-morph", "hybrid-sharp", "crossfade"),
        default="pixel-morph"
    )

    archive_parser = commands.add_parser("archive-144hz")
    archive_parser.add_argument("--output", type=Path, required=True)
    archive_parser.add_argument("--manifest", type=Path)
    archive_parser.add_argument(
        "--keyframe-overrides",
        type=Path,
        default=DEFAULT_KEYFRAME_OVERRIDES,
    )
    archive_parser.add_argument(
        "--jobs", type=int, default=min(4, os.cpu_count() or 1)
    )
    archive_parser.add_argument("--only-unit", type=int)
    archive_parser.add_argument("--only-group", type=int)
    validate_parser = commands.add_parser("validate-144hz")
    validate_parser.add_argument("--input", type=Path, required=True)

    preview_parser = commands.add_parser("preview-144hz")
    preview_parser.add_argument("--input", type=Path, required=True)
    preview_parser.add_argument("--unit", type=int, required=True)
    preview_parser.add_argument("--group", type=int, required=True)
    preview_parser.add_argument("--source", type=int, required=True)
    preview_parser.add_argument("--target", type=int, required=True)
    preview_parser.add_argument("--output", type=Path, required=True)
    preview_parser.add_argument("--margin", type=int, default=6)
    preview_parser.add_argument("--scale", type=int, default=2)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    archive = TrcArchive(args.archive)
    if args.command == "inventory":
        inventory(archive, args.output)
    elif args.command == "contact-sheet":
        unit = decode_unit_record(archive, args.unit)
        write_contact_sheet(unit, args.group, args.output, args.columns, args.margin)
    elif args.command == "transition-sheet":
        unit = decode_unit_record(archive, args.unit)
        write_transition_sheet(
            unit, args.group, args.source, args.target, args.output, args.margin,
            args.method
        )
    elif args.command == "archive-144hz":
        write_visual_animation_archive(
            archive,
            args.output,
            args.manifest,
            args.jobs,
            args.keyframe_overrides,
            args.only_unit,
            args.only_group,
        )
    elif args.command == "validate-144hz":
        validate_visual_animation_archive(archive, args.input)
    elif args.command == "preview-144hz":
        write_pre_generated_transition_sheet(
            archive, args.input, args.unit, args.group, args.source,
            args.target, args.output, args.margin, args.scale
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
