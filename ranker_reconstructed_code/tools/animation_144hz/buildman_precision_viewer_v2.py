#!/usr/bin/env python3
"""Review BuildMan group-0 precision transitions and a full 360-degree turn."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tkinter as tk
from pathlib import Path
from tkinter import ttk

from PIL import Image, ImageTk


DIRECTORY = Path(__file__).resolve().parent
RUNTIME_PHASES = (0, 2, 4, 6, 7, 9, 11, 12)
# BuildMan group 0 is a 5-direction x 3-action-phase grid.  The original game
# supplies the other side of the unit by horizontally mirroring the three
# oblique views.  This path starts at the front and closes after one 360 turn.
TURN_ROWS = (4, 3, 2, 1, 0, 1, 2, 3, 4)
TURN_FLIPS = (False, False, False, False, False, True, True, True, False)


def load_assets() -> object:
    path = DIRECTORY / "unit_sprite_assets.py"
    spec = importlib.util.spec_from_file_location("unit_sprite_assets", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


assets = load_assets()
Dots = dict[tuple[int, int], int]


def decode_dots(raw_frames: list[list[list[int]]]) -> list[Dots]:
    return [
        {(int(x), int(y)): int(token) for x, y, token in raw_frame}
        for raw_frame in raw_frames
    ]


def mirror_dots(dots: Dots, origin_x: int, delta_y: int) -> Dots:
    """Apply the original sprite renderer's horizontal anchor transform.

    ``origin_x - x`` and ``y + delta_y`` are the exact integer operations used
    by the reconstructed renderer and recovered from the original metadata.
    """
    return {
        (origin_x - x, y + delta_y): token
        for (x, y), token in dots.items()
    }


class Viewer:
    def __init__(
        self, root: tk.Tk, archive: Path, overrides: Path,
        turn_seams: Path | None,
    ) -> None:
        self.root = root
        self.archive = assets.TrcArchive(archive)
        self.unit = assets.decode_unit_record(self.archive, 0)
        flip_transforms = {
            (
                assets.signed_u32(frame.metadata[4]) +
                    frame.width + frame.offset_x,
                assets.signed_u32(frame.metadata[5]) - frame.offset_y,
            )
            for frame in self.unit.groups[0]
        }
        if len(flip_transforms) != 1:
            raise ValueError("BuildMan group 0 uses incompatible flip transforms")
        self.flip_origin_x, self.flip_delta_y = next(iter(flip_transforms))
        document = json.loads(overrides.read_text(encoding="utf-8"))
        self.transitions = document["overrides"]
        self.transition_by_label = {
            f"{int(item['source']):04d} → {int(item['target']):04d}": item
            for item in self.transitions
        }
        self.transition_by_pair = {
            (int(item["source"]), int(item["target"])): item
            for item in self.transitions
        }
        if turn_seams is None:
            turn_seams = overrides.with_name("buildman_turn360_seams_v2.json")
        if not turn_seams.exists():
            raise FileNotFoundError(
                f"360-degree seam keyframes are missing: {turn_seams}"
            )
        turn_document = json.loads(turn_seams.read_text(encoding="utf-8"))
        if (turn_document.get("source_archive_sha256") !=
                document.get("source_archive_sha256")):
            raise ValueError("360-degree seam keyframes use a different TRC")
        self.turn_seam_by_key = {
            (
                int(item["source"]),
                int(item["target"]),
                bool(item["source_flipped"]),
                bool(item["target_flipped"]),
            ): item
            for item in turn_document["overrides"]
        }
        self.view = tk.StringVar(value="turn360")
        self.mode = tk.StringVar(value="runtime_slow")
        self.action_phase = tk.IntVar(value=1)
        self.transition_name = tk.StringVar(value=next(iter(self.transition_by_label)))
        self.frame_index = 0
        self.after_id: str | None = None
        self.original_photo: ImageTk.PhotoImage | None = None
        self.generated_photo: ImageTk.PhotoImage | None = None
        self.original_frames: list[Image.Image] = []
        self.generated_frames: list[Image.Image] = []
        self.frame_descriptions: list[str] = []

        root.title("Build Man 원본 60Hz ↔ 정밀 144Hz — 360° 동시 비교")
        root.geometry("1460x820")
        root.minsize(1120, 650)

        viewbar = ttk.Frame(root, padding=(10, 10, 10, 4))
        viewbar.pack(fill="x")
        ttk.Label(viewbar, text="검수 방식").pack(side="left")
        ttk.Radiobutton(
            viewbar, text="360° 한 바퀴", variable=self.view,
            value="turn360", command=self.change_view,
        ).pack(side="left", padx=(8, 3))
        ttk.Radiobutton(
            viewbar, text="단일 전환", variable=self.view,
            value="transition", command=self.change_view,
        ).pack(side="left", padx=3)
        ttk.Label(viewbar, text="고정 동작 위상").pack(side="left", padx=(24, 4))
        self.action_combo = ttk.Combobox(
            viewbar,
            textvariable=self.action_phase,
            values=(0, 1, 2),
            state="readonly",
            width=4,
        )
        self.action_combo.pack(side="left")
        self.action_combo.bind("<<ComboboxSelected>>", self.change_action_phase)

        toolbar = ttk.Frame(root, padding=(10, 4, 10, 10))
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="전환").pack(side="left")
        self.transition_combo = ttk.Combobox(
            toolbar,
            textvariable=self.transition_name,
            values=list(self.transition_by_label),
            state="readonly",
            width=18,
        )
        self.transition_combo.pack(side="left", padx=(6, 14))
        self.transition_combo.bind("<<ComboboxSelected>>", self.change_transition)
        self.previous_button = ttk.Button(
            toolbar, text="이전", command=lambda: self.move(-1)
        )
        self.previous_button.pack(side="left", padx=2)
        self.next_button = ttk.Button(
            toolbar, text="다음", command=lambda: self.move(1)
        )
        self.next_button.pack(side="left", padx=2)
        ttk.Radiobutton(
            toolbar, text="실제 45ms/144Hz", variable=self.mode,
            value="runtime", command=self.rebuild_view,
        ).pack(side="left", padx=(20, 2))
        ttk.Radiobutton(
            toolbar, text="12배 느리게", variable=self.mode,
            value="runtime_slow", command=self.rebuild_view,
        ).pack(side="left", padx=2)
        ttk.Radiobutton(
            toolbar, text="13위상 정밀검수", variable=self.mode,
            value="all_phases", command=self.rebuild_view,
        ).pack(side="left", padx=2)

        self.summary = ttk.Label(root, padding=(12, 4), anchor="center")
        self.summary.pack(fill="x")
        comparison = ttk.Frame(root, padding=(12, 6))
        comparison.pack(fill="both", expand=True)
        comparison.columnconfigure(0, weight=1, uniform="comparison")
        comparison.columnconfigure(1, weight=1, uniform="comparison")
        comparison.rowconfigure(1, weight=1)
        ttk.Label(
            comparison,
            text="원본 Jw2_09.trc · 60Hz 방향 자세 (중간 프레임 없음)",
            anchor="center",
        ).grid(row=0, column=0, sticky="ew", padx=(0, 6), pady=(0, 5))
        ttk.Label(
            comparison,
            text="생성된 정밀 144Hz · 동일 원본 사이의 중간 프레임",
            anchor="center",
        ).grid(row=0, column=1, sticky="ew", padx=(6, 0), pady=(0, 5))
        self.original_image_label = ttk.Label(
            comparison, anchor="center", relief="groove"
        )
        self.original_image_label.grid(
            row=1, column=0, sticky="nsew", padx=(0, 6)
        )
        self.generated_image_label = ttk.Label(
            comparison, anchor="center", relief="groove"
        )
        self.generated_image_label.grid(
            row=1, column=1, sticky="nsew", padx=(6, 0)
        )
        self.phase_label = ttk.Label(root, padding=10, anchor="center")
        self.phase_label.pack(fill="x")
        self.notice = ttk.Label(
            root,
            text=(
                "두 화면은 같은 시간축으로 정면→우측→후면→좌측→정면 회전 · "
                "왼쪽은 TRC 원본만, 오른쪽은 사전 생성 중간 프레임 포함"
            ),
            padding=8,
            anchor="center",
        )
        self.notice.pack(fill="x")
        self.change_view()

    def current_item(self) -> dict[str, object]:
        return self.transition_by_label[self.transition_name.get()]

    def move(self, delta: int) -> None:
        if self.view.get() != "transition":
            return
        labels = list(self.transition_by_label)
        index = labels.index(self.transition_name.get())
        self.transition_name.set(labels[(index + delta) % len(labels)])
        self.change_transition()

    def change_view(self) -> None:
        single = self.view.get() == "transition"
        state = "readonly" if single else "disabled"
        self.transition_combo.configure(state=state)
        self.previous_button.configure(state="normal" if single else "disabled")
        self.next_button.configure(state="normal" if single else "disabled")
        self.action_combo.configure(state="disabled" if single else "readonly")
        self.rebuild_view()

    def change_action_phase(self, _event: object | None = None) -> None:
        if self.view.get() == "turn360":
            self.rebuild_view()

    def change_transition(self, _event: object | None = None) -> None:
        if self.view.get() == "transition":
            self.rebuild_view()

    def phase_selection(self) -> tuple[int, ...]:
        return tuple(range(13)) if self.mode.get() == "all_phases" else RUNTIME_PHASES

    def transition_dots(self, source: int, target: int) -> list[Dots]:
        item = self.transition_by_pair.get((source, target))
        if item is None:
            raise ValueError(f"precision override is missing {source}->{target}")
        group = self.unit.groups[0]
        return [
            assets.endpoint_dots(group[source]),
            *decode_dots(item["frames"]),
            assets.endpoint_dots(group[target]),
        ]

    def turn_transition_dots(
        self, source: int, target: int,
        source_flipped: bool, target_flipped: bool,
    ) -> list[Dots]:
        if source_flipped != target_flipped:
            key = (source, target, source_flipped, target_flipped)
            item = self.turn_seam_by_key.get(key)
            if item is None:
                raise ValueError(f"360-degree seam override is missing: {key}")
            group = self.unit.groups[0]
            return [
                {
                    (x, y): token
                    for x, y, token in assets.frame_points(
                        group[source], source_flipped
                    )
                },
                *decode_dots(item["frames"]),
                {
                    (x, y): token
                    for x, y, token in assets.frame_points(
                        group[target], target_flipped
                    )
                },
            ]
        dots = self.transition_dots(source, target)
        return [
            mirror_dots(frame, self.flip_origin_x, self.flip_delta_y)
            for frame in dots
        ] if source_flipped else dots

    def original_frame_dots(self, frame_index: int, flipped: bool) -> Dots:
        points = assets.frame_points(self.unit.groups[0][frame_index], flipped)
        return {(x, y): token for x, y, token in points}

    def build_transition_sequence(
        self,
    ) -> tuple[list[Dots], list[Dots], list[str]]:
        item = self.current_item()
        source = int(item["source"])
        target = int(item["target"])
        selected = self.phase_selection()
        dots = self.transition_dots(source, target)
        generated = [dots[phase] for phase in selected]
        original = [
            self.original_frame_dots(source if phase < 12 else target, False)
            for phase in selected
        ]
        labels = [
            f"원본 {source if phase < 12 else target} · "
            f"정밀 전환 {source}→{target} · 전환 위상 {phase}/12"
            for phase in selected
        ]
        source_row, source_phase = divmod(source, 3)
        target_row, target_phase = divmod(target, 3)
        self.summary.configure(text=(
            f"BuildMan / 그룹 0 / {source} → {target}    "
            f"방향 행 {source_row}→{target_row}, 동작 위상 "
            f"{source_phase}→{target_phase}    원본 팔레트 정수 픽셀"
        ))
        return original, generated, labels

    def build_turn_sequence(
        self,
    ) -> tuple[list[Dots], list[Dots], list[str]]:
        phase = self.action_phase.get()
        if phase not in (0, 1, 2):
            phase = 1
            self.action_phase.set(phase)
        selected = self.phase_selection()
        original: list[Dots] = []
        generated: list[Dots] = []
        labels: list[str] = []
        for turn_segment, (source_row, target_row) in enumerate(
            zip(TURN_ROWS, TURN_ROWS[1:])
        ):
            source = source_row * 3 + phase
            target = target_row * 3 + phase
            source_flipped = TURN_FLIPS[turn_segment]
            target_flipped = TURN_FLIPS[turn_segment + 1]
            segment_dots = self.turn_transition_dots(
                source, target, source_flipped, target_flipped
            )
            for ordinal, transition_phase in enumerate(selected):
                if turn_segment and ordinal == 0:
                    continue
                frame = segment_dots[transition_phase]
                generated.append(frame)
                degrees = (turn_segment * 45.0 + transition_phase * 3.75) % 360.0
                original_is_target = transition_phase == 12
                original_index = target if original_is_target else source
                original_flipped = (
                    target_flipped if original_is_target else source_flipped
                )
                original.append(self.original_frame_dots(
                    original_index, original_flipped
                ))
                original_degrees = (
                    (turn_segment + (1 if original_is_target else 0)) * 45
                ) % 360
                if source_flipped != target_flipped:
                    view_kind = "반전 경계 정밀 프레임"
                else:
                    view_kind = "좌우 반전" if target_flipped else "원본"
                labels.append(
                    f"원본 {original_degrees:03d}°/프레임 {original_index} · "
                    f"정밀 144Hz {degrees:06.2f}° · "
                    f"방향 {turn_segment + 1}/8 · 전환 위상 "
                    f"{transition_phase}/12 · {view_kind}"
                )
        self.summary.configure(text=(
            f"BuildMan / 그룹 0 / 360° 한 바퀴    "
            f"동작 위상 {phase} 고정 · 8방향 구간 · "
            f"원본 팔레트 정수 픽셀 · 좌우 반전 포함"
        ))
        return original, generated, labels

    def render_frames(
        self, dots: list[Dots],
        bounds: tuple[int, int, int, int] | None = None,
    ) -> list[Image.Image]:
        points = [
            (x, y, token)
            for frame in dots for (x, y), token in frame.items()
        ]
        if bounds is None:
            left = min(x for x, _y, _token in points)
            top = min(y for _x, y, _token in points)
            right = max(x for x, _y, _token in points) + 1
            bottom = max(y for _x, y, _token in points) + 1
        else:
            left, top, right, bottom = bounds
        margin = 9
        width = right - left + margin * 2
        height = bottom - top + margin * 2
        rendered = []
        for frame in dots:
            canvas = bytearray(width * height * 4)
            assets.fill_checkerboard(canvas, width, height)
            assets.draw_morph_dots(
                canvas, width, height, frame, self.unit.palette,
                margin - left, margin - top,
            )
            scaled_width, scaled_height, rgba = assets.scale_rgba_nearest(
                bytes(canvas), width, height, 7
            )
            rendered.append(Image.frombytes(
                "RGBA", (scaled_width, scaled_height), rgba
            ))
        return rendered

    def rebuild_view(self) -> None:
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.frame_index = 0
        if self.view.get() == "turn360":
            originals, generated, self.frame_descriptions = (
                self.build_turn_sequence()
            )
        else:
            originals, generated, self.frame_descriptions = (
                self.build_transition_sequence()
            )
        combined_points = [
            (x, y, token)
            for frame in (*originals, *generated)
            for (x, y), token in frame.items()
        ]
        bounds = (
            min(x for x, _y, _token in combined_points),
            min(y for _x, y, _token in combined_points),
            max(x for x, _y, _token in combined_points) + 1,
            max(y for _x, y, _token in combined_points) + 1,
        )
        self.original_frames = self.render_frames(originals, bounds)
        self.generated_frames = self.render_frames(generated, bounds)
        self.show_next()

    def show_next(self) -> None:
        if not self.generated_frames:
            return
        self.original_photo = ImageTk.PhotoImage(
            self.original_frames[self.frame_index]
        )
        self.generated_photo = ImageTk.PhotoImage(
            self.generated_frames[self.frame_index]
        )
        self.original_image_label.configure(image=self.original_photo)
        self.generated_image_label.configure(image=self.generated_photo)
        self.phase_label.configure(text=(
            f"{self.frame_descriptions[self.frame_index]} · "
            f"비교 프레임 {self.frame_index + 1}/{len(self.generated_frames)}"
        ))
        self.frame_index = (
            self.frame_index + 1
        ) % len(self.generated_frames)
        if self.mode.get() == "runtime":
            delay = 7
        elif self.mode.get() == "runtime_slow":
            delay = 83
        else:
            delay = 140
        self.after_id = self.root.after(delay, self.show_next)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--overrides", type=Path, required=True)
    parser.add_argument("--turn-seams", type=Path)
    args = parser.parse_args()
    root = tk.Tk()
    Viewer(root, args.archive, args.overrides, args.turn_seams)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
