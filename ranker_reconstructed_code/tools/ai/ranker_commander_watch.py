"""Open the most recently completed commander match using its saved commands.

The normal Replay menu preserves game speed and assigns recorded policy slots
to packet playback. The original builtin opponent still simulates its own AI.
No policy inference or legacy AI autopilot runs on the recorded policy side.
Use --dry-run to inspect the selection without starting a game.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from ranker_commander_strategy import validate_strategy

ROOT = Path(__file__).resolve().parents[3]
COMMANDER = ROOT / "debug_artifacts/commander"
VIEWER = ROOT / "build/replay_controller_fix_20260908/ranker_rebuild.exe"
# This viewer has the per-owner replay controller fix. A changed binary must
# be explicitly selected, rather than silently substituting the training exe.
VIEWER_SHA256 = "92e9d4542bd9e850c810993aa080e42369e2e1974f2185505762c45398e9df2c"
NAMES = {0: "Primitive", 1: "Elf", 2: "Tyrano", 3: "Demon"}


@dataclass
class Match:
    receipt: Path
    replay: Path
    finished_ns: int
    report: dict

    def describe(self):
        row = self.report
        result = dict(replay=str(self.replay), receipt=str(self.receipt),
                    finished_local=datetime.fromtimestamp(self.finished_ns / 1e9).isoformat(timespec="seconds"),
                    race=NAMES[row["own_tribe"]], opponent=NAMES[row["tribe"]],
                    seed=row["seed"], status=row["status"], end_frame=row["end_frame"],
                    policy_version=row.get("weight_version"),
                    policy_sha256=row.get("weights_sha256"))
        if row.get('elf_strategy_profile'):
            strategy=row['elf_strategy_profile']['definition']
            result.update(policy_kind=strategy['kind'],policy_version=None,
                policy_sha256=strategy['profile_sha256'],strategy_parameters=strategy['parameters'])
        if row.get('elf_strategy_router'):
            router=row['elf_strategy_router']
            result.update(policy_kind=router['definition']['kind'],
                policy_sha256=router['manifest_sha256'],
                strategy_profile_sha256=row['elf_strategy_profile']['definition']['profile_sha256'])
        return result


def campaign_directories(base=COMMANDER):
    # Only registered campaigns; isolated teacher probes and replay verification
    # jobs must not displace the user's last actual learned-policy game.
    states = set(base.glob("*/training_run/state.json")) | set(base.glob("*/state.json"))
    return [p.parent for p in sorted(states)]


def receipts(work):
    for update in work.glob("*/update_*"):
        yield from update.glob("games*/game_*/output/job.json")
        yield from update.glob("evaluation/*/game_*/output/job.json")
        yield from update.glob("teacher/game_*/output/job.json")
    # Recovery compares the initial candidate and the starting policy before PPO.
    yield from work.glob("*/baseline*/*/game_*/output/job.json")
    yield from work.glob("*/warmstart/evaluation*/*/game_*/output/job.json")


def latest_match(directories):
    candidates = []
    for work in directories:
        for receipt in receipts(work):
            result = receipt.with_name("ai_selfplay_result.json")
            replay = receipt.with_name("ai_selfplay_replay.ply")
            try:
                if replay.stat().st_size and result.is_file():
                    candidates.append((result.stat().st_mtime_ns, receipt, replay))
            except FileNotFoundError:
                continue  # A running game is not a completed replay.
    for finished, receipt, replay in sorted(candidates, reverse=True):
        try:
            row = json.loads(receipt.read_text(encoding="utf-8"))
            result = json.loads(receipt.with_name("ai_selfplay_result.json").read_text(encoding="utf-8"))
            strategy=False
            if row.get('elf_strategy_profile') and row.get('strategy_profile_verified') is True:
                definition=validate_strategy(row['elf_strategy_profile'],
                    executable_sha=hashlib.sha256(Path(row['command'][0]).read_bytes()).hexdigest())
                strategy=definition['purpose'] in ('search','learned_evaluation')
            if row.get('elf_strategy_router'):
                from ranker_commander_strategy_router import validate_router, select_strategy
                validate_router(row['elf_strategy_router'])
                if (row.get('own_tribe') != 1 or row.get('teacher') is not True
                        or row.get('strategy_profile_verified') is not True
                        or select_strategy(row['elf_strategy_router'],row['tribe']) != row.get('elf_strategy_profile')):
                    continue
                # A learned router can deliberately retain the zero profile
                # against an opponent. Its overall controller still ran.
                strategy=True
            if (not row.get("valid") or (row.get("teacher") and not strategy) or row.get("status") not in (1, 2, 3)
                    or row.get("own_tribe") not in NAMES or row.get("tribe") not in NAMES
                    or row.get("end_frame") != result.get("end_frame")
                    or row.get("result") != result):
                continue
            return Match(receipt.resolve(), replay.resolve(), finished, row)
        except (OSError, ValueError, KeyError, TypeError):
            continue  # An unfinished/invalid receipt never hides an older valid one.
    raise RuntimeError("No completed learned-policy match with a saved .ply was found.")


def verify_viewer(executable, expected_sha256):
    if executable.name.lower() != "ranker_rebuild.exe" or not executable.is_file():
        raise RuntimeError(f"Replay executable is missing: {executable}")
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    if digest != expected_sha256:
        raise RuntimeError("Replay executable hash changed; check the build before selecting it.")


def prepare_runtime(match, runtime, install):
    runtime.mkdir(parents=True, exist_ok=False)
    for source in install.iterdir():
        destination = runtime / source.name
        if source.is_file() and source.suffix.lower() in {".trc", ".dll", ".asi", ".chm"}:
            try:
                destination.symlink_to(source)
            except OSError:
                try:
                    os.link(source, destination)
                except OSError:
                    shutil.copy2(source, destination)
        elif source.is_file() and (source.suffix.lower() == ".ini" or source.name.lower() == "_setup.dat"):
            shutil.copy2(source, destination)
        elif source.is_dir() and source.name.lower() in {"maps", "media"}:
            try:
                destination.symlink_to(source, target_is_directory=True)
            except OSError:
                target, origin = (str(p).replace("'", "''") for p in (destination, source))
                subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                                f"New-Item -ItemType Junction -Path '{target}' -Target '{origin}' -ErrorAction Stop | Out-Null"],
                               check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    replays = runtime / "Replays"
    replays.mkdir()
    description = match.describe()
    replay = replays / f"Last_{description['race']}_vs_{description['opponent']}_{description['seed']}.ply"
    shutil.copy2(match.replay, replay)
    for source, target in ((match.replay.with_suffix(".vpo"), replay.with_suffix(".vpo")),
                           (match.replay.parent / "commander.rlo.decisions.jsonl", replay.with_suffix(".decisions.jsonl"))):
        if source.exists():
            shutil.copy2(source, target)
    (runtime / "selected_match.json").write_text(json.dumps(description, indent=2) + "\n", encoding="utf-8")
    return replay


class ReplayUI:
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes, self.types = ctypes, wintypes
        self.ui = ui = ctypes.WinDLL("user32", use_last_error=True)
        self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        ui.EnumWindows.argtypes = [self.callback, wintypes.LPARAM]
        ui.EnumChildWindows.argtypes = [wintypes.HWND, self.callback, wintypes.LPARAM]
        ui.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        ui.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        ui.GetDlgCtrlID.argtypes = [wintypes.HWND]
        ui.SetForegroundWindow.argtypes = [wintypes.HWND]
        ui.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
                                          wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
        ui.SendMessageTimeoutW.restype = wintypes.LPARAM

    def find(self, pid, title=None, parent=None, control=None):
        found = []

        @self.callback
        def visit(handle, _):
            owner = self.types.DWORD()
            self.ui.GetWindowThreadProcessId(handle, self.ctypes.byref(owner))
            if owner.value != pid or (control is not None and self.ui.GetDlgCtrlID(handle) != control):
                return True
            if title is not None:
                text = self.ctypes.create_unicode_buffer(256)
                self.ui.GetWindowTextW(handle, text, len(text))
                if text.value != title:
                    return True
            found.append(handle)
            return False

        if parent:
            self.ui.EnumChildWindows(parent, visit, 0)
        else:
            self.ui.EnumWindows(visit, 0)
        return found[0] if found else None

    def send(self, handle, message, wparam=0, lparam=0):
        result = self.ctypes.c_size_t()
        if not self.ui.SendMessageTimeoutW(handle, message, wparam, lparam, 2, 4000, self.ctypes.byref(result)):
            raise RuntimeError("The replay window did not respond.")
        return self.ctypes.c_ssize_t(result.value).value

    def open_replay(self, process, replay, runtime):
        def wait(predicate, description, seconds=45):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError("Game closed before playback. Close any other visible game and try again.")
                result = predicate()
                if result:
                    return result
                time.sleep(0.2)
            raise RuntimeError(f"Timed out waiting for {description}. See {runtime / 'viewer.log'}")

        main = wait(lambda: self.find(process.pid, "The Ranker"), "game window")
        self.ui.SetForegroundWindow(main)
        time.sleep(6)
        self.send(main, 0x0102, ord("S"))
        time.sleep(2)
        self.send(main, 0x0102, ord("R"))
        replay_dialog = lambda: self.find(process.pid, "Replay") or self.find(process.pid, "Replay", main)
        dialog = wait(replay_dialog, "Replay menu")
        listing = self.find(process.pid, parent=dialog, control=0x70A)
        okay = self.find(process.pid, parent=dialog, control=0x70D)
        if not listing or not okay:
            raise RuntimeError("Replay menu controls were not found.")
        for index in range(self.send(listing, 0x018B)):
            length = self.send(listing, 0x018A, index)
            if length < 0:
                continue
            text = self.ctypes.create_unicode_buffer(length + 1)
            self.send(listing, 0x0189, index, self.ctypes.addressof(text))
            if text.value == replay.name:
                self.send(listing, 0x0186, index)
                self.send(dialog, 0x0111, (1 << 16) | 0x70A, listing)
                time.sleep(0.3)
                self.send(okay, 0x00F5)
                wait(lambda: not replay_dialog(), "playback start", seconds=25)
                self.ui.SetForegroundWindow(main)
                return
        raise RuntimeError(f"Replay was not listed: {replay.name}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, action="append", help="Limit selection to this campaign; repeatable")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--exe", type=Path, default=VIEWER)
    parser.add_argument("--exe-sha256", default=VIEWER_SHA256)
    args = parser.parse_args(argv)
    match = latest_match(args.work_dir or campaign_directories())
    print(json.dumps(match.describe(), indent=2), flush=True)
    if args.dry_run:
        return
    verify_viewer(args.exe, args.exe_sha256)
    runtime = ROOT / "debug_artifacts/replay_viewer" / datetime.now().strftime("latest_%Y%m%d_%H%M%S_%f")
    replay = prepare_runtime(match, runtime, ROOT / "RankerOCPV_Win")
    environment = os.environ.copy()
    environment.update(RANKER_RECONSTRUCTED_LOG_PATH=str(runtime / "viewer.log"),
                       RANKER_RECONSTRUCTED_REPLAY_DIR=str(runtime / "Replays"))
    environment.pop("RANKER_REBUILD_BACKGROUND_TEST", None)
    process = subprocess.Popen([str(args.exe.resolve())], cwd=runtime, env=environment,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    (runtime / "viewer_process.json").write_text(json.dumps(dict(pid=process.pid, executable=str(args.exe),
        executable_sha256=args.exe_sha256, replay=str(replay)), indent=2) + "\n", encoding="utf-8")
    ReplayUI().open_replay(process, replay, runtime)
    print(f"Replay opened: {replay.name}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
