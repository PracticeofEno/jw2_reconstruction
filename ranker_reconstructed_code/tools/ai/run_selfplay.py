#!/usr/bin/env python3
"""Automated self-play harness driver for ranker_rebuild.exe.

Runs one or more unattended ``-AISELF`` matches, each of which auto-starts a
local game (host + Computer(AI) + built-in Computers), plays until a natural
end condition or the ``-MAXFRAMES`` cap, writes ``ai_selfplay_result.json``,
and exits on its own.  This driver launches those matches, collects the result
JSON plus the per-owner activity trace from ``Jw2.log``, and prints a summary so
the Computer(AI) owner can be compared against the built-in Computer owners.

Example::

    python tools/ai/run_selfplay.py \
        --install-dir "C:/Users/eno/Desktop/jw_resversing/RankerOCPV_Win" \
        --games 3 --max-frames 2000 --timeout 240
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ACTIVITY_RE = re.compile(
    r"ai-selfplay-activity frame=(\d+) units\[(.*?)\]"
)
OWNER_TOKEN_RE = re.compile(r"(\d+):(\d+)/pop(\d+)")


def parse_activity(log_path: Path) -> list[dict]:
    """Return the per-frame owner activity samples recorded in Jw2.log."""
    samples: list[dict] = []
    if not log_path.exists():
        return samples
    text = log_path.read_text(errors="replace")
    for frame_str, body in ACTIVITY_RE.findall(text):
        owners = {
            int(o): {"units": int(u), "pop": int(p)}
            for o, u, p in OWNER_TOKEN_RE.findall(body)
        }
        samples.append({"frame": int(frame_str), "owners": owners})
    return samples


def run_one_match(exe: Path, install_dir: Path, max_frames: int,
                  timeout: float, index: int, scripted: bool,
                  seed: int | None, random_policy: bool = False) -> dict:
    """Run a single self-play match and return its collected result."""
    result_path = install_dir / "ai_selfplay_result.json"
    log_path = install_dir / "Jw2.log"
    for stale in (result_path, log_path):
        try:
            stale.unlink()
        except FileNotFoundError:
            pass

    # -AISELF: owner 1 is the reconstructed Computer(AI).  Without -AISCRIPT it
    # plays the built-in Owner AI baseline; with it, the scripted packet policy.
    args = [str(exe), "-AISELF", f"-MAXFRAMES:{max_frames}"]
    if random_policy:
        # -AIRANDOM implies the scripted-owner handover but replaces the
        # scripted decision with a uniformly-random *legal* RL action.
        args.append("-AIRANDOM")
    elif scripted:
        args.append("-AISCRIPT")
    if seed is not None:
        # Deterministic, reproducible match; vary per match for diverse data.
        args.append(f"-SEED:{seed}")
    start = time.monotonic()
    timed_out = False
    try:
        proc = subprocess.Popen(args, cwd=str(install_dir))
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            proc.kill()
            proc.wait()
    except OSError as exc:  # pragma: no cover - launch failure
        return {"index": index, "error": f"launch failed: {exc}"}
    elapsed = time.monotonic() - start

    record: dict = {
        "index": index,
        "wall_seconds": round(elapsed, 1),
        "process_timed_out": timed_out,
        "exit_code": proc.returncode,
    }
    if result_path.exists():
        try:
            record["result"] = json.loads(result_path.read_text())
        except (OSError, json.JSONDecodeError) as exc:
            record["result_error"] = str(exc)
    else:
        record["result_error"] = "no ai_selfplay_result.json produced"
    record["activity"] = parse_activity(log_path)
    return record


def summarize_match(record: dict) -> str:
    """One-line human summary of a single match record."""
    if "error" in record:
        return f"  match {record['index']}: ERROR {record['error']}"
    result = record.get("result")
    activity = record.get("activity") or []
    parts = [
        f"  match {record['index']}: "
        f"{record['wall_seconds']}s exit={record['exit_code']}"
    ]
    if record.get("process_timed_out"):
        parts.append("PROCESS-TIMEOUT")
    if not result:
        parts.append(f"no-result ({record.get('result_error', '?')})")
        return " ".join(parts)

    parts.append(f"reason={result.get('reason')} frame={result.get('end_frame')}")
    owners = result.get("owners", [])
    alive = [o["owner"] for o in owners if o.get("alive")]
    parts.append("alive=" + ",".join(str(o) for o in alive))

    # Growth check: did the Computer(AI) owner (1) keep producing?
    if activity:
        first, last = activity[0]["owners"], activity[-1]["owners"]
        deltas = []
        for owner in sorted(last):
            d = last[owner]["units"] - first.get(owner, {}).get("units", 0)
            if last[owner]["units"] or first.get(owner, {}).get("units", 0):
                deltas.append(f"{owner}:{d:+d}")
        parts.append("unit_delta[" + " ".join(deltas) + "]")
    return " ".join(parts)


def summarize_run(records: list[dict]) -> str:
    """Aggregate summary across all matches, focused on owner 1 vs 2/3."""
    lines = ["", "=== self-play summary ==="]
    finished = [r for r in records if r.get("result")]
    lines.append(
        f"matches: {len(records)}  with-result: {len(finished)}  "
        f"clean-exit: {sum(1 for r in records if r.get('exit_code') == 0)}"
    )
    # Compare final unit counts of Computer(AI)=owner1 vs Computers=owner2,3.
    def avg_units(owner: int) -> float:
        vals = [
            next((o["units"] for o in r["result"]["owners"]
                  if o["owner"] == owner), 0)
            for r in finished
        ]
        return round(sum(vals) / len(vals), 1) if vals else 0.0

    if finished:
        lines.append(
            f"avg final units  owner1(Computer(AI))={avg_units(1)}  "
            f"owner2(Computer)={avg_units(2)}  owner3(Computer)={avg_units(3)}"
        )
        idle = [r for r in finished
                if not any(o["owner"] == 1 and o["units"] > 0
                           for o in r["result"]["owners"])]
        if idle:
            lines.append(
                f"WARNING: owner1 had 0 units in {len(idle)} match(es) "
                "(possible stuck/idle bot)"
            )
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install-dir", required=True, type=Path,
        help="deployment directory containing ranker_rebuild.exe and game data")
    parser.add_argument(
        "--exe", default="ranker_rebuild.exe",
        help="executable name inside the install dir")
    parser.add_argument("--games", type=int, default=1)
    parser.add_argument("--max-frames", type=int, default=2000)
    parser.add_argument(
        "--timeout", type=float, default=300.0,
        help="per-match wall-clock timeout in seconds")
    parser.add_argument(
        "--scripted", action="store_true",
        help="drive owner 1 with the scripted packet policy (-AISCRIPT) instead "
             "of the built-in Owner AI baseline")
    parser.add_argument(
        "--random", action="store_true", dest="random_policy",
        help="drive owner 1 with a uniformly-random *legal* RL high-level "
             "action each cycle (-AIRANDOM); validates the encode/mask/translate "
             "plumbing. Takes precedence over --scripted.")
    parser.add_argument(
        "--seed", type=int, default=None,
        help="deterministic base RNG seed; match i uses seed+i for reproducible "
             "but varied games. Omit for wall-clock (non-reproducible) seeding.")
    parser.add_argument(
        "--json-out", type=Path, default=None,
        help="optional path to write the full collected records as JSON")
    args = parser.parse_args(argv)

    exe = args.install_dir / args.exe
    if not exe.exists():
        print(f"error: executable not found: {exe}", file=sys.stderr)
        return 2

    records = []
    for i in range(args.games):
        print(f"[{i + 1}/{args.games}] running self-play match "
              f"(max_frames={args.max_frames}) ...", flush=True)
        seed = None if args.seed is None else args.seed + i
        record = run_one_match(exe, args.install_dir, args.max_frames,
                               args.timeout, i, args.scripted, seed,
                               args.random_policy)
        records.append(record)
        print(summarize_match(record), flush=True)

    print(summarize_run(records))
    if args.json_out:
        args.json_out.write_text(json.dumps(records, indent=2))
        print(f"\nfull records written to {args.json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
