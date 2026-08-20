#!/usr/bin/env python3
"""Run every catalog-bound special action in both executables.

A comparison is only recorded as exact when the expanded simulation states
match continuously and the action's runtime effect (0x3d + action id) was
actually observed.  Thus a rejected/no-op command cannot masquerade as skill
parity.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from parity_result_store import atomic_json, record_key


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


case_key = record_key


def verify_behavior_trace_gaps(root: Path, replay: Path, artifact: Path,
                               trace: dict[str, Any], command_frame: int,
                               timeout_seconds: int) -> dict[str, Any] | None:
    """Verify compact-sampler gaps after the skill command one frame at a time."""
    behavior_gaps = [gap for gap in trace.get("pair_gaps", [])
                     if gap[1] > command_frame]
    if not trace.get("pass") or not behavior_gaps:
        return None
    frames = sorted({frame
                     for low, high in behavior_gaps
                     for frame in range(max(low + 1, command_frame), high)})
    probe_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "probe_replay.ps1")
    exact_frames = []
    for frame in frames:
        output = artifact / f"gap_one_shot_{frame:03d}"
        completed = subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(probe_script), "-ReplayPath", str(replay),
            "-TargetFrame", str(frame), "-OutputDirectory", str(output),
            "-TimeoutSeconds", str(timeout_seconds),
            "-StabilizeViewport", "-AlignPresentationRng",
        ], cwd=root, capture_output=True, text=True,
            timeout=timeout_seconds + 60)
        result_path = output / "result.json"
        if completed.returncode != 0 or not result_path.exists():
            return {"pass": False, "frames": exact_frames,
                    "reason": (completed.stderr.strip() or
                               completed.stdout.strip() or
                               f"gap frame {frame} probe failed")}
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if not result.get("pass"):
            return {"pass": False, "frames": exact_frames,
                    "reason": result.get("reason", f"frame {frame} diverged")}
        exact_frames.append(frame)
    trace["exact_pair_count"] = (
        trace.get("exact_pair_count", 0) + len(exact_frames))
    return {"pass": True, "frames": exact_frames,
            "reason": "behavior trace gaps verified by one-shot probes"}


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root_default = tool_dir.parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=root_default)
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=160)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument(
        "--case", action="append", default=[], metavar="AxxUxxx",
        help="run only the named action/unit case; may be repeated")
    args = parser.parse_args()

    root = args.root.resolve()
    report_path = tool_dir / "reports" / "gameplay_inventory.json"
    results_path = tool_dir / "parity_results.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    actions = {row["action_id"]: row for row in report["actions"]}
    units = {row["unit_id"]: row for row in report["units"]}
    selected = {item.upper() for item in args.case}
    bindings = [
        binding for binding in report["skill_bindings"]
        if not selected or
        f"A{binding['action_id']:02d}U{binding['unit_id']:03d}" in selected
    ]
    if selected and len(bindings) != len(selected):
        found = {
            f"A{row['action_id']:02d}U{row['unit_id']:03d}"
            for row in bindings
        }
        raise ValueError(f"unknown cases: {sorted(selected - found)}")

    recorded = (json.loads(results_path.read_text(encoding="utf-8"))
                if results_path.exists() else {"schema": 1, "cases": []})
    cases = {case_key(case): case for case in recorded.get("cases", [])}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    run_stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact_root = (root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts" / "runs" /
                     f"skill_parity_{run_stamp}")
    artifact_root.mkdir(parents=True, exist_ok=False)

    failures = 0
    for number, binding in enumerate(bindings, 1):
        unit_id = binding["unit_id"]
        action_id = binding["action_id"]
        label = f"A{action_id:02d}U{unit_id:03d}"
        replay_relative = Path("RankerOCPV_Win") / "Replays" / (
            f"(2) GP Skill A{action_id:02d} U{unit_id:03d}.ply")
        replay = root / replay_relative
        if not replay.exists():
            raise FileNotFoundError(replay)
        artifact = artifact_root / label
        command = [
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script),
            "-ReplayPath", str(replay_relative),
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(args.end_frame),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(args.timeout_seconds),
            "-TraceIntervalMs", str(args.trace_interval_ms),
            "-StabilizeViewport", "-AlignPresentationRng",
        ]
        print(
            f"[{number:02d}/{len(bindings):02d}] {label} "
            f"{units[unit_id]['name']} / {actions[action_id]['name']}",
            flush=True)
        completed = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            timeout=args.timeout_seconds + 60)
        result_file = artifact / "result.json"
        if completed.returncode == 0 and result_file.exists():
            trace = json.loads(result_file.read_text(encoding="utf-8"))
        else:
            trace = {
                "pass": False,
                "reason": (completed.stderr.strip() or
                           completed.stdout.strip() or
                           f"trace command exited {completed.returncode}"),
            }

        gap_fallback = verify_behavior_trace_gaps(
            root, replay_relative, artifact, trace, 31,
            args.timeout_seconds)
        behavior_gaps = [gap for gap in trace.get("pair_gaps", [])
                         if gap[1] > 31]
        behavior_gaps_verified = bool(
            gap_fallback and gap_fallback.get("pass"))

        expected_effect = 0x3D + action_id
        coverage = trace.get("semantic_coverage") or {}
        observed_effects = coverage.get("unit_effect_ids", [])
        source_seen = unit_id in coverage.get("unit_types", [])
        effect_seen = expected_effect in observed_effects
        continuous_exact = bool(
            trace.get("pass") and
            trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and
            trace.get("first_exact_frame") <= 31 and
            trace.get("last_exact_frame") >= args.end_frame - 1 and
            (not behavior_gaps or behavior_gaps_verified))
        exercised = source_seen and effect_seen
        if not continuous_exact:
            verdict = "divergent"
        elif not exercised:
            verdict = "not_exercised"
        else:
            verdict = "exact"

        terminal = trace.get("terminal_exact_state") or {}
        source_terminal = {
            slot: row
            for slot, row in terminal.get("player_units", {}).items()
            if row.get("type") == unit_id and row.get("owner") == 0
        }
        record = {
            "kind": "skill",
            "unit_id": unit_id,
            "unit_name": units[unit_id]["name"],
            "action_id": action_id,
            "action_name": actions[action_id]["name"],
            "result": verdict,
            "replay": replay_relative.as_posix(),
            "replay_sha256": sha256(replay),
            "rebuild_sha256": trace.get("sha256"),
            "trace_artifact": artifact.relative_to(root).as_posix(),
            "frame_range": [args.start_frame, args.end_frame - 1],
            "exact_pair_count": trace.get("exact_pair_count", 0),
            "first_exact_frame": trace.get("first_exact_frame"),
            "last_exact_frame": trace.get("last_exact_frame"),
            "pair_gaps": trace.get("pair_gaps", []),
            "behavior_pair_gaps": behavior_gaps,
            "gap_fallback": gap_fallback,
            "expected_unit_effect_id": expected_effect,
            "observed_unit_effect_ids": observed_effects,
            "source_unit_seen": source_seen,
            "action_effect_seen": effect_seen,
            "terminal_source_units": source_terminal,
            "trace_reason": trace.get("reason"),
            "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        cases[case_key(record)] = record
        recorded["schema"] = 1
        recorded["cases"] = sorted(
            cases.values(), key=lambda row: tuple(str(x) for x in case_key(row)))
        atomic_json(results_path, recorded)

        status = "PASS" if verdict == "exact" else verdict.upper()
        print(
            f"  {status}: exact={record['exact_pair_count']} "
            f"effects={observed_effects} artifact={artifact.name}",
            flush=True)
        failures += verdict != "exact"

    print(
        f"completed={len(bindings)} exact={len(bindings) - failures} "
        f"failed_or_unexercised={failures} artifacts={artifact_root}",
        flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"trace timed out: {error}", file=sys.stderr)
        raise SystemExit(2)
