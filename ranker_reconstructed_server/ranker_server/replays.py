"""Persistent replay catalog used by the WizardNet replay browser."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import re
import time
from typing import Any


_UNSAFE_FILENAME = re.compile(r'[\x00-\x1f<>:"/\\|?*]+')


def sanitize_replay_filename(value: str) -> str:
    name = Path(value.replace("\\", "/")).name
    name = _UNSAFE_FILENAME.sub("_", name).strip(" .")
    if not name:
        name = "Replay.ply"
    stem = name[:-4] if name.lower().endswith(".ply") else name
    stem = stem[:123].rstrip(" .") or "Replay"
    return stem + ".ply"


def fnv1a64(data: bytes, value: int = 0xCBF29CE484222325) -> int:
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


@dataclass(frozen=True, slots=True)
class ReplayCatalogEntry:
    replay_id: int
    stored_name: str
    display_name: str
    uploader: str
    byte_count: int
    game_type: int
    game_id: int
    uploaded_at: int


class ReplayCatalog:
    def __init__(self, directory: Path) -> None:
        self.directory = directory
        self.index_path = directory / "index.json"
        self._entries: dict[int, ReplayCatalogEntry] = {}
        self._next_id = 1
        self.load()

    def load(self) -> None:
        self.directory.mkdir(parents=True, exist_ok=True)
        self._entries.clear()
        self._next_id = 1
        if not self.index_path.exists():
            return
        try:
            root: Any = json.loads(self.index_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return
        rows = root.get("replays", []) if isinstance(root, dict) else []
        if not isinstance(rows, list):
            return
        for row in rows:
            if not isinstance(row, dict):
                continue
            try:
                entry = ReplayCatalogEntry(
                    replay_id=int(row["replay_id"]),
                    stored_name=str(row["stored_name"]),
                    display_name=sanitize_replay_filename(str(row["display_name"])),
                    uploader=str(row["uploader"]),
                    byte_count=int(row["byte_count"]),
                    game_type=int(row["game_type"]),
                    game_id=int(row.get("game_id", 0)),
                    uploaded_at=int(row["uploaded_at"]),
                )
            except (KeyError, TypeError, ValueError):
                continue
            if Path(entry.stored_name).name != entry.stored_name:
                continue
            path = self.directory / entry.stored_name
            if entry.replay_id > 0 and path.is_file() and path.stat().st_size == entry.byte_count:
                self._entries[entry.replay_id] = entry
                self._next_id = max(self._next_id, entry.replay_id + 1)

    def save(self) -> None:
        temporary = self.index_path.with_suffix(".json.tmp")
        payload = {
            "version": 1,
            "next_id": self._next_id,
            "replays": [asdict(entry) for entry in self.entries()],
        }
        temporary.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, self.index_path)

    def entries(self) -> list[ReplayCatalogEntry]:
        return sorted(
            self._entries.values(),
            key=lambda entry: (-entry.uploaded_at, -entry.replay_id),
        )

    def get(self, replay_id: int) -> ReplayCatalogEntry | None:
        return self._entries.get(replay_id)

    def path_for(self, entry: ReplayCatalogEntry) -> Path:
        return self.directory / entry.stored_name

    def commit_upload(
        self,
        temporary_path: Path,
        *,
        display_name: str,
        uploader: str,
        byte_count: int,
        game_type: int,
        game_id: int,
    ) -> ReplayCatalogEntry:
        replay_id = self._next_id
        self._next_id += 1
        safe_name = sanitize_replay_filename(display_name)
        stored_name = f"{replay_id:08d}_{safe_name}"
        destination = self.directory / stored_name
        os.replace(temporary_path, destination)
        entry = ReplayCatalogEntry(
            replay_id=replay_id,
            stored_name=stored_name,
            display_name=safe_name,
            uploader=uploader,
            byte_count=byte_count,
            game_type=game_type,
            game_id=game_id,
            uploaded_at=int(time.time()),
        )
        self._entries[replay_id] = entry
        self.save()
        return entry
