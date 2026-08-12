#!/usr/bin/env python3
"""Interactive A/B/C selector for the generated BuildMan candidate pack."""

from __future__ import annotations

import argparse
import json
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

from PIL import Image, ImageTk


class Viewer:
    def __init__(self, root: tk.Tk, manifest_path: Path, selection_path: Path) -> None:
        self.root = root
        self.manifest_path = manifest_path
        self.selection_path = selection_path
        self.document = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.transitions = self.document["transitions"]
        self.by_group: dict[int, list[dict[str, object]]] = {}
        for transition in self.transitions:
            self.by_group.setdefault(int(transition["group"]), []).append(transition)
        self.selections: dict[str, str] = {}
        if selection_path.exists():
            saved = json.loads(selection_path.read_text(encoding="utf-8"))
            self.selections = dict(saved.get("selections", {}))
        self.group_var = tk.StringVar(value=str(min(self.by_group)))
        self.transition_var = tk.StringVar()
        self.choice_var = tk.StringVar(value="")
        self.view_var = tk.StringVar(value="animation")
        self.frame_index = 0
        self.after_id: str | None = None
        self.photos: list[ImageTk.PhotoImage] = []

        root.title("BuildMan 144Hz A/B/C 후보 선택 (폐기된 6위상 방식)")
        root.geometry("1680x980")
        toolbar = ttk.Frame(root, padding=8)
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="그룹").pack(side="left")
        self.group_box = ttk.Combobox(
            toolbar,
            textvariable=self.group_var,
            values=[str(value) for value in sorted(self.by_group)],
            width=5,
            state="readonly",
        )
        self.group_box.pack(side="left", padx=(4, 12))
        self.group_box.bind("<<ComboboxSelected>>", self.change_group)
        ttk.Label(toolbar, text="전환").pack(side="left")
        self.transition_box = ttk.Combobox(
            toolbar, textvariable=self.transition_var, width=18, state="readonly"
        )
        self.transition_box.pack(side="left", padx=4)
        self.transition_box.bind("<<ComboboxSelected>>", self.change_transition)
        ttk.Button(toolbar, text="이전", command=lambda: self.move(-1)).pack(side="left", padx=3)
        ttk.Button(toolbar, text="다음", command=lambda: self.move(1)).pack(side="left", padx=3)
        ttk.Radiobutton(
            toolbar, text="느린 반복 재생", variable=self.view_var,
            value="animation", command=self.refresh
        ).pack(side="left", padx=(18, 3))
        ttk.Radiobutton(
            toolbar, text="7프레임 전체", variable=self.view_var,
            value="strip", command=self.refresh
        ).pack(side="left", padx=3)
        ttk.Button(toolbar, text="선택 저장", command=self.save).pack(side="right")

        self.status = ttk.Label(root, padding=(10, 4))
        self.status.pack(fill="x")
        self.rows = []
        for code in "ABC":
            frame = ttk.LabelFrame(root, text=f"후보 {code}", padding=5)
            frame.pack(fill="both", expand=True, padx=8, pady=3)
            radio = ttk.Radiobutton(
                frame, text="이 후보 선택", variable=self.choice_var,
                value=code, command=self.record_choice
            )
            radio.pack(side="left", padx=8)
            image_label = ttk.Label(frame, anchor="center")
            image_label.pack(side="left", fill="both", expand=True)
            self.rows.append((frame, image_label))
        self.change_group()

    def group_transitions(self) -> list[dict[str, object]]:
        return self.by_group[int(self.group_var.get())]

    def current(self) -> dict[str, object]:
        return self.group_transitions()[self.transition_box.current()]

    def change_group(self, _event: object | None = None) -> None:
        values = [
            f"{int(t['source']):04d} -> {int(t['target']):04d}"
            for t in self.group_transitions()
        ]
        self.transition_box.configure(values=values)
        self.transition_box.current(0)
        self.change_transition()

    def change_transition(self, _event: object | None = None) -> None:
        self.frame_index = 0
        current = self.current()
        self.choice_var.set(self.selections.get(str(current["key"]), ""))
        self.refresh()

    def move(self, delta: int) -> None:
        count = len(self.group_transitions())
        self.transition_box.current((self.transition_box.current() + delta) % count)
        self.change_transition()

    def record_choice(self) -> None:
        self.selections[str(self.current()["key"])] = self.choice_var.get()
        self.update_status()

    def update_status(self) -> None:
        current = self.current()
        selected = len(self.selections)
        self.status.configure(text=(
            f"[구 방식—선택 금지] BuildMan / 그룹 {int(current['group']):02d} / "
            f"{int(current['source']):04d} -> {int(current['target']):04d}    "
            f"선택 완료 {selected}/{len(self.transitions)}"
        ))

    def refresh(self) -> None:
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None
        self.photos.clear()
        current = self.current()
        for (frame, image_label), candidate in zip(self.rows, current["candidates"]):
            frame.configure(text=(
                f"후보 {candidate['code']} — {candidate['label']} "
                f"[{candidate['mode']}]"
            ))
            image = Image.open(candidate["image"])
            if self.view_var.get() == "animation":
                cell_width = int(candidate["cell_width"])
                image = image.crop((
                    self.frame_index * cell_width, 0,
                    (self.frame_index + 1) * cell_width, int(candidate["cell_height"])
                ))
            photo = ImageTk.PhotoImage(image)
            self.photos.append(photo)
            image_label.configure(image=photo)
        self.update_status()
        if self.view_var.get() == "animation":
            self.frame_index = (self.frame_index + 1) % 7
            self.after_id = self.root.after(140, self.refresh)

    def save(self) -> None:
        document = {
            "schema": 1,
            "unit_type": 0,
            "unit_name": "BuildMan",
            "source_manifest": str(self.manifest_path.resolve()),
            "selected_transition_count": len(self.selections),
            "total_transition_count": len(self.transitions),
            "selections": dict(sorted(self.selections.items())),
        }
        self.selection_path.parent.mkdir(parents=True, exist_ok=True)
        self.selection_path.write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        messagebox.showinfo("저장 완료", f"선택을 저장했습니다.\n{self.selection_path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    args = parser.parse_args()
    root = tk.Tk()
    Viewer(root, args.manifest, args.selection)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
