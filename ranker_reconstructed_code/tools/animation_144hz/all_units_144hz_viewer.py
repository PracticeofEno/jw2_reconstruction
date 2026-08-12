#!/usr/bin/env python3
"""Interactive per-unit preview for the pre-generated 144 Hz animation bank."""

from __future__ import annotations

import argparse
import math
import re
import sys
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import messagebox, ttk

from PIL import Image, ImageDraw, ImageFont, ImageTk

import unit_sprite_assets as assets
from animation_archive_viewer import AnimationCatalog, Transition


SOURCE_RATE = 60
TARGET_RATE = 144
COMMON_PHASE_STEP = 5
CHECKER_A = (48, 64, 56, 255)
CHECKER_B = (58, 76, 66, 255)


@dataclass(frozen=True)
class PreviewSequence:
    group: int
    frames: tuple[int, ...]
    looped: bool
    source: str


@dataclass(frozen=True)
class FrameSample:
    frame: assets.SpriteFrame
    label: str
    source: int
    target: int
    phase: int


@dataclass(frozen=True)
class GroupTrackSample:
    sequence: PreviewSequence
    sample: FrameSample
    mirrored: bool
    direction_index: int
    direction_count: int
    family_index: int
    family_count: int


def safe_name(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_-]+", "_", value).strip("_")
    return result or "unit"


def load_label_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        Path("C:/Windows/Fonts/malgun.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


class UnitAnimationPreviewCatalog:
    def __init__(self, source: Path, animation: Path) -> None:
        self.archive = AnimationCatalog(source, animation)
        self.units = [
            self.archive.unit(type_id)
            for type_id in range(assets.UNIT_RECORD_COUNT)
        ]
        self.unit_types = [unit.type_id for unit in self.units if unit.image_count]
        self.transition_by_key: dict[tuple[int, int, int, int], Transition] = {
            (
                transition.unit_type,
                transition.group,
                transition.source,
                transition.target,
            ): transition
            for transition in self.archive.transitions
        }
        self._sequence_cache: dict[int, list[PreviewSequence]] = {}

    def unit(self, type_id: int) -> assets.UnitRecord:
        return self.units[type_id]

    def unit_name(self, type_id: int) -> str:
        unit = self.unit(type_id)
        return unit.unit_name or unit.record_name or f"Unit {type_id}"

    def sequences(self, type_id: int) -> list[PreviewSequence]:
        cached = self._sequence_cache.get(type_id)
        if cached is not None:
            return cached
        unit = self.unit(type_id)
        result: list[PreviewSequence] = []
        seen: set[tuple[int, tuple[int, ...], bool]] = set()
        for group, frames, looped, source in assets.animation_temporal_sequences(unit):
            key = group, frames, looped
            if key in seen:
                continue
            seen.add(key)
            result.append(PreviewSequence(group, frames, looped, source))
        covered_groups = {sequence.group for sequence in result}
        for group, frames in enumerate(unit.groups):
            if frames and group not in covered_groups:
                result.append(PreviewSequence(group, (0,), False, "static_original"))
        result.sort(key=lambda sequence: (
            sequence.group,
            sequence.source == "single_row_progress_strip",
            sequence.frames[0],
            sequence.source,
        ))
        self._sequence_cache[type_id] = result
        return result

    def groups(self, type_id: int) -> list[int]:
        return sorted({sequence.group for sequence in self.sequences(type_id)})

    def sequences_for_group(
        self, type_id: int, group: int
    ) -> list[PreviewSequence]:
        return [
            sequence for sequence in self.sequences(type_id)
            if sequence.group == group
        ]

    def transition_frames(
        self, type_id: int, group: int, source: int, target: int
    ) -> list[assets.SpriteFrame] | None:
        transition = self.transition_by_key.get((type_id, group, source, target))
        if transition is None:
            return None
        return self.archive.generated_frames(transition)

    def frame_for_phase(
        self,
        type_id: int,
        sequence: PreviewSequence,
        interval: int,
        phase: int,
    ) -> FrameSample:
        frames = sequence.frames
        source = frames[interval]
        if interval + 1 < len(frames):
            target = frames[interval + 1]
        elif sequence.looped:
            target = frames[0]
        else:
            target = source
        group_frames = self.unit(type_id).groups[sequence.group]
        if phase <= 0 or source == target:
            frame = group_frames[source]
        elif phase >= assets.VISUAL_ARCHIVE_INTERVAL_COUNT:
            frame = group_frames[target]
        else:
            generated = self.transition_frames(
                type_id, sequence.group, source, target
            )
            frame = (
                generated[phase - 1]
                if generated is not None
                else group_frames[target]
            )
        return FrameSample(
            frame,
            f"{source:04d}→{target:04d}  위상 {phase:02d}/12",
            source,
            target,
            phase,
        )

    def timeline(
        self, type_id: int, sequence: PreviewSequence, mode: str
    ) -> list[FrameSample]:
        if len(sequence.frames) == 1:
            frame_index = sequence.frames[0]
            frame = self.unit(type_id).groups[sequence.group][frame_index]
            return [FrameSample(
                frame, f"정적 원본 프레임 {frame_index:04d}",
                frame_index, frame_index, 0,
            )]
        interval_count = len(sequence.frames) if sequence.looped else len(sequence.frames) - 1
        if mode == "all_phases":
            result = [
                self.frame_for_phase(type_id, sequence, interval, phase)
                for interval in range(interval_count)
                for phase in range(assets.VISUAL_ARCHIVE_INTERVAL_COUNT)
            ]
            if not sequence.looped:
                result.append(self.frame_for_phase(
                    type_id,
                    sequence,
                    interval_count - 1,
                    assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
                ))
            return result

        # One presentation frame advances five ticks on the exact 720 Hz
        # common clock. This is the 60 -> 144 polyphase sequence used by the
        # game, reduced to an integer number of preview frames per cycle.
        output_count = max(1, (
            interval_count * assets.VISUAL_ARCHIVE_INTERVAL_COUNT + 2
        ) // COMMON_PHASE_STEP)
        result = []
        for presentation_frame in range(output_count):
            common_tick = presentation_frame * COMMON_PHASE_STEP
            interval = min(
                common_tick // assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
                interval_count - 1,
            )
            phase = common_tick % assets.VISUAL_ARCHIVE_INTERVAL_COUNT
            result.append(self.frame_for_phase(
                type_id, sequence, interval, phase
            ))
        if not sequence.looped:
            result.append(self.frame_for_phase(
                type_id,
                sequence,
                interval_count - 1,
                assets.VISUAL_ARCHIVE_INTERVAL_COUNT,
            ))
        return result

    def bounds(
        self, type_id: int, sequence: PreviewSequence
    ) -> tuple[int, int, int, int]:
        unit = self.unit(type_id)
        frames = [unit.groups[sequence.group][index] for index in sequence.frames]
        left, top, right, bottom = assets.frame_bounds(frames)
        for index, source in enumerate(sequence.frames):
            if index + 1 < len(sequence.frames):
                target = sequence.frames[index + 1]
            elif sequence.looped:
                target = sequence.frames[0]
            else:
                continue
            transition = self.transition_by_key.get(
                (type_id, sequence.group, source, target)
            )
            if transition is not None:
                left = min(left, transition.left)
                top = min(top, transition.top)
                right = max(right, transition.left + transition.width)
                bottom = max(bottom, transition.top + transition.height)
        return left, top, right, bottom

    def render_sample(
        self,
        type_id: int,
        sequence: PreviewSequence,
        sample: FrameSample,
        scale: int,
        mirrored: bool,
        margin: int = 8,
    ) -> Image.Image:
        left, top, right, bottom = self.bounds(type_id, sequence)
        width = max(1, right - left) + margin * 2
        height = max(1, bottom - top) + margin * 2
        rgba = bytearray(width * height * 4)
        assets.fill_checkerboard(rgba, width, height)
        assets.draw_frame_preview(
            rgba,
            width,
            height,
            sample.frame,
            self.unit(type_id).palette,
            margin - left,
            margin - top,
        )
        image = Image.frombytes("RGBA", (width, height), bytes(rgba))
        if mirrored:
            image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if scale != 1:
            image = image.resize(
                (width * scale, height * scale),
                Image.Resampling.NEAREST,
            )
        return image

    def original_60_sample(
        self,
        type_id: int,
        sequence: PreviewSequence,
        sample: FrameSample,
    ) -> FrameSample:
        """Return the authored 60 Hz frame visible at the sample's exact time."""
        frame_index = (
            sample.target
            if sample.phase >= assets.VISUAL_ARCHIVE_INTERVAL_COUNT
            else sample.source
        )
        frame = self.unit(type_id).groups[sequence.group][frame_index]
        return FrameSample(
            frame,
            f"60Hz 원본 프레임 {frame_index:04d}",
            frame_index,
            frame_index,
            0,
        )

    def render_comparison(
        self,
        type_id: int,
        sequence: PreviewSequence,
        sample: FrameSample,
        scale: int,
        mirrored: bool,
        margin: int = 8,
    ) -> Image.Image:
        """Render time-aligned authored 60 Hz and generated 144 Hz frames."""
        original_sample = self.original_60_sample(type_id, sequence, sample)
        original = self.render_sample(
            type_id, sequence, original_sample, scale, mirrored, margin
        )
        generated = self.render_sample(
            type_id, sequence, sample, scale, mirrored, margin
        )
        gap = 8
        header_height = 24
        panel = Image.new(
            "RGBA",
            (
                original.width + gap + generated.width,
                header_height + max(original.height, generated.height),
            ),
            (31, 35, 33, 255),
        )
        panel.alpha_composite(original, (0, header_height))
        generated_x = original.width + gap
        panel.alpha_composite(generated, (generated_x, header_height))
        draw = ImageDraw.Draw(panel)
        font = load_label_font(12)
        labels = (
            (0, original.width, "60Hz 원본", (164, 216, 255, 255)),
            (generated_x, generated.width, "144Hz 보간", (255, 222, 128, 255)),
        )
        for x, width, label, color in labels:
            text_box = draw.textbbox((0, 0), label, font=font)
            text_width = text_box[2] - text_box[0]
            draw.text(
                (x + max(3, (width - text_width) // 2), 3),
                label,
                fill=color,
                font=font,
            )
        divider_x = original.width + gap // 2
        draw.line(
            (divider_x, 2, divider_x, panel.height - 2),
            fill=(105, 115, 110, 255),
            width=1,
        )
        return panel

    def preferred_sequence(self, type_id: int) -> PreviewSequence:
        sequences = self.sequences(type_id)
        return min(sequences, key=lambda sequence: (
            sequence.group != 1,
            not sequence.looped,
            len(sequence.frames) < 2,
            sequence.group,
        ))

    def preferred_sequence_for_group(
        self, type_id: int, group: int
    ) -> PreviewSequence:
        sequences = self.sequences_for_group(type_id, group)
        if not sequences:
            raise ValueError(f"unit {type_id} group {group} has no preview sequence")
        return min(sequences, key=lambda sequence: (
            not sequence.looped,
            len(sequence.frames) < 2,
            sequence.source == "single_row_progress_strip",
            -len(sequence.frames),
            sequence.source,
        ))

    def group_360_track(
        self, type_id: int, group: int, mode: str
    ) -> list[GroupTrackSample]:
        """Join authored and runtime-mirrored rows in compass order.

        Five stored rows cover one 180-degree half turn. The original runtime
        supplies the other three compass views by mirroring the internal rows,
        producing 5 + 3 = 8 directions without interpolating between views.
        """
        directional: dict[str, list[tuple[int, PreviewSequence]]] = {}
        nondirectional: list[PreviewSequence] = []
        for sequence in self.sequences_for_group(type_id, group):
            match = re.fullmatch(
                r"(table_\d+)_row_(\d+)(_segment_\d+)",
                sequence.source,
            )
            if match is None:
                nondirectional.append(sequence)
                continue
            family = match.group(1) + match.group(3)
            directional.setdefault(family, []).append(
                (int(match.group(2)), sequence)
            )

        families: list[list[tuple[PreviewSequence, bool]]] = []
        for family in sorted(directional):
            rows = [
                sequence
                for _row, sequence in sorted(directional[family])
            ]
            views = [(sequence, False) for sequence in rows]
            if len(rows) >= 3:
                views.extend(
                    (sequence, True) for sequence in reversed(rows[1:-1])
                )
            families.append(views)
        families.extend([[(sequence, False)] for sequence in nondirectional])

        track: list[GroupTrackSample] = []
        family_count = len(families)
        for family_index, views in enumerate(families):
            direction_count = len(views)
            for direction_index, (sequence, mirrored) in enumerate(views):
                for sample in self.timeline(type_id, sequence, mode):
                    track.append(GroupTrackSample(
                        sequence,
                        sample,
                        mirrored,
                        direction_index,
                        direction_count,
                        family_index,
                        family_count,
                    ))
        return track


def sequence_description(sequence: PreviewSequence, ordinal: int) -> str:
    row = re.search(r"row_(\d+)", sequence.source)
    table = re.search(r"table_(\d+)", sequence.source)
    pieces = [f"시퀀스 {ordinal + 1:02d}"]
    if row:
        pieces.append(f"방향 행 {int(row.group(1))}")
    if table:
        pieces.append(f"테이블 {int(table.group(1))}")
    if sequence.source == "single_row_progress_strip":
        pieces.append("진행/일회성")
    elif sequence.source == "static_original":
        pieces.append("정적 원본")
    pieces.append(f"원본 {len(sequence.frames)}장")
    pieces.append("순환" if sequence.looped else "비순환")
    return " · ".join(pieces)


class AllUnitsAnimationViewer:
    def __init__(
        self,
        root: tk.Tk,
        catalog: UnitAnimationPreviewCatalog,
        output: Path,
    ) -> None:
        self.root = root
        self.catalog = catalog
        self.output = output
        self.output.mkdir(parents=True, exist_ok=True)
        self.unit_value_to_type: dict[str, int] = {}
        self.group_values: list[int] = []
        self.sequence_values: list[PreviewSequence] = []
        self.timeline: list[FrameSample] = []
        self.bounds_sequence: PreviewSequence | None = None
        self.all_group_tracks: dict[int, list[GroupTrackSample]] = {}
        self.all_group_frame_count = 1
        self.frame_index = 0
        self.playing = True
        self.after_id: str | None = None
        self.photo: ImageTk.PhotoImage | None = None
        self.current_display_image: Image.Image | None = None

        self.view_mode = tk.StringVar(value="all_groups")
        self.mode = tk.StringVar(value="sampled_144")
        self.mirrored = tk.BooleanVar(value=False)
        self.loop_playback = tk.BooleanVar(value=True)

        root.title("Ranker 전체 유닛 60Hz ↔ 144Hz 애니메이션 비교")
        root.geometry("1280x900")
        root.minsize(900, 650)

        toolbar = ttk.Frame(root, padding=10)
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="유닛").grid(row=0, column=0, padx=(0, 5))
        self.unit_combo = ttk.Combobox(toolbar, state="readonly", width=34)
        unit_values = []
        for type_id in catalog.unit_types:
            value = (
                f"{type_id:03d}  {catalog.unit_name(type_id)}  "
                f"({catalog.unit(type_id).image_count:,} 원본)"
            )
            unit_values.append(value)
            self.unit_value_to_type[value] = type_id
        self.unit_combo["values"] = unit_values
        self.unit_combo.grid(row=0, column=1, padx=(0, 12))
        self.unit_combo.bind("<<ComboboxSelected>>", self.unit_changed)

        ttk.Label(toolbar, text="그룹").grid(row=0, column=2, padx=(0, 5))
        self.group_combo = ttk.Combobox(toolbar, state="readonly", width=12)
        self.group_combo.grid(row=0, column=3, padx=(0, 12))
        self.group_combo.bind("<<ComboboxSelected>>", self.group_changed)

        ttk.Label(toolbar, text="방향/시퀀스").grid(row=0, column=4, padx=(0, 5))
        self.sequence_combo = ttk.Combobox(toolbar, state="readonly", width=43)
        self.sequence_combo.grid(row=0, column=5, sticky="ew")
        self.sequence_combo.bind("<<ComboboxSelected>>", self.sequence_changed)
        toolbar.columnconfigure(5, weight=1)

        controls = ttk.Frame(root, padding=(10, 0, 10, 8))
        controls.pack(fill="x")
        self.play_button = ttk.Button(controls, text="일시정지", command=self.toggle_play)
        self.play_button.pack(side="left", padx=(0, 4))
        ttk.Button(controls, text="◀ 한 프레임", command=lambda: self.move(-1)).pack(
            side="left", padx=2
        )
        ttk.Button(controls, text="한 프레임 ▶", command=lambda: self.move(1)).pack(
            side="left", padx=2
        )
        ttk.Button(controls, text="처음", command=self.first_frame).pack(
            side="left", padx=2
        )
        ttk.Checkbutton(
            controls, text="반복", variable=self.loop_playback
        ).pack(side="left", padx=(12, 2))
        ttk.Checkbutton(
            controls,
            text="좌우 반전 방향",
            variable=self.mirrored,
            command=self.render_current,
        ).pack(side="left", padx=4)

        ttk.Label(controls, text="재생 속도").pack(side="left", padx=(18, 5))
        self.speed_combo = ttk.Combobox(
            controls,
            state="readonly",
            width=16,
            values=("144 FPS (실시간)", "60 FPS", "30 FPS", "15 FPS"),
        )
        self.speed_combo.set("30 FPS")
        self.speed_combo.pack(side="left")
        ttk.Label(controls, text="확대").pack(side="left", padx=(18, 5))
        self.scale_combo = ttk.Combobox(
            controls, state="readonly", width=5,
            values=("2x", "4x", "6x", "8x", "10x"),
        )
        self.scale_combo.set("6x")
        self.scale_combo.pack(side="left")
        self.scale_combo.bind("<<ComboboxSelected>>", self.render_current)
        self.export_button = ttk.Button(
            controls,
            text="현재 시퀀스 접촉 시트 저장",
            command=self.export_contact_sheet,
        )
        self.export_button.pack(side="right")

        modes = ttk.Frame(root, padding=(10, 0, 10, 8))
        modes.pack(fill="x")
        ttk.Radiobutton(
            modes,
            text="전체 그룹 + 360° + 60/144Hz 동시 비교",
            variable=self.view_mode,
            value="all_groups",
            command=self.change_view_mode,
        ).pack(side="left")
        ttk.Radiobutton(
            modes,
            text="선택 그룹 정밀검사",
            variable=self.view_mode,
            value="single_group",
            command=self.change_view_mode,
        ).pack(side="left", padx=(12, 24))
        ttk.Radiobutton(
            modes,
            text="60→144Hz 실제 샘플(2.4배)",
            variable=self.mode,
            value="sampled_144",
            command=self.rebuild_timeline,
        ).pack(side="left")
        ttk.Radiobutton(
            modes,
            text="모든 12개 중간 위상 정밀검사",
            variable=self.mode,
            value="all_phases",
            command=self.rebuild_timeline,
        ).pack(side="left", padx=(18, 0))
        self.summary = ttk.Label(modes, anchor="e")
        self.summary.pack(side="right", fill="x", expand=True)

        image_frame = ttk.Frame(root, padding=(10, 0, 10, 6))
        image_frame.pack(fill="both", expand=True)
        self.image_label = ttk.Label(image_frame, anchor="center", relief="groove")
        self.image_label.pack(fill="both", expand=True)

        slider_frame = ttk.Frame(root, padding=(10, 4))
        slider_frame.pack(fill="x")
        self.position = tk.DoubleVar(value=0)
        self.slider = ttk.Scale(
            slider_frame,
            from_=0,
            to=0,
            variable=self.position,
            command=self.slider_changed,
        )
        self.slider.pack(fill="x", expand=True, side="left")
        self.frame_label = ttk.Label(slider_frame, width=42, anchor="e")
        self.frame_label.pack(side="right", padx=(10, 0))

        self.notice = ttk.Label(
            root,
            text=(
                "왼쪽은 같은 시각의 원본 60Hz 프레임, 오른쪽은 생성된 144Hz 프레임입니다. "
                "방향 행끼리는 보간하지 않으며, 144 FPS 타이머 정밀도는 화면/OS에 따라 달라질 수 있습니다."
            ),
            padding=(10, 4),
            anchor="center",
        )
        self.notice.pack(fill="x")
        self.status = ttk.Label(root, relief="sunken", padding=5, anchor="w")
        self.status.pack(fill="x")

        root.bind("<space>", lambda _event: self.toggle_play())
        root.bind("<Left>", lambda _event: self.move(-1))
        root.bind("<Right>", lambda _event: self.move(1))
        root.protocol("WM_DELETE_WINDOW", self.close)

        self.unit_combo.current(0)
        self.unit_changed()
        self.change_view_mode()
        self.schedule_next()

    def selected_type(self) -> int:
        return self.unit_value_to_type[self.unit_combo.get()]

    def selected_sequence(self) -> PreviewSequence:
        return self.sequence_values[max(0, self.sequence_combo.current())]

    def unit_changed(self, _event: object | None = None) -> None:
        type_id = self.selected_type()
        self.group_values = self.catalog.groups(type_id)
        self.group_combo["values"] = [
            f"그룹 {group:02d}" for group in self.group_values
        ]
        preferred = self.catalog.preferred_sequence(type_id)
        self.group_combo.current(self.group_values.index(preferred.group))
        self.group_changed(preferred)

    def group_changed(
        self, preferred: PreviewSequence | object | None = None
    ) -> None:
        type_id = self.selected_type()
        group = self.group_values[max(0, self.group_combo.current())]
        self.sequence_values = self.catalog.sequences_for_group(type_id, group)
        self.sequence_combo["values"] = [
            sequence_description(sequence, index)
            for index, sequence in enumerate(self.sequence_values)
        ]
        index = 0
        if isinstance(preferred, PreviewSequence) and preferred in self.sequence_values:
            index = self.sequence_values.index(preferred)
        self.sequence_combo.current(index)
        self.sequence_changed()

    def sequence_changed(self, _event: object | None = None) -> None:
        self.rebuild_timeline()

    def change_view_mode(self) -> None:
        single = self.view_mode.get() == "single_group"
        self.group_combo.configure(state="readonly" if single else "disabled")
        self.sequence_combo.configure(state="readonly" if single else "disabled")
        self.export_button.configure(
            text=(
                "현재 시퀀스 접촉 시트 저장"
                if single else "현재 전체 그룹 화면 저장"
            )
        )
        self.rebuild_timeline()

    def build_all_group_tracks(self, type_id: int) -> None:
        tracks: dict[int, list[GroupTrackSample]] = {}
        for group in self.catalog.groups(type_id):
            track = self.catalog.group_360_track(
                type_id, group, self.mode.get()
            )
            if track:
                tracks[group] = track
        if not tracks:
            raise ValueError(f"unit {type_id} has no group animation tracks")
        self.all_group_tracks = tracks
        self.all_group_frame_count = max(len(track) for track in tracks.values())

    def rebuild_timeline(self) -> None:
        type_id = self.selected_type()
        try:
            if self.view_mode.get() == "all_groups":
                self.build_all_group_tracks(type_id)
                self.timeline = []
                self.bounds_sequence = None
                frame_count = self.all_group_frame_count
                sequence_count = sum(
                    len(self.catalog.sequences_for_group(type_id, group))
                    for group in self.all_group_tracks
                )
                self.summary.configure(text=(
                    f"전체 {len(self.all_group_tracks)}개 그룹 · "
                    f"방향/동작 시퀀스 {sequence_count}개 · "
                    "360° · 60/144Hz 동시 비교"
                ))
            else:
                sequence = self.selected_sequence()
                self.timeline = self.catalog.timeline(
                    type_id, sequence, self.mode.get()
                )
                self.bounds_sequence = sequence
                frame_count = len(self.timeline)
                self.summary.configure(text=(
                    f"원본 {len(sequence.frames)}장 → 미리보기 {len(self.timeline)}장  "
                    f"| {'순환' if sequence.looped else '비순환'}"
                ))
        except Exception as error:
            messagebox.showerror("애니메이션 로드 오류", str(error), parent=self.root)
            return
        self.frame_index = 0
        self.position.set(0)
        self.slider.configure(to=max(0, frame_count - 1))
        self.render_current()

    def render_current(self, _event: object | None = None) -> None:
        if self.view_mode.get() == "all_groups":
            self.render_all_groups()
            return
        if not self.timeline or self.bounds_sequence is None:
            return
        self.frame_index %= len(self.timeline)
        sample = self.timeline[self.frame_index]
        scale = int(self.scale_combo.get()[:-1])
        image = self.catalog.render_comparison(
            self.selected_type(),
            self.bounds_sequence,
            sample,
            scale,
            self.mirrored.get(),
        )
        self.current_display_image = image
        self.photo = ImageTk.PhotoImage(image)
        self.image_label.configure(image=self.photo)
        self.position.set(self.frame_index)
        original_sample = self.catalog.original_60_sample(
            self.selected_type(), self.bounds_sequence, sample
        )
        self.frame_label.configure(
            text=(
                f"{self.frame_index + 1:,}/{len(self.timeline):,} · "
                f"60Hz {original_sample.source:04d} | "
                f"144Hz {sample.source:04d}→{sample.target:04d} "
                f"p{sample.phase:02d}/12"
            )
        )
        type_id = self.selected_type()
        self.status.configure(text=(
            f"유닛 {type_id:03d} {self.catalog.unit_name(type_id)} | "
            f"그룹 {self.bounds_sequence.group:02d} | "
            "왼쪽 60Hz 원본 | 오른쪽 144Hz 보간 | 공통 시각 동기 비교"
        ))

    def render_all_groups(self) -> None:
        if not self.all_group_tracks:
            return
        type_id = self.selected_type()
        self.frame_index %= self.all_group_frame_count
        groups = sorted(self.all_group_tracks)
        if len(groups) <= 1:
            columns = 1
        elif len(groups) <= 4:
            columns = 2
        elif len(groups) <= 9:
            columns = 3
        else:
            columns = 4
        rows = math.ceil(len(groups) / columns)
        cell_width = 292
        cell_height = 190
        sheet = Image.new(
            "RGBA",
            (columns * cell_width, rows * cell_height),
            (42, 46, 44, 255),
        )
        draw = ImageDraw.Draw(sheet)
        title_font = load_label_font(15)
        detail_font = load_label_font(11)
        for tile, group in enumerate(groups):
            track = self.all_group_tracks[group]
            track_sample = track[self.frame_index % len(track)]
            sequence = track_sample.sequence
            sample = track_sample.sample
            image = self.catalog.render_comparison(
                type_id,
                sequence,
                sample,
                3,
                track_sample.mirrored ^ self.mirrored.get(),
                margin=4,
            )
            max_width = cell_width - 12
            max_height = cell_height - 62
            if image.width > max_width or image.height > max_height:
                ratio = min(max_width / image.width, max_height / image.height)
                image = image.resize(
                    (
                        max(1, int(image.width * ratio)),
                        max(1, int(image.height * ratio)),
                    ),
                    Image.Resampling.NEAREST,
                )
            column = tile % columns
            row = tile // columns
            x0 = column * cell_width
            y0 = row * cell_height
            draw.rectangle(
                (x0 + 2, y0 + 2, x0 + cell_width - 3, y0 + cell_height - 3),
                outline=(100, 118, 108, 255),
                width=2,
            )
            image_x = x0 + (cell_width - image.width) // 2
            image_y = y0 + 34 + (max_height - image.height) // 2
            sheet.alpha_composite(image, (image_x, image_y))
            row_match = re.search(r"row_(\d+)", sequence.source)
            sequence_index = self.catalog.sequences_for_group(
                type_id, group
            ).index(sequence)
            direction_degrees = round(
                track_sample.direction_index * 360 /
                max(1, track_sample.direction_count)
            )
            direction_text = (
                f"방향 {direction_degrees:03d}° "
                f"({track_sample.direction_index + 1}/"
                f"{track_sample.direction_count}) · "
                f"행 {int(row_match.group(1))}"
                if row_match else
                ("진행/일회성" if sequence.source == "single_row_progress_strip"
                 else "정적/직접 시퀀스")
            )
            if track_sample.mirrored:
                direction_text += " · 반전"
            draw.text(
                (x0 + 8, y0 + 7),
                f"그룹 {group:02d} · 시퀀스 {sequence_index + 1:02d} · {direction_text}",
                fill=(250, 250, 250, 255),
                font=title_font,
            )
            draw.text(
                (x0 + 8, y0 + cell_height - 23),
                f"60Hz "
                f"{self.catalog.original_60_sample(type_id, sequence, sample).source:04d} | "
                f"144Hz {sample.source:04d}→{sample.target:04d} "
                f"p{sample.phase:02d}/12 "
                f"· 동작 {track_sample.family_index + 1}/"
                f"{track_sample.family_count} · 트랙 "
                f"{self.frame_index % len(track) + 1}/{len(track)}",
                fill=(212, 224, 217, 255),
                font=detail_font,
            )
        self.current_display_image = sheet
        self.photo = ImageTk.PhotoImage(sheet)
        self.image_label.configure(image=self.photo)
        self.position.set(self.frame_index)
        self.frame_label.configure(text=(
            f"전체 그룹 공통 시간 {self.frame_index + 1:,}/"
            f"{self.all_group_frame_count:,}"
        ))
        self.status.configure(text=(
            f"유닛 {type_id:03d} {self.catalog.unit_name(type_id)} | "
            f"{len(groups)}개 그룹의 60Hz/144Hz를 한 화면에서 동시 재생 | "
            "각 그룹은 원본 방향과 런타임 좌우 반전 방향을 이어 360° 순환"
        ))

    def active_frame_count(self) -> int:
        return (
            self.all_group_frame_count
            if self.view_mode.get() == "all_groups"
            else len(self.timeline)
        )

    def slider_changed(self, value: str) -> None:
        frame_count = self.active_frame_count()
        if frame_count <= 0:
            return
        index = max(0, min(int(round(float(value))), frame_count - 1))
        if index != self.frame_index:
            self.frame_index = index
            self.render_current()

    def delay_ms(self) -> int:
        return {
            "144 FPS (실시간)": 7,
            "60 FPS": 17,
            "30 FPS": 33,
            "15 FPS": 67,
        }[self.speed_combo.get()]

    def schedule_next(self) -> None:
        self.after_id = self.root.after(self.delay_ms(), self.tick)

    def tick(self) -> None:
        frame_count = self.active_frame_count()
        if self.playing and frame_count > 1:
            next_index = self.frame_index + 1
            if next_index >= frame_count:
                if self.loop_playback.get():
                    next_index = 0
                else:
                    next_index = frame_count - 1
                    self.playing = False
                    self.play_button.configure(text="재생")
            self.frame_index = next_index
            self.render_current()
        self.schedule_next()

    def toggle_play(self) -> None:
        self.playing = not self.playing
        self.play_button.configure(text="일시정지" if self.playing else "재생")

    def move(self, delta: int) -> None:
        frame_count = self.active_frame_count()
        if frame_count <= 0:
            return
        self.playing = False
        self.play_button.configure(text="재생")
        self.frame_index = (self.frame_index + delta) % frame_count
        self.render_current()

    def first_frame(self) -> None:
        self.playing = False
        self.play_button.configure(text="재생")
        self.frame_index = 0
        self.render_current()

    def export_contact_sheet(self) -> None:
        if self.view_mode.get() == "all_groups":
            if self.current_display_image is None:
                return
            type_id = self.selected_type()
            unit_name = safe_name(self.catalog.unit_name(type_id))
            path = self.output / (
                f"unit_{type_id:03d}_{unit_name}_all_groups_"
                f"frame_{self.frame_index:04d}.png"
            )
            self.current_display_image.save(path)
            self.status.configure(text=f"전체 그룹 화면 저장 완료: {path}")
            return
        if not self.timeline or self.bounds_sequence is None:
            return
        type_id = self.selected_type()
        scale = 3
        rendered = [
            self.catalog.render_comparison(
                type_id,
                self.bounds_sequence,
                sample,
                scale,
                self.mirrored.get(),
                margin=4,
            )
            for sample in self.timeline
        ]
        columns = min(6, len(rendered))
        rows = math.ceil(len(rendered) / columns)
        cell_width = max(image.width for image in rendered)
        cell_height = max(image.height for image in rendered) + 22
        sheet = Image.new(
            "RGBA", (columns * cell_width, rows * cell_height), (128, 128, 128, 255)
        )
        draw = ImageDraw.Draw(sheet)
        font = load_label_font(12)
        for index, (image, sample) in enumerate(zip(rendered, self.timeline)):
            x = (index % columns) * cell_width + (cell_width - image.width) // 2
            y = (index // columns) * cell_height
            sheet.alpha_composite(image, (x, y))
            draw.text(
                ((index % columns) * cell_width + 3, y + cell_height - 20),
                f"{index:03d} {sample.source}->{sample.target} p{sample.phase}",
                fill=(245, 245, 245, 255),
                font=font,
            )
        unit_name = safe_name(self.catalog.unit_name(type_id))
        path = self.output / (
            f"unit_{type_id:03d}_{unit_name}_group_{self.bounds_sequence.group:02d}_"
            f"sequence_{self.sequence_combo.current():02d}.png"
        )
        sheet.save(path)
        self.status.configure(text=f"접촉 시트 저장 완료: {path}")

    def close(self) -> None:
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
        self.root.destroy()


def thumbnail_pose(
    catalog: UnitAnimationPreviewCatalog,
    type_id: int,
    sequence: PreviewSequence,
) -> Image.Image:
    timeline = catalog.timeline(type_id, sequence, "sampled_144")
    samples = [
        timeline[0],
        timeline[len(timeline) // 2],
        timeline[-1],
    ]
    rendered = [
        catalog.render_sample(type_id, sequence, sample, 3, False, margin=4)
        for sample in samples
    ]
    panel_width = 112
    panel_height = 104
    result = Image.new("RGBA", (panel_width * 3, panel_height + 34), (72, 72, 72, 255))
    for index, image in enumerate(rendered):
        if image.width > panel_width - 8 or image.height > panel_height - 8:
            ratio = min(
                (panel_width - 8) / image.width,
                (panel_height - 8) / image.height,
            )
            image = image.resize(
                (max(1, int(image.width * ratio)), max(1, int(image.height * ratio))),
                Image.Resampling.NEAREST,
            )
        x = index * panel_width + (panel_width - image.width) // 2
        y = (panel_height - image.height) // 2
        result.alpha_composite(image, (x, y))
    draw = ImageDraw.Draw(result)
    font = load_label_font(15)
    draw.text(
        (6, panel_height + 7),
        f"{type_id:03d}  {catalog.unit_name(type_id)}  G{sequence.group:02d}",
        fill=(255, 255, 255, 255),
        font=font,
    )
    return result


def build_thumbnail_index(
    catalog: UnitAnimationPreviewCatalog, output: Path
) -> tuple[Path, int]:
    thumbnails = output / "thumbnails"
    thumbnails.mkdir(parents=True, exist_ok=True)
    entries: list[tuple[int, Image.Image]] = []
    for ordinal, type_id in enumerate(catalog.unit_types, 1):
        sequence = catalog.preferred_sequence(type_id)
        image = thumbnail_pose(catalog, type_id, sequence)
        name = safe_name(catalog.unit_name(type_id))
        image.save(thumbnails / f"unit_{type_id:03d}_{name}.png")
        entries.append((type_id, image))
        if ordinal % 25 == 0:
            print(
                f"thumbnail {ordinal}/{len(catalog.unit_types)}",
                flush=True,
            )

    columns = 4
    rows = math.ceil(len(entries) / columns)
    cell_width = 336
    cell_height = 138
    index_image = Image.new(
        "RGBA",
        (columns * cell_width, rows * cell_height),
        (128, 128, 128, 255),
    )
    for index, (_type_id, image) in enumerate(entries):
        index_image.alpha_composite(
            image,
            ((index % columns) * cell_width, (index // columns) * cell_height),
        )
    index_path = output / "all_units_144hz_index.png"
    index_image.save(index_path)
    return index_path, len(entries)


def verify_catalog(catalog: UnitAnimationPreviewCatalog) -> None:
    sequence_count = 0
    rendered_units = 0
    rendered_groups = 0
    groups_with_runtime_mirrors = 0
    for type_id in catalog.unit_types:
        sequences = catalog.sequences(type_id)
        if not sequences:
            raise ValueError(f"unit {type_id} has images but no preview sequence")
        sequence_count += len(sequences)
        for group in catalog.groups(type_id):
            group_sequences = catalog.sequences_for_group(type_id, group)
            if not group_sequences:
                raise ValueError(
                    f"unit {type_id} group {group} has no preview sequence"
                )
            sequence = catalog.preferred_sequence_for_group(type_id, group)
            timeline = catalog.timeline(type_id, sequence, "sampled_144")
            if not timeline:
                raise ValueError(
                    f"unit {type_id} group {group} produced an empty timeline"
                )
            probes = {0, len(timeline) // 2, len(timeline) - 1}
            for probe in probes:
                image = catalog.render_sample(
                    type_id, sequence, timeline[probe], 1, False
                )
                if image.width <= 0 or image.height <= 0:
                    raise ValueError(
                        f"unit {type_id} group {group} rendered an empty preview"
                    )
            comparison = catalog.render_comparison(
                type_id, sequence, timeline[len(timeline) // 2], 1, False
            )
            if comparison.width <= image.width or comparison.height <= 0:
                raise ValueError(
                    f"unit {type_id} group {group} rendered an invalid comparison"
                )
            full_track = catalog.group_360_track(type_id, group, "sampled_144")
            if not full_track:
                raise ValueError(
                    f"unit {type_id} group {group} has no 360-degree track"
                )
            if any(sample.mirrored for sample in full_track):
                groups_with_runtime_mirrors += 1
            rendered_groups += 1
        rendered_units += 1
    print(
        f"viewer valid units={rendered_units} groups={rendered_groups} "
        f"sequences={sequence_count} "
        f"mirrored_groups={groups_with_runtime_mirrors} "
        f"transitions={len(catalog.archive.transitions)}",
        flush=True,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-trc", type=Path, required=True)
    parser.add_argument("--animation-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--build-index", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    catalog = UnitAnimationPreviewCatalog(
        args.source_trc.resolve(), args.animation_archive.resolve()
    )
    if args.verify_only:
        verify_catalog(catalog)
    if args.build_index:
        index, count = build_thumbnail_index(catalog, args.output.resolve())
        print(f"thumbnail index complete units={count} path={index}", flush=True)
    if args.verify_only:
        return 0
    root = tk.Tk()
    AllUnitsAnimationViewer(root, catalog, args.output.resolve() / "contact_sheets")
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
