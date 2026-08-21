"""Persistent replay catalog used by the WizardNet replay browser."""

from __future__ import annotations

from dataclasses import asdict, dataclass, replace
import hashlib
import json
import os
from pathlib import Path
import re
import time
from typing import Any


_UNSAFE_FILENAME = re.compile(r'[\x00-\x1f<>:"/\\|?*]+')
_MAP_FILE_EXTENSIONS = {".map", ".scn", ".trc", ".trk"}


def _truncate_cp949(value: str, maximum_bytes: int) -> str:
    output: list[str] = []
    used = 0
    for character in value:
        encoded = character.encode("cp949", errors="replace")
        if used + len(encoded) > maximum_bytes:
            break
        output.append(character)
        used += len(encoded)
    return "".join(output)


def _sanitize_filename_component(
    value: str, *, fallback: str, maximum_bytes: int
) -> str:
    component = _UNSAFE_FILENAME.sub("_", value).strip(" .")
    if not component:
        component = fallback
    device = component.split(".", 1)[0].upper()
    if device in {"CON", "PRN", "AUX", "NUL"} or (
        len(device) == 4
        and device[:3] in {"COM", "LPT"}
        and device[3] in "123456789"
    ):
        component = "_" + component
    component = _truncate_cp949(component, maximum_bytes).rstrip(" .")
    return component or fallback


def sanitize_replay_filename(value: str) -> str:
    name = Path(value.replace("\\", "/")).name
    stem = name[:-4] if name.lower().endswith(".ply") else name
    stem = _sanitize_filename_component(
        stem, fallback="Replay", maximum_bytes=119
    )
    return stem + ".ply"


def build_replay_filename(
    map_name: str,
    player_names: list[str] | tuple[str, ...],
    timestamp: time.struct_time | None = None,
) -> str:
    map_leaf = Path(map_name.replace("\\", "/")).name
    map_path = Path(map_leaf)
    map_stem = (
        map_path.stem
        if map_path.suffix.casefold() in _MAP_FILE_EXTENSIONS
        else map_leaf
    )
    safe_map = _sanitize_filename_component(
        map_stem, fallback="Map", maximum_bytes=44
    )
    selected = [
        _sanitize_filename_component(name, fallback="Player", maximum_bytes=22)
        for name in player_names
        if name
    ][:2]
    while len(selected) < 2:
        selected.append("Player")
    local = timestamp if timestamp is not None else time.localtime()
    stamp = time.strftime("%Y_%m_%d_%H-%M-%S", local)
    return sanitize_replay_filename(
        f"[{safe_map}]_{selected[0]}vs{selected[1]}_{stamp}.ply"
    )


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
    content_sha256: str = ""
    match_key: str = ""
    winner: str = ""
    loser: str = ""
    duration_seconds: int = 0


@dataclass(frozen=True, slots=True)
class ReplayCommitResult:
    entry: ReplayCatalogEntry
    stored: bool


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
                    content_sha256=str(row.get("content_sha256", "")),
                    match_key=str(row.get("match_key", "")),
                    winner=str(row.get("winner", "")),
                    loser=str(row.get("loser", "")),
                    duration_seconds=max(0, int(row.get("duration_seconds", 0))),
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
            "version": 3,
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

    @staticmethod
    def _content_sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
        return digest.hexdigest()

    def find_duplicate(
        self, path: Path, *, match_key: str = ""
    ) -> tuple[ReplayCatalogEntry | None, str]:
        content_sha256 = self._content_sha256(path)
        byte_count = path.stat().st_size
        for entry in self.entries():
            if match_key and entry.match_key == match_key:
                return entry, content_sha256
        for entry in self.entries():
            if entry.byte_count != byte_count:
                continue
            existing_hash = entry.content_sha256
            if not existing_hash:
                try:
                    existing_hash = self._content_sha256(self.path_for(entry))
                except OSError:
                    continue
            if existing_hash == content_sha256:
                return entry, content_sha256
        return None, content_sha256

    def commit_upload(
        self,
        temporary_path: Path,
        *,
        display_name: str,
        uploader: str,
        byte_count: int,
        game_type: int,
        game_id: int,
        match_key: str = "",
        winner: str = "",
        loser: str = "",
        duration_seconds: int = 0,
    ) -> ReplayCommitResult:
        duplicate, content_sha256 = self.find_duplicate(
            temporary_path, match_key=match_key
        )
        if duplicate is not None:
            temporary_path.unlink(missing_ok=True)
            enriched = replace(
                duplicate,
                winner=duplicate.winner or winner,
                loser=duplicate.loser or loser,
                duration_seconds=(
                    duplicate.duration_seconds
                    if duplicate.duration_seconds > 0
                    else max(0, duration_seconds)
                ),
            )
            if enriched != duplicate:
                self._entries[duplicate.replay_id] = enriched
                self.save()
            return ReplayCommitResult(enriched, False)

        replay_id = self._next_id
        self._next_id += 1
        safe_name = sanitize_replay_filename(display_name)
        stored_name = safe_name
        collision_index = 2
        while (self.directory / stored_name).exists():
            suffix = f"_{collision_index}"
            stem = _truncate_cp949(safe_name[:-4], 119 - len(suffix)).rstrip(" .")
            stored_name = f"{stem}{suffix}.ply"
            collision_index += 1
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
            content_sha256=content_sha256,
            match_key=match_key,
            winner=winner,
            loser=loser,
            duration_seconds=max(0, duration_seconds),
        )
        self._entries[replay_id] = entry
        self.save()
        return ReplayCommitResult(entry, True)
