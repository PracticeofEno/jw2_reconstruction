#!/usr/bin/env python3
"""Browse every pre-generated Ranker 144 Hz unit transition."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import re
import struct
import tkinter as tk
import zlib
from collections import OrderedDict, defaultdict
from dataclasses import dataclass
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import unit_sprite_assets as assets


@dataclass(frozen=True)
class Transition:
    ordinal: int
    unit_type: int
    group: int
    source: int
    target: int
    left: int
    top: int
    width: int
    height: int
    data_offset: int
    stored_size: int
    uncompressed_size: int
    raw_crc32: int


def rgba_png_bytes(width: int, height: int, rgba: bytes) -> bytes:
    scanlines = b"".join(
        b"\0" + rgba[y * width * 4:(y + 1) * width * 4]
        for y in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + assets.png_chunk(
            b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
        )
        + assets.png_chunk(b"IDAT", zlib.compress(scanlines, 9))
        + assets.png_chunk(b"IEND", b"")
    )


class AnimationCatalog:
    def __init__(self, source_path: Path, animation_path: Path) -> None:
        self.source_path = source_path
        self.animation_path = animation_path
        self.source = assets.TrcArchive(source_path)
        self.transitions: list[Transition] = []
        self.by_unit_group: dict[tuple[int, int], list[Transition]] = defaultdict(list)
        self.unit_names: dict[int, str] = {}
        self._unit_cache: dict[int, assets.UnitRecord] = {}
        self._transition_cache: OrderedDict[int, list[assets.SpriteFrame]] = OrderedDict()
        self._load_directory()

    def _load_directory(self) -> None:
        with self.animation_path.open("rb") as stream:
            header = stream.read(assets.VISUAL_ARCHIVE_HEADER.size)
            if len(header) != assets.VISUAL_ARCHIVE_HEADER.size:
                raise ValueError("144 Hz archive header is truncated")
            (
                magic, version, header_size, transition_count,
                directory_entry_size, directory_offset, payload_offset,
                payload_size, source_size, source_sha256, source_crc32,
                directory_crc32, interval_count, intermediate_count,
            ) = assets.VISUAL_ARCHIVE_HEADER.unpack(header)
            expected_directory_size = (
                transition_count * assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.size
            )
            if (
                magic != assets.VISUAL_ARCHIVE_MAGIC
                or version != assets.VISUAL_ARCHIVE_VERSION
                or header_size != assets.VISUAL_ARCHIVE_HEADER.size
                or directory_entry_size
                    != assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.size
                or directory_offset != header_size
                or payload_offset != directory_offset + expected_directory_size
                or interval_count != assets.VISUAL_ARCHIVE_INTERVAL_COUNT
                or intermediate_count != assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
            ):
                raise ValueError("144 Hz archive format is incompatible")
            if (
                source_size != len(self.source.data)
                or source_sha256 != hashlib.sha256(self.source.data).digest()
                or source_crc32
                    != (binascii.crc32(self.source.data) & 0xFFFFFFFF)
            ):
                raise ValueError("144 Hz archive does not match Jw2_09.trc")
            stream.seek(0, 2)
            file_size = stream.tell()
            if payload_size != file_size - payload_offset:
                raise ValueError("144 Hz archive payload bounds are invalid")
            stream.seek(directory_offset)
            directory = stream.read(expected_directory_size)
        if (
            len(directory) != expected_directory_size
            or (binascii.crc32(directory) & 0xFFFFFFFF) != directory_crc32
        ):
            raise ValueError("144 Hz archive directory checksum failed")

        for ordinal in range(transition_count):
            entry = assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.unpack_from(
                directory,
                ordinal * assets.VISUAL_ARCHIVE_DIRECTORY_ENTRY.size,
            )
            (
                unit_type, group, flags, source, target, left, top,
                width, height, data_offset, stored_size, uncompressed_size,
                raw_crc32, _flip_transform,
            ) = entry
            if flags != 0:
                raise ValueError("viewer expects mirrored normal-frame archive data")
            transition = Transition(
                ordinal, unit_type, group, source, target, left, top,
                width, height, data_offset, stored_size, uncompressed_size,
                raw_crc32,
            )
            self.transitions.append(transition)
            self.by_unit_group[(unit_type, group)].append(transition)

        unit_types = sorted({transition.unit_type for transition in self.transitions})
        for unit_type in unit_types:
            record = self.unit(unit_type)
            self.unit_names[unit_type] = record.unit_name or record.record_name

    def unit(self, unit_type: int) -> assets.UnitRecord:
        if unit_type not in self._unit_cache:
            self._unit_cache[unit_type] = assets.decode_unit_record(
                self.source, unit_type
            )
        return self._unit_cache[unit_type]

    def groups(self, unit_type: int) -> list[int]:
        return sorted(
            group for candidate_type, group in self.by_unit_group
            if candidate_type == unit_type
        )

    def generated_frames(self, transition: Transition) -> list[assets.SpriteFrame]:
        cached = self._transition_cache.get(transition.ordinal)
        if cached is not None:
            self._transition_cache.move_to_end(transition.ordinal)
            return cached
        with self.animation_path.open("rb") as stream:
            stream.seek(transition.data_offset)
            stored = stream.read(transition.stored_size)
        if len(stored) != transition.stored_size:
            raise ValueError("selected transition payload is truncated")
        raw = zlib.decompress(stored)
        if (
            len(raw) != transition.uncompressed_size
            or (binascii.crc32(raw) & 0xFFFFFFFF) != transition.raw_crc32
        ):
            raise ValueError("selected transition checksum failed")
        frames: list[assets.SpriteFrame] = []
        cursor = 0
        for _ in range(assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT):
            if cursor + 4 > len(raw):
                raise ValueError("selected transition frame table is truncated")
            size = assets.read_u32(raw, cursor)
            cursor += 4
            end = cursor + size
            if end > len(raw):
                raise ValueError("selected transition frame is truncated")
            metadata = (
                transition.width,
                transition.height,
                transition.left & 0xFFFFFFFF,
                transition.top & 0xFFFFFFFF,
                0,
                0,
            )
            frame = assets.SpriteFrame(metadata, raw[cursor:end])
            frame.decode_indices()
            frames.append(frame)
            cursor = end
        if cursor != len(raw):
            raise ValueError("selected transition has trailing data")
        self._transition_cache[transition.ordinal] = frames
        while len(self._transition_cache) > 64:
            self._transition_cache.popitem(last=False)
        return frames

    def render(self, transition: Transition, scale: int) -> tuple[bytes, int, int]:
        unit = self.unit(transition.unit_type)
        group = unit.groups[transition.group]
        if transition.source >= len(group) or transition.target >= len(group):
            raise ValueError("selected transition endpoint is outside the TRC group")
        frames = [
            group[transition.source],
            *self.generated_frames(transition),
            group[transition.target],
        ]
        left, top, right, bottom = assets.frame_bounds(frames)
        margin = 8
        cell_width = max(1, right - left) + margin * 2
        cell_height = max(1, bottom - top) + margin * 2
        width = cell_width * len(frames)
        height = cell_height
        canvas = bytearray(width * height * 4)
        assets.fill_checkerboard(canvas, width, height)
        for index, frame in enumerate(frames):
            assets.draw_frame_preview(
                canvas,
                width,
                height,
                frame,
                unit.palette,
                index * cell_width + margin - left,
                margin - top,
            )
        width, height, rgba = assets.scale_rgba_nearest(
            bytes(canvas), width, height, scale
        )
        return rgba_png_bytes(width, height, rgba), width, height


class AnimationViewer:
    def __init__(self, root: tk.Tk, catalog: AnimationCatalog, output: Path) -> None:
        self.root = root
        self.catalog = catalog
        self.output = output
        self.output.mkdir(parents=True, exist_ok=True)
        self.unit_types = sorted(catalog.unit_names)
        self.unit_value_to_type: dict[str, int] = {}
        self.group_values: list[int] = []
        self.current_transitions: list[Transition] = []
        self.current_png = b""
        self.current_transition: Transition | None = None
        self.photo: tk.PhotoImage | None = None

        root.title("Ranker 144 Hz 전체 사전 생성 애니메이션 뷰어")
        root.geometry("1500x850")
        root.minsize(900, 500)

        toolbar = ttk.Frame(root, padding=8)
        toolbar.pack(fill=tk.X)
        ttk.Label(toolbar, text="유닛").grid(row=0, column=0, padx=(0, 4))
        self.unit_combo = ttk.Combobox(toolbar, state="readonly", width=32)
        unit_values: list[str] = []
        for unit_type in self.unit_types:
            count = sum(
                len(catalog.by_unit_group[(unit_type, group)])
                for group in catalog.groups(unit_type)
            )
            value = (
                f"{unit_type:03d}  {catalog.unit_names[unit_type]}  "
                f"({count:,} 전이)"
            )
            unit_values.append(value)
            self.unit_value_to_type[value] = unit_type
        self.unit_combo["values"] = unit_values
        self.unit_combo.grid(row=0, column=1, padx=(0, 10))
        self.unit_combo.bind("<<ComboboxSelected>>", self._unit_changed)

        ttk.Label(toolbar, text="그룹").grid(row=0, column=2, padx=(0, 4))
        self.group_combo = ttk.Combobox(toolbar, state="readonly", width=15)
        self.group_combo.grid(row=0, column=3, padx=(0, 10))
        self.group_combo.bind("<<ComboboxSelected>>", self._group_changed)

        ttk.Label(toolbar, text="전이").grid(row=0, column=4, padx=(0, 4))
        self.transition_combo = ttk.Combobox(toolbar, state="readonly", width=26)
        self.transition_combo.grid(row=0, column=5, padx=(0, 10))
        self.transition_combo.bind("<<ComboboxSelected>>", self._render_selected)

        ttk.Button(toolbar, text="◀ 이전", command=self.previous).grid(
            row=0, column=6, padx=2
        )
        ttk.Button(toolbar, text="다음 ▶", command=self.next).grid(
            row=0, column=7, padx=2
        )
        ttk.Label(toolbar, text="확대").grid(row=0, column=8, padx=(12, 4))
        self.scale_combo = ttk.Combobox(
            toolbar, state="readonly", width=5, values=("1x", "2x", "3x", "4x")
        )
        self.scale_combo.set("2x")
        self.scale_combo.grid(row=0, column=9, padx=(0, 10))
        self.scale_combo.bind("<<ComboboxSelected>>", self._render_selected)
        ttk.Button(toolbar, text="현재 PNG 저장", command=self.export_current).grid(
            row=0, column=10, padx=2
        )

        self.summary = ttk.Label(root, padding=(8, 0, 8, 6))
        self.summary.pack(fill=tk.X)
        self.frame_labels = ttk.Label(
            root,
            text=(
                "왼쪽부터: 원본 시작 | 사전 생성 1/12 … 11/12 | 원본 끝"
            ),
            anchor=tk.CENTER,
            padding=(8, 0, 8, 6),
        )
        self.frame_labels.pack(fill=tk.X)

        container = ttk.Frame(root)
        container.pack(fill=tk.BOTH, expand=True)
        self.canvas = tk.Canvas(container, background="#26332d")
        x_scroll = ttk.Scrollbar(container, orient=tk.HORIZONTAL, command=self.canvas.xview)
        y_scroll = ttk.Scrollbar(container, orient=tk.VERTICAL, command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=x_scroll.set, yscrollcommand=y_scroll.set)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        y_scroll.grid(row=0, column=1, sticky="ns")
        x_scroll.grid(row=1, column=0, sticky="ew")
        container.rowconfigure(0, weight=1)
        container.columnconfigure(0, weight=1)

        self.status = ttk.Label(root, relief=tk.SUNKEN, anchor=tk.W, padding=5)
        self.status.pack(fill=tk.X)
        root.bind("<Left>", lambda _event: self.previous())
        root.bind("<Right>", lambda _event: self.next())

        self.unit_combo.current(0)
        self._unit_changed()

    def selected_unit_type(self) -> int:
        return self.unit_value_to_type[self.unit_combo.get()]

    def _unit_changed(self, _event: object | None = None) -> None:
        unit_type = self.selected_unit_type()
        self.group_values = self.catalog.groups(unit_type)
        self.group_combo["values"] = [
            f"그룹 {group:02d}  ({len(self.catalog.by_unit_group[(unit_type, group)]):,})"
            for group in self.group_values
        ]
        self.group_combo.current(0)
        self._group_changed()

    def _group_changed(self, _event: object | None = None) -> None:
        unit_type = self.selected_unit_type()
        group = self.group_values[self.group_combo.current()]
        self.current_transitions = self.catalog.by_unit_group[(unit_type, group)]
        self.transition_combo["values"] = [
            f"{transition.source:04d} → {transition.target:04d}"
            for transition in self.current_transitions
        ]
        self.transition_combo.current(0)
        self._render_selected()

    def _render_selected(self, _event: object | None = None) -> None:
        if not self.current_transitions:
            return
        index = max(0, self.transition_combo.current())
        transition = self.current_transitions[index]
        scale = int(self.scale_combo.get()[0])
        try:
            png, width, height = self.catalog.render(transition, scale)
        except Exception as error:
            messagebox.showerror("렌더링 오류", str(error), parent=self.root)
            return
        self.current_transition = transition
        self.current_png = png
        encoded = base64.b64encode(png).decode("ascii")
        self.photo = tk.PhotoImage(data=encoded, format="png")
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor=tk.NW, image=self.photo)
        self.canvas.configure(scrollregion=(0, 0, width, height))
        self.canvas.xview_moveto(0)
        self.canvas.yview_moveto(0)
        unit_name = self.catalog.unit_names[transition.unit_type]
        self.summary.configure(
            text=(
                f"유닛 {transition.unit_type:03d} {unit_name} / "
                f"그룹 {transition.group:02d} / 프레임 "
                f"{transition.source} → {transition.target} / "
                f"전체 아카이브 #{transition.ordinal + 1:,}"
            )
        )
        self.status.configure(
            text=(
                f"전체 {len(self.catalog.transitions):,} 전이 · "
                f"{len(self.catalog.transitions) * assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT:,} "
                "사전 생성 이미지 | "
                f"현재 그룹 {index + 1:,}/{len(self.current_transitions):,} | "
                f"{width:,}×{height:,} px"
            )
        )

    def previous(self) -> None:
        index = self.transition_combo.current()
        if index > 0:
            self.transition_combo.current(index - 1)
            self._render_selected()

    def next(self) -> None:
        index = self.transition_combo.current()
        if index + 1 < len(self.current_transitions):
            self.transition_combo.current(index + 1)
            self._render_selected()

    def export_current(self) -> None:
        transition = self.current_transition
        if transition is None or not self.current_png:
            return
        name = re.sub(
            r"[^A-Za-z0-9_-]+", "_",
            self.catalog.unit_names[transition.unit_type],
        ).strip("_") or "unit"
        default = (
            f"unit_{transition.unit_type:03d}_{name}_group_{transition.group:02d}_"
            f"{transition.source:04d}_to_{transition.target:04d}.png"
        )
        selected = filedialog.asksaveasfilename(
            parent=self.root,
            title="현재 사전 생성 전이 PNG 저장",
            initialdir=str(self.output.resolve()),
            initialfile=default,
            defaultextension=".png",
            filetypes=(("PNG 이미지", "*.png"),),
        )
        if not selected:
            return
        Path(selected).write_bytes(self.current_png)
        self.status.configure(text=f"저장 완료: {selected}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-trc", type=Path, required=True)
    parser.add_argument("--animation-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    catalog = AnimationCatalog(args.source_trc, args.animation_archive)
    expected_frames = len(catalog.transitions) * assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
    if args.verify_only:
        # Touch the first and last records as well as every unit/group bucket.
        probes = {0, len(catalog.transitions) - 1}
        for transitions in catalog.by_unit_group.values():
            probes.add(transitions[0].ordinal)
            probes.add(transitions[-1].ordinal)
        for ordinal in sorted(probes):
            frames = catalog.generated_frames(catalog.transitions[ordinal])
            if len(frames) != assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT:
                raise ValueError("viewer did not expose all intermediate frames")
        print(
            f"viewer valid units={len(catalog.unit_names)} "
            f"groups={len(catalog.by_unit_group)} "
            f"transitions={len(catalog.transitions)} frames={expected_frames} "
            f"probes={len(probes)}"
        )
        return 0
    root = tk.Tk()
    AnimationViewer(root, catalog, args.output)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
