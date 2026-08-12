#!/usr/bin/env python3
"""Build the complete 144 Hz unit-animation archive with FFmpeg minterpolate.

Only temporal edges from one original direction row are interpolated.  RGB,
body alpha, and token-1 ground-shadow masks are processed separately on a
black matte.  The two masks are thresholded back to binary and every body
pixel is requantized to colors present in its original animation strip.
"""

from __future__ import annotations

import argparse
import binascii
import concurrent.futures
import hashlib
import json
import pickle
import shutil
import subprocess
import sys
import tempfile
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

import unit_sprite_assets as assets


GENERATOR_SCHEMA = 1
LOOKAHEAD_FRAMES = 3
CANVAS_MARGIN = 4
EDGE_BATCH_SIZE = 32
MINTERPOLATE = (
    "minterpolate=fps=12:mi_mode=mci:mc_mode=aobmc:"
    "me_mode=bidir:vsbmc=1"
)

_CACHE_WORKER_RECORDS: list[assets.UnitRecord] | None = None
_CACHE_WORKER_FFMPEG: Path | None = None
_CACHE_WORKER_CACHE_ROOT: Path | None = None
_CACHE_WORKER_SOURCE_SHA256 = ""
_CACHE_WORKER_FFMPEG_VERSION = ""


@dataclass(frozen=True)
class AnimationPlan:
    frames: tuple[int, ...]
    looped: bool
    source: str


@dataclass(frozen=True)
class SelectedEdge:
    source: int
    target: int
    plan_index: int
    interval_index: int


def find_ffmpeg(explicit: Path | None, workspace: Path) -> Path:
    if explicit is not None:
        candidate = explicit.resolve()
        if candidate.is_file():
            return candidate
        raise FileNotFoundError(f"FFmpeg executable does not exist: {candidate}")
    system = shutil.which("ffmpeg")
    if system:
        return Path(system).resolve()
    candidates = sorted(workspace.glob("third_party/ffmpeg/**/ffmpeg.exe"))
    if candidates:
        return candidates[0].resolve()
    raise FileNotFoundError("FFmpeg was not found; pass --ffmpeg explicitly")


def verify_ffmpeg(ffmpeg: Path) -> str:
    version = subprocess.run(
        [str(ffmpeg), "-hide_banner", "-version"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout.splitlines()[0]
    filters = subprocess.run(
        [str(ffmpeg), "-hide_banner", "-filters"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout
    if "minterpolate" not in filters:
        raise RuntimeError("this FFmpeg build does not contain minterpolate")
    return version


def group_plans(unit: assets.UnitRecord, group: int) -> list[AnimationPlan]:
    return [
        AnimationPlan(sequence, looped, source)
        for sequence_group, sequence, looped, source in
        assets.animation_temporal_sequences(unit)
        if sequence_group == group
    ]


def plan_edges(plan: AnimationPlan) -> list[tuple[int, int, int]]:
    result = [
        (source, target, index)
        for index, (source, target) in enumerate(
            zip(plan.frames, plan.frames[1:])
        )
        if source != target
    ]
    if plan.looped and plan.frames[-1] != plan.frames[0]:
        result.append((plan.frames[-1], plan.frames[0], len(plan.frames) - 1))
    return result


def select_edges(plans: list[AnimationPlan]) -> list[SelectedEdge]:
    selected: dict[tuple[int, int], SelectedEdge] = {}
    for plan_index, plan in enumerate(plans):
        for source, target, interval_index in plan_edges(plan):
            selected.setdefault(
                (source, target),
                SelectedEdge(source, target, plan_index, interval_index),
            )
    return sorted(selected.values(), key=lambda edge: (edge.source, edge.target))


def cyclic_prefix(values: tuple[int, ...], count: int) -> list[int]:
    return [values[index % len(values)] for index in range(count)]


def build_input_stream(
    plans: list[AnimationPlan], selected: list[SelectedEdge]
) -> tuple[list[int], list[tuple[int, SelectedEdge]]]:
    selected_by_plan: dict[int, list[SelectedEdge]] = {}
    for edge in selected:
        selected_by_plan.setdefault(edge.plan_index, []).append(edge)

    stream: list[int] = []
    selected_intervals: list[tuple[int, SelectedEdge]] = []
    for plan_index in sorted(selected_by_plan):
        plan = plans[plan_index]
        prefix = [plan.frames[0]] * LOOKAHEAD_FRAMES
        lookahead = (
            cyclic_prefix(plan.frames, LOOKAHEAD_FRAMES)
            if plan.looped
            else [plan.frames[-1]] * LOOKAHEAD_FRAMES
        )
        segment_start = len(stream)
        stream.extend(prefix)
        stream.extend(plan.frames)
        stream.extend(lookahead)
        stream.extend([lookahead[-1]] * LOOKAHEAD_FRAMES)
        for edge in selected_by_plan[plan_index]:
            input_interval = segment_start + LOOKAHEAD_FRAMES + edge.interval_index
            selected_intervals.append((input_interval, edge))
    selected_intervals.sort()
    return stream, selected_intervals


def group_canvas(
    frames: list[assets.SpriteFrame], margin: int = CANVAS_MARGIN
) -> tuple[int, int, int, int]:
    left, top, right, bottom = assets.frame_bounds(frames)
    width = right - left + margin * 2
    height = bottom - top + margin * 2
    if width & 1:
        width += 1
    if height & 1:
        height += 1
    # FFmpeg's minterpolate filter requires both dimensions to be at least 32.
    # Extra pixels remain black/transparent and never enter the sparse output.
    return left - margin, top - margin, max(32, width), max(32, height)


def render_original_planes(
    unit: assets.UnitRecord,
    group: int,
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
) -> tuple[list[bytes], list[bytes], list[bytes], list[set[int]]]:
    rgb_planes: list[bytes] = []
    body_planes: list[bytes] = []
    shadow_planes: list[bytes] = []
    tokens_per_frame: list[set[int]] = []
    for frame in unit.groups[group]:
        rgb = np.zeros((height, width, 3), dtype=np.uint8)
        body = np.zeros((height, width), dtype=np.uint8)
        shadow = np.zeros((height, width), dtype=np.uint8)
        tokens: set[int] = set()
        indices = np.frombuffer(frame.decode_indices(), dtype=np.uint8).reshape(
            frame.height, frame.width
        )
        start_x = frame.offset_x - origin_x
        start_y = frame.offset_y - origin_y
        for y, x in np.argwhere(indices != 0):
            token = int(indices[y, x])
            target_x = start_x + int(x)
            target_y = start_y + int(y)
            if token == 1:
                shadow[target_y, target_x] = 255
            else:
                red, green, blue, _alpha = assets.palette_rgba(unit.palette, token)
                rgb[target_y, target_x] = (red, green, blue)
                body[target_y, target_x] = 255
                tokens.add(token)
        rgb_planes.append(rgb.tobytes())
        body_planes.append(body.tobytes())
        shadow_planes.append(shadow.tobytes())
        tokens_per_frame.append(tokens)
    return rgb_planes, body_planes, shadow_planes, tokens_per_frame


def write_stream(path: Path, planes: list[bytes], stream: list[int]) -> None:
    with path.open("wb") as output:
        for frame_index in stream:
            output.write(planes[frame_index])


def selection_expression(intervals: list[tuple[int, SelectedEdge]]) -> str:
    clauses = []
    for input_interval, _edge in intervals:
        first = input_interval * assets.VISUAL_ARCHIVE_INTERVAL_COUNT + 1
        last = first + assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT - 1
        clauses.append(f"between(n\\,{first}\\,{last})")
    return "+".join(clauses)


def run_minterpolate(
    ffmpeg: Path,
    input_path: Path,
    output_path: Path,
    width: int,
    height: int,
    pixel_format: str,
    intervals: list[tuple[int, SelectedEdge]],
) -> None:
    expected_frames = (
        len(intervals) * assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
    )
    expression = selection_expression(intervals)
    command = [
        str(ffmpeg),
        "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo",
        "-pixel_format", pixel_format,
        "-video_size", f"{width}x{height}",
        "-framerate", "1",
        "-i", str(input_path),
        "-vf", f"{MINTERPOLATE},select={expression},setpts=N/12/TB",
        "-fps_mode", "passthrough",
        "-frames:v", str(expected_frames),
        "-f", "rawvideo",
        "-pix_fmt", pixel_format,
        str(output_path),
    ]
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"FFmpeg minterpolate failed ({pixel_format}):\n{completed.stderr}"
        )
    bytes_per_pixel = 3 if pixel_format == "rgb24" else 1
    expected_bytes = width * height * bytes_per_pixel * expected_frames
    actual_bytes = output_path.stat().st_size
    if actual_bytes != expected_bytes:
        raise RuntimeError(
            f"FFmpeg output size mismatch: {actual_bytes} != {expected_bytes}"
        )


def palette_for_plan(
    unit: assets.UnitRecord,
    group: int,
    plan: AnimationPlan,
    tokens_per_frame: list[set[int]],
) -> tuple[Image.Image, np.ndarray]:
    tokens = sorted({
        token for frame_index in plan.frames
        for token in tokens_per_frame[frame_index]
    })
    if not tokens:
        tokens = sorted({
            token for frame_tokens in tokens_per_frame for token in frame_tokens
        })
    colors = [assets.palette_rgba(unit.palette, token)[:3] for token in tokens]
    palette_image = Image.new("P", (1, 1))
    flat = [channel for color in colors for channel in color]
    pad_color = colors[0] if colors else (0, 0, 0)
    while len(flat) < 768:
        flat.extend(pad_color)
    palette_image.putpalette(flat)
    token_lookup = np.full(
        256, tokens[0] if tokens else 0, dtype=np.uint8
    )
    for palette_index, token in enumerate(tokens):
        token_lookup[palette_index] = token
    return palette_image, token_lookup


def decoded_authored_frame(
    rgb_bytes: bytes,
    body_bytes: bytes,
    shadow_bytes: bytes,
    width: int,
    height: int,
    origin_x: int,
    origin_y: int,
    palette_image: Image.Image,
    token_lookup: np.ndarray,
) -> dict[tuple[int, int], int]:
    rgb_image = Image.frombytes("RGB", (width, height), rgb_bytes)
    quantized = rgb_image.quantize(
        palette=palette_image,
        dither=Image.Dither.NONE,
    )
    palette_indices = np.frombuffer(quantized.tobytes(), dtype=np.uint8)
    body = np.frombuffer(body_bytes, dtype=np.uint8) >= 128
    shadow = np.frombuffer(shadow_bytes, dtype=np.uint8) >= 128
    body_positions = np.flatnonzero(body)
    shadow_positions = np.flatnonzero(shadow & ~body)
    dots: dict[tuple[int, int], int] = {}
    for position, token in zip(
        body_positions.tolist(),
        token_lookup[palette_indices[body_positions]].tolist(),
    ):
        y, x = divmod(position, width)
        dots[(origin_x + x, origin_y + y)] = token
    for position in shadow_positions.tolist():
        y, x = divmod(position, width)
        dots[(origin_x + x, origin_y + y)] = 1
    return dots


def flip_transform(frame: assets.SpriteFrame) -> int:
    flip_origin_x = (
        assets.signed_u32(frame.metadata[4]) + frame.width + frame.offset_x
    )
    flip_delta_y = assets.signed_u32(frame.metadata[5]) - frame.offset_y
    if not (-0x8000 <= flip_origin_x <= 0x7FFF and
            -0x8000 <= flip_delta_y <= 0x7FFF):
        raise ValueError("flip transform exceeds archive range")
    return ((flip_origin_x & 0xFFFF) |
            ((flip_delta_y & 0xFFFF) << 16))


def generate_group_records(
    ffmpeg: Path,
    unit: assets.UnitRecord,
    group: int,
    work_root: Path,
) -> list[tuple[int, int, int, int, int, int, bytes, int, int, int]]:
    plans = group_plans(unit, group)
    selected = select_edges(plans)
    expected = {
        (source, target)
        for pair_group, source, target in
        assets.animation_temporal_transition_pairs(unit)
        if pair_group == group
    }
    if {(edge.source, edge.target) for edge in selected} != expected:
        raise RuntimeError(
            f"unit {unit.type_id} group {group} temporal edge plan mismatch"
        )
    if not selected:
        return []

    origin_x, origin_y, width, height = group_canvas(unit.groups[group])
    rgb_planes, body_planes, shadow_planes, tokens_per_frame = (
        render_original_planes(
            unit, group, origin_x, origin_y, width, height
        )
    )
    group_root = work_root / f"unit_{unit.type_id:03d}_group_{group:02d}"
    group_root.mkdir(parents=True, exist_ok=True)
    rgb_frame_bytes = width * height * 3
    mask_frame_bytes = width * height
    records = []
    for batch_start in range(0, len(selected), EDGE_BATCH_SIZE):
        batch = selected[batch_start:batch_start + EDGE_BATCH_SIZE]
        stream, intervals = build_input_stream(plans, batch)
        stem = f"batch_{batch_start // EDGE_BATCH_SIZE:04d}"
        rgb_input = group_root / f"{stem}_rgb_input.raw"
        body_input = group_root / f"{stem}_body_input.raw"
        shadow_input = group_root / f"{stem}_shadow_input.raw"
        rgb_output = group_root / f"{stem}_rgb_output.raw"
        body_output = group_root / f"{stem}_body_output.raw"
        shadow_output = group_root / f"{stem}_shadow_output.raw"
        write_stream(rgb_input, rgb_planes, stream)
        write_stream(body_input, body_planes, stream)
        write_stream(shadow_input, shadow_planes, stream)
        run_minterpolate(
            ffmpeg, rgb_input, rgb_output, width, height, "rgb24", intervals
        )
        run_minterpolate(
            ffmpeg, body_input, body_output, width, height, "gray", intervals
        )
        run_minterpolate(
            ffmpeg, shadow_input, shadow_output, width, height, "gray", intervals
        )

        with (rgb_output.open("rb") as rgb_stream,
              body_output.open("rb") as body_stream,
              shadow_output.open("rb") as shadow_stream):
            for _input_interval, edge in intervals:
                palette_image, token_lookup = palette_for_plan(
                    unit, group, plans[edge.plan_index], tokens_per_frame
                )
                authored = []
                for _phase in range(assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
                    authored.append(decoded_authored_frame(
                        rgb_stream.read(rgb_frame_bytes),
                        body_stream.read(mask_frame_bytes),
                        shadow_stream.read(mask_frame_bytes),
                        width,
                        height,
                        origin_x,
                        origin_y,
                        palette_image,
                        token_lookup,
                    ))
                source_frame = unit.groups[group][edge.source]
                target_frame = unit.groups[group][edge.target]
                source_transform = flip_transform(source_frame)
                if source_transform != flip_transform(target_frame):
                    raise ValueError(
                        "transition crosses incompatible flip transforms"
                    )
                (
                    left, top, transition_width, transition_height,
                    raw, _mode, _coverage,
                ) = assets.build_archive_transition(
                    source_frame,
                    target_frame,
                    False,
                    authored_frames=authored,
                )
                records.append((
                    edge.source,
                    edge.target,
                    left,
                    top,
                    transition_width,
                    transition_height,
                    zlib.compress(raw, 9),
                    len(raw),
                    binascii.crc32(raw) & 0xFFFFFFFF,
                    source_transform,
                ))
        for path in (
            rgb_input, body_input, shadow_input,
            rgb_output, body_output, shadow_output,
        ):
            path.unlink()
    shutil.rmtree(group_root)
    records.sort(key=lambda record: (record[0], record[1]))
    return records


def cache_path(cache_root: Path, type_id: int, group: int) -> Path:
    return cache_root / f"unit_{type_id:03d}_group_{group:02d}.pickle.z"


def load_cached_group(
    path: Path, source_sha256: str, ffmpeg_version: str
) -> list[tuple[int, int, int, int, int, int, bytes, int, int, int]] | None:
    if not path.is_file():
        return None
    document = pickle.loads(zlib.decompress(path.read_bytes()))
    if (document.get("schema") != GENERATOR_SCHEMA or
            document.get("source_sha256") != source_sha256 or
            document.get("ffmpeg_version") != ffmpeg_version):
        return None
    return document["records"]


def store_cached_group(
    path: Path,
    source_sha256: str,
    ffmpeg_version: str,
    records: list[tuple[int, int, int, int, int, int, bytes, int, int, int]],
) -> None:
    document = {
        "schema": GENERATOR_SCHEMA,
        "source_sha256": source_sha256,
        "ffmpeg_version": ffmpeg_version,
        "records": records,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(zlib.compress(pickle.dumps(document, protocol=5), 1))
    temporary.replace(path)


def initialize_cache_worker(
    archive_path: str,
    ffmpeg_path: str,
    cache_root: str,
    source_sha256: str,
    ffmpeg_version: str,
) -> None:
    global _CACHE_WORKER_RECORDS, _CACHE_WORKER_FFMPEG
    global _CACHE_WORKER_CACHE_ROOT, _CACHE_WORKER_SOURCE_SHA256
    global _CACHE_WORKER_FFMPEG_VERSION
    source = assets.TrcArchive(Path(archive_path))
    _CACHE_WORKER_RECORDS = [
        assets.decode_unit_record(source, type_id)
        for type_id in range(assets.UNIT_RECORD_COUNT)
    ]
    _CACHE_WORKER_FFMPEG = Path(ffmpeg_path)
    _CACHE_WORKER_CACHE_ROOT = Path(cache_root)
    _CACHE_WORKER_SOURCE_SHA256 = source_sha256
    _CACHE_WORKER_FFMPEG_VERSION = ffmpeg_version


def generate_cache_worker(spec: tuple[int, int]) -> tuple[int, int, int, float]:
    if (_CACHE_WORKER_RECORDS is None or _CACHE_WORKER_FFMPEG is None or
            _CACHE_WORKER_CACHE_ROOT is None):
        raise RuntimeError("FFmpeg cache worker was not initialized")
    type_id, group = spec
    started = time.monotonic()
    with tempfile.TemporaryDirectory(
        prefix=f"ranker_ffmpeg_144hz_{type_id:03d}_{group:02d}_"
    ) as temporary:
        records = generate_group_records(
            _CACHE_WORKER_FFMPEG,
            _CACHE_WORKER_RECORDS[type_id],
            group,
            Path(temporary),
        )
    store_cached_group(
        cache_path(_CACHE_WORKER_CACHE_ROOT, type_id, group),
        _CACHE_WORKER_SOURCE_SHA256,
        _CACHE_WORKER_FFMPEG_VERSION,
        records,
    )
    return type_id, group, len(records), time.monotonic() - started


def precompute_group_caches(
    source: assets.TrcArchive,
    ffmpeg: Path,
    cache_root: Path,
    source_sha256: str,
    ffmpeg_version: str,
    group_specs: list[tuple[int, int]],
    jobs: int,
) -> None:
    missing = [
        spec for spec in group_specs
        if load_cached_group(
            cache_path(cache_root, spec[0], spec[1]),
            source_sha256,
            ffmpeg_version,
        ) is None
    ]
    if not missing:
        print("all group caches are already complete", flush=True)
        return
    jobs = max(1, min(jobs, 8, len(missing)))
    print(
        f"precomputing missing_groups={len(missing)} jobs={jobs} "
        f"cached_groups={len(group_specs) - len(missing)}",
        flush=True,
    )
    initializer_args = (
        str(source.path.resolve()),
        str(ffmpeg.resolve()),
        str(cache_root.resolve()),
        source_sha256,
        ffmpeg_version,
    )
    if jobs == 1:
        initialize_cache_worker(*initializer_args)
        futures = (generate_cache_worker(spec) for spec in missing)
        for completed, result in enumerate(futures, 1):
            type_id, group, count, elapsed = result
            print(
                f"cache [{completed}/{len(missing)}] unit={type_id:03d} "
                f"group={group:02d} transitions={count} elapsed={elapsed:.1f}s",
                flush=True,
            )
        return
    with concurrent.futures.ProcessPoolExecutor(
        max_workers=jobs,
        initializer=initialize_cache_worker,
        initargs=initializer_args,
    ) as executor:
        pending = {
            executor.submit(generate_cache_worker, spec): spec for spec in missing
        }
        for completed, future in enumerate(
            concurrent.futures.as_completed(pending), 1
        ):
            type_id, group, count, elapsed = future.result()
            print(
                f"cache [{completed}/{len(missing)}] unit={type_id:03d} "
                f"group={group:02d} transitions={count} elapsed={elapsed:.1f}s",
                flush=True,
            )


def write_archive(
    source: assets.TrcArchive,
    records: list[assets.UnitRecord],
    output: Path,
    manifest_path: Path,
    ffmpeg: Path,
    ffmpeg_version: str,
    cache_root: Path,
    keep_cache: bool,
    jobs: int,
) -> None:
    source_sha256 = hashlib.sha256(source.data).hexdigest()
    source_crc32 = binascii.crc32(source.data) & 0xFFFFFFFF
    group_specs = []
    total_transitions = 0
    unit_counts = [0] * assets.UNIT_RECORD_COUNT
    for unit in records:
        pairs = assets.animation_temporal_transition_pairs(unit)
        unit_counts[unit.type_id] = len(pairs)
        total_transitions += len(pairs)
        for group in sorted({group for group, _source, _target in pairs}):
            group_specs.append((unit.type_id, group))

    precompute_group_caches(
        source,
        ffmpeg,
        cache_root,
        source_sha256,
        ffmpeg_version,
        group_specs,
        jobs,
    )

    header_size = assets.VISUAL_ARCHIVE_HEADER.size
    directory_offset = header_size
    payload_offset = (
        directory_offset +
        total_transitions * assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.size
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    directory = bytearray()
    generated = 0
    raw_total = 0
    stored_total = 0
    start_time = time.monotonic()

    with temporary.open("w+b") as output_stream:
        output_stream.write(bytes(payload_offset))
        for spec_index, (type_id, group) in enumerate(group_specs, 1):
            cache_file = cache_path(cache_root, type_id, group)
            group_records = load_cached_group(
                cache_file, source_sha256, ffmpeg_version
            )
            if group_records is None:
                raise RuntimeError(
                    f"missing completed cache for unit {type_id} group {group}"
                )
            for (
                source_frame, target_frame, left, top, width, height,
                stored, raw_size, raw_crc32, transform,
            ) in group_records:
                data_offset = output_stream.tell()
                output_stream.write(stored)
                directory.extend(assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.pack(
                    type_id,
                    group,
                    0,
                    source_frame,
                    target_frame,
                    left,
                    top,
                    width,
                    height,
                    data_offset,
                    len(stored),
                    raw_size,
                    raw_crc32,
                    transform,
                ))
                generated += 1
                raw_total += raw_size
                stored_total += len(stored)
            elapsed = time.monotonic() - start_time
            print(
                f"[{spec_index}/{len(group_specs)}] unit={type_id:03d} "
                f"group={group:02d} transitions={generated}/{total_transitions} "
                f"source=cache elapsed={elapsed:.1f}s",
                flush=True,
            )

        if generated != total_transitions:
            raise RuntimeError(
                f"generated transition count mismatch: {generated} != "
                f"{total_transitions}"
            )
        directory_crc32 = binascii.crc32(directory) & 0xFFFFFFFF
        payload_size = output_stream.tell() - payload_offset
        header = assets.VISUAL_ARCHIVE_HEADER.pack(
            assets.VISUAL_ARCHIVE_MAGIC,
            assets.VISUAL_ARCHIVE_VERSION,
            header_size,
            total_transitions,
            assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.size,
            directory_offset,
            payload_offset,
            payload_size,
            len(source.data),
            bytes.fromhex(source_sha256),
            source_crc32,
            directory_crc32,
            assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
            assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT,
        )
        output_stream.seek(0)
        output_stream.write(header)
        output_stream.write(directory)
    temporary.replace(output)

    final_source_sha256 = hashlib.sha256(source.path.read_bytes()).hexdigest()
    if final_source_sha256 != source_sha256:
        raise RuntimeError("the authoritative source TRC changed during generation")
    manifest = {
        "schema": 2,
        "archive": output.name,
        "archive_bytes": output.stat().st_size,
        "source_archive": source.path.name,
        "source_archive_bytes": len(source.data),
        "source_archive_sha256": source_sha256,
        "source_archive_crc32": f"{source_crc32:08x}",
        "source_archive_unchanged": True,
        "ffmpeg": str(ffmpeg),
        "ffmpeg_version": ffmpeg_version,
        "interpolation": MINTERPOLATE,
        "interval_count": assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
        "intermediate_frames_per_transition": (
            assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
        ),
        "transition_count": total_transitions,
        "generated_intermediate_frame_count": (
            total_transitions * assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
        ),
        "eligible_unit_records": sum(count > 0 for count in unit_counts),
        "total_original_images": sum(unit.image_count for unit in records),
        "quality_strategy": (
            "separate_black_matte_rgb_body_alpha_shadow_alpha_ffmpeg_"
            "minterpolate_binary_masks_original_strip_palette"
        ),
        "direction_interpolation": False,
        "transparent_background_token": 0,
        "ground_shadow_token": 1,
        "runtime_generation_required": False,
        "missing_transition_behavior": "exact_original_target_frame",
        "uncompressed_payload_bytes": raw_total,
        "compressed_payload_bytes": stored_total,
        "units": [
            {
                "type_id": unit.type_id,
                "unit_name": unit.unit_name,
                "original_images": unit.image_count,
                "temporal_transition_records": unit_counts[unit.type_id],
                "intermediate_frames": (
                    unit_counts[unit.type_id] *
                    assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
                ),
            }
            for unit in records
        ],
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if not keep_cache:
        shutil.rmtree(cache_root, ignore_errors=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--keep-cache", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    workspace = Path(__file__).resolve().parents[3]
    ffmpeg = find_ffmpeg(args.ffmpeg, workspace)
    ffmpeg_version = verify_ffmpeg(ffmpeg)
    source = assets.TrcArchive(args.archive.resolve())
    records = [
        assets.decode_unit_record(source, type_id)
        for type_id in range(assets.UNIT_RECORD_COUNT)
    ]
    print(
        f"source={source.path} ffmpeg={ffmpeg_version} "
        f"units={len(records)} images={sum(unit.image_count for unit in records)}",
        flush=True,
    )
    write_archive(
        source,
        records,
        args.output.resolve(),
        args.manifest.resolve(),
        ffmpeg,
        ffmpeg_version,
        args.cache.resolve(),
        args.keep_cache,
        args.jobs,
    )
    assets.validate_visual_animation_archive(source, args.output.resolve())
    print("all-unit FFmpeg 144 Hz archive: PASS", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
