#!/usr/bin/env python3
"""Shared one-shot verification for frames skipped by compact replay traces."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any


def verify_missing_exact_frames(
        root: Path, replay: str | Path, artifact: Path,
        trace: dict[str, Any], start_frame: int, end_frame: int,
        timeout_seconds: int, *, stabilize_viewport: bool = False,
        align_presentation_rng: bool = False) -> dict[str, Any] | None:
    """Prove sampler-skipped frames individually and extend the exact count."""
    first = trace.get("first_exact_frame")
    last = trace.get("last_exact_frame")
    if not trace.get("pass") or first is None or last is None:
        return None

    missing = set(range(start_frame, min(first, end_frame)))
    missing.update(range(max(last + 1, start_frame), end_frame))
    for low, high in trace.get("pair_gaps", []):
        missing.update(range(max(low + 1, start_frame), min(high, end_frame)))
    frames = sorted(missing)
    if not frames:
        return None

    probe_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "probe_replay.ps1")
    exact_frames: list[int] = []
    for frame in frames:
        output = artifact / f"gap_one_shot_{frame:03d}"
        command = [
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(probe_script), "-ReplayPath", str(replay),
            "-TargetFrame", str(frame), "-OutputDirectory", str(output),
            "-TimeoutSeconds", str(timeout_seconds),
        ]
        if stabilize_viewport:
            command.append("-StabilizeViewport")
        if align_presentation_rng:
            command.append("-AlignPresentationRng")
        completed = subprocess.run(command, cwd=root, capture_output=True, text=True,
            timeout=timeout_seconds + 60)
        result_path = output / "result.json"
        if completed.returncode != 0 or not result_path.exists():
            return {
                "pass": False,
                "frames": exact_frames,
                "reason": (completed.stderr.strip() or
                           completed.stdout.strip() or
                           f"gap frame {frame} probe failed"),
            }
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if not result.get("pass"):
            return {
                "pass": False,
                "frames": exact_frames,
                "reason": result.get("reason", f"frame {frame} diverged"),
            }
        exact_frames.append(frame)

    trace["exact_pair_count"] = (
        trace.get("exact_pair_count", 0) + len(exact_frames))
    return {
        "pass": True,
        "frames": exact_frames,
        "reason": "compact-trace gaps verified by one-shot probes",
    }
