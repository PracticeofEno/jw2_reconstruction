#!/usr/bin/env python3
"""Generate the complete per-unit special-action replay fixture set."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


ALLY_TARGET_ACTIONS = {0, 2, 8, 10, 17, 21, 25}


def main() -> int:
    root = Path(__file__).resolve().parents[3]
    report_path = Path(__file__).resolve().parent / "reports" / "gameplay_inventory.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    actions = {row["action_id"]: row for row in report["actions"]}
    generator = Path(__file__).resolve().parent / "generate_skill_replay.py"

    for binding in report["skill_bindings"]:
        unit = binding["unit_id"]
        action = binding["action_id"]
        mode = actions[action]["direction_or_mode"]
        target_owner = 0 if action in ALLY_TARGET_ACTIONS else 1
        command = [
            sys.executable, str(generator),
            "--template-map", f"(2) Skill P2P A{action:02d} U{unit:03d}.trk",
            "--output-stem", f"(2) GP Skill A{action:02d} U{unit:03d}",
            "--action", str(action),
            "--end-frame", "600",
        ]
        if mode == 0:
            command += ["--target", "none"]
        elif mode in (1, 2):
            # Slot 25 is already 230 world units from the caster and gives
            # area effects a concrete enemy candidate at the cast point.
            command += ["--target", "point", "--target-slot", "25",
                        "--target-owner", str(target_owner)]
        elif mode == 4:
            command += ["--target", "lifecycle", "--target-slot", "12",
                        "--target-owner", str(target_owner),
                        "--target-x", "2400", "--target-y", "832"]
        else:
            command += ["--target", "unit", "--target-slot", "12",
                        "--target-owner", str(target_owner),
                        "--target-x", "2400", "--target-y", "832"]
        subprocess.run(command, cwd=root, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
