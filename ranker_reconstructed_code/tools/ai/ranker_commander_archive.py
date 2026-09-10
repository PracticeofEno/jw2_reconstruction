"""Lossless storage management for completed non-Tyrano development evaluations.

Only losing/truncated RLO files in fully completed evaluations are compressed.
Winning examples, active cohorts, replay commands, weights and receipts stay intact.
An archive is read back and SHA-256 verified before its raw file is removed.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
import uuid

from ranker_commander_watch import COMMANDER, campaign_directories


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def checked_path(path, root):
    path = Path(path).resolve()
    if root.resolve() not in path.parents:
        raise ValueError(f"archive path escapes the commander workspace: {path}")
    return path


def retain_marker(source):
    return source.with_name(source.name + ".retain")


def candidates(root):
    seen = set()
    evaluations = []
    for work in campaign_directories(root):
        for pattern in ("*/update_*/evaluation/evaluation.json", "*/baseline*/evaluation.json",
                        "*/warmstart/evaluation*/evaluation.json"):
            evaluations.extend(work.glob(pattern))
    for evaluation in sorted(evaluations, key=lambda p:p.stat().st_mtime_ns):
        result = read_json(evaluation)
        if not result.get("gate", {}).get("coverage_complete"):
            continue
        for row in result["reports"]:
            if (not row.get("valid") or not row.get("evaluation_valid") or row.get("teacher")
                    or row.get("own_tribe") not in (0, 1, 3) or row.get("status") not in (2, 3)):
                continue
            source = checked_path(row["rollout"], root)
            if (source in seen or source.name != "commander.rlo" or not source.is_file()
                    or retain_marker(source).exists() or evaluation.parent.resolve() not in source.parents):
                continue
            seen.add(source)
            yield source


def stream_hash(stream):
    digest = hashlib.sha256()
    for block in iter(lambda:stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def archive_file(source, root):
    source = checked_path(source, root)
    if retain_marker(source).exists():
        return 0
    archive = source.with_name(source.name + ".gz")
    metadata = archive.with_name(archive.name + ".json")
    if archive.exists() or metadata.exists():
        raise FileExistsError(f"archive already exists; inspect it before retrying: {archive}")
    before = source.stat()
    temporary = archive.with_name(archive.name + ".partial_" + uuid.uuid4().hex)
    digest = hashlib.sha256()
    try:
        with source.open("rb") as reader, gzip.open(temporary, "wb", compresslevel=1) as writer:
            for block in iter(lambda:reader.read(1024 * 1024), b""):
                digest.update(block)
                writer.write(block)
        with gzip.open(temporary, "rb") as reader:
            if stream_hash(reader) != digest.hexdigest():
                raise ValueError("compressed rollout did not reproduce the source hash")
        after = source.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise ValueError("rollout changed during compression")
        temporary.replace(archive)
        write_json(metadata, dict(source=str(source), source_sha256=digest.hexdigest(),
            source_size=before.st_size, source_mtime_ns=before.st_mtime_ns,
            archive_size=archive.stat().st_size, verified=True,
            created=datetime.now().astimezone().isoformat()))
        # Individual file only, after root validation and a verified lossless copy.
        checked_path(source, root).unlink()
        return before.st_size - archive.stat().st_size
    finally:
        temporary.unlink(missing_ok=True)


def restore_file(archive, root):
    archive = checked_path(archive, root)
    if archive.suffix != ".gz":
        raise ValueError("restore expects a .rlo.gz archive")
    metadata = read_json(archive.with_name(archive.name + ".json"))
    source = checked_path(metadata["source"], root)
    if source.name != "commander.rlo" or archive != source.with_name(source.name + ".gz"):
        raise ValueError("archive identity does not match its source")
    if source.exists():
        raise FileExistsError(source)
    retain_marker(source).write_text("Keep the restored diagnostic rollout uncompressed.\n", encoding="utf-8")
    temporary = source.with_name(source.name + ".restore_" + uuid.uuid4().hex)
    try:
        with gzip.open(archive, "rb") as reader, temporary.open("wb") as writer:
            shutil.copyfileobj(reader, writer, 1024 * 1024)
        with temporary.open("rb") as reader:
            if stream_hash(reader) != metadata["source_sha256"] or temporary.stat().st_size != metadata["source_size"]:
                raise ValueError("restored rollout hash/size differs")
        temporary.replace(source)
        os.utime(source, ns=(metadata["source_mtime_ns"], metadata["source_mtime_ns"]))
    finally:
        temporary.unlink(missing_ok=True)
    return source


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=COMMANDER)
    parser.add_argument("--watch", action="store_true")
    parser.add_argument("--target-free-gb", type=float, default=16)
    parser.add_argument("--restore", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if args.target_free_gb <= 0:
        parser.error("target-free-gb must be positive")
    if args.restore:
        print(restore_file(args.restore, root), flush=True)
        return
    # A second archive observer must not race the existing one.
    import msvcrt
    with (root / "archive.lock").open("a+b") as lock:
        if lock.tell() == 0:
            lock.write(b"0"); lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        status = dict(pid=os.getpid(), files=0, saved_bytes=0, state="running")
        status_path = root / "archive_status.json"
        try:
            while True:
                if shutil.disk_usage(root).free < args.target_free_gb * 1024**3:
                    for source in candidates(root):
                        if shutil.disk_usage(root).free >= args.target_free_gb * 1024**3:
                            break
                        status["saved_bytes"] += archive_file(source, root)
                        status["files"] += 1
                        status.update(last_source=str(source), free_gb=shutil.disk_usage(root).free/1024**3,
                                      time=datetime.now().astimezone().isoformat())
                        if status["files"] % 20 == 0:
                            write_json(status_path, status)
                            print(json.dumps(status), flush=True)
                status.update(free_gb=shutil.disk_usage(root).free/1024**3,
                              time=datetime.now().astimezone().isoformat())
                write_json(status_path, status)
                if status["free_gb"] < 2:
                    stopped = []
                    for work in campaign_directories(root):
                        state = read_json(work / "state.json")
                        if state.get("pid") and state.get("status") not in ("error", "stopped", "cycle_limit"):
                            (work / "STOP").write_text("Storage observer: less than 2 GB remains after archiving.\n", encoding="utf-8")
                            stopped.append(str(work))
                    status["stop_requested"] = stopped
                    write_json(status_path, status)
                if not args.watch:
                    break
                time.sleep(30)
        except BaseException as error:
            status.update(state="error", error=f"{type(error).__name__}: {error}")
            raise
        finally:
            status["pid"] = None
            if status["state"] == "running":
                status["state"] = "finished"
            write_json(status_path, status)


if __name__ == "__main__":
    main()
