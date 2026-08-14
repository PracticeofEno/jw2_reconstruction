#!/usr/bin/env python3
"""Fail unless every generated player-operable parity case has current evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any

from parity_result_store import atomic_json, record_key


def manifest_cases(path: Path) -> list[tuple[dict[str, Any], dict[str, Any]]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if "batches" in manifest:
        return [(case, batch) for batch in manifest["batches"]
                for case in batch.get("cases", [])]
    return [(case, manifest) for case in manifest.get("cases", [])]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=tool_dir.parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    current_sha = sha256(root / "RankerOCPV_Win" / "ranker_rebuild.exe")
    inventory = json.loads(
        (tool_dir / "reports" / "gameplay_inventory.json")
        .read_text(encoding="utf-8"))
    results = json.loads(
        (tool_dir / "parity_results.json").read_text(encoding="utf-8"))

    expected: dict[tuple[Any, ...], dict[str, Any]] = {}

    def add(kind: str, identity: dict[str, Any], suite: str,
            replay_sha256: str | None = None,
            allowed: tuple[str, ...] = ("exact",)) -> None:
        case = {"kind": kind, **identity}
        key = record_key(case)
        if key in expected:
            raise ValueError(f"duplicate expected identity: {key}")
        expected[key] = {
            "suite": suite,
            "allowed_results": allowed,
            "replay_sha256": replay_sha256,
        }

    for row in inventory["skill_bindings"]:
        add("skill", row, "skills")
    for row in inventory["attack_bindings"]:
        add("attack", row, "attack_bindings",
            allowed=("exact", "not_player_reachable"))

    for case, batch in manifest_cases(tool_dir / "attack_batch_manifest.json"):
        add("attack_target_class", {
            "unit_id": case["unit_id"],
            "target_render_class": batch["render_class"],
        }, "attack_target_classes")

    manifest_suites = [
        ("direct_command_batch_manifest.json", "direct_command",
         "direct_commands"),
        ("linked_release_manifest.json", "linked_release", "linked_release"),
        ("morph_cycle_manifest.json", "morph_cycle", "morph_cycle"),
        ("move_patrol_batch_manifest.json", "move_patrol", "move_patrol"),
        ("secondary_command_batch_manifest.json", "secondary_command",
         "secondary_commands"),
        ("transport_cycle_manifest.json", "transport_cycle",
         "transport_cycle"),
        ("definition_group_batch_manifest.json", "definition_group_order",
         "definition_group"),
        ("production_order_batch_manifest.json", "production_order",
         "production_orders"),
        ("production_order_cancel_batch_manifest.json",
         "production_order_cancel", "production_order_cancel"),
        ("unit_production_batch_manifest.json", "unit_production",
         "unit_production"),
        ("construction_batch_manifest.json", "construction", "construction"),
        ("equipment_apply_batch_manifest.json", "equipment_apply",
         "equipment_apply"),
        ("equipment_toggle_batch_manifest.json", "equipment_toggle",
         "equipment_toggle"),
    ]
    for filename, kind, suite in manifest_suites:
        for case, container in manifest_cases(tool_dir / filename):
            add(kind, case, suite, container.get("replay_sha256"))

    for case, container in manifest_cases(tool_dir / "transport_batch_manifest.json"):
        allowed = (("exact",) if case.get("player_reachable", True) else
                   ("not_player_reachable",))
        add("transport_unload", case, "transport_unload",
            container.get("replay_sha256"), allowed)

    for case, container in manifest_cases(tool_dir / "basic_command_batch_manifest.json"):
        add("direct_action", {"action_selector": 0,
                              "unit_id": case["unit_id"]},
            "guard_idle", container.get("replay_sha256"))
        if case["primary_capable"]:
            add("direct_action", {"action_selector": 1,
                                  "unit_id": case["unit_id"]},
                "primary_point_action", container.get("replay_sha256"))
    for case, container in manifest_cases(tool_dir / "area_toggle_batch_manifest.json"):
        add("direct_action", {"action_selector": 0x0D,
                              "unit_id": case["unit_id"]},
            "area_toggle", container.get("replay_sha256"))
    for case, container in manifest_cases(tool_dir / "harvest_manifest.json"):
        add("direct_action", {"action_selector": 7,
                              "unit_id": case["unit_id"]},
            "harvest", container.get("replay_sha256"))

    rows: dict[tuple[Any, ...], dict[str, Any]] = {}
    duplicate_rows: list[str] = []
    for row in results.get("cases", []):
        key = record_key(row)
        if key in rows:
            duplicate_rows.append(repr(key))
        rows[key] = row

    errors: list[str] = []
    if duplicate_rows:
        errors.extend(f"duplicate result {key}" for key in duplicate_rows)
    suite_status: Counter[tuple[str, str]] = Counter()
    replay_hash_cache: dict[Path, str] = {}
    for key, requirement in expected.items():
        suite = requirement["suite"]
        row = rows.get(key)
        if row is None:
            suite_status[(suite, "missing")] += 1
            errors.append(f"missing {key}")
            continue
        result = row.get("result", "missing")
        suite_status[(suite, result)] += 1
        if result not in requirement["allowed_results"]:
            errors.append(
                f"{key}: result {result!r}, expected {requirement['allowed_results']}")
            continue
        if result == "exact" and row.get("rebuild_sha256") != current_sha:
            errors.append(f"{key}: exact evidence is from a stale rebuild")
        expected_replay_sha = requirement.get("replay_sha256")
        if expected_replay_sha and row.get("replay_sha256") != expected_replay_sha:
            errors.append(f"{key}: stale replay hash")
        replay_name = row.get("replay")
        replay_sha = row.get("replay_sha256")
        if replay_name and replay_sha:
            replay_path = root / replay_name
            if not replay_path.exists():
                errors.append(f"{key}: replay is missing: {replay_name}")
            else:
                actual_sha = replay_hash_cache.setdefault(
                    replay_path, sha256(replay_path))
                if actual_sha != replay_sha:
                    errors.append(f"{key}: replay file hash changed")

    suites: dict[str, dict[str, int]] = {}
    for suite in sorted({item["suite"] for item in expected.values()}):
        values = {status: count for (name, status), count in suite_status.items()
                  if name == suite}
        values["expected"] = sum(
            item["suite"] == suite for item in expected.values())
        suites[suite] = values

    report = {
        "schema": 1,
        "pass": not errors,
        "current_rebuild_sha256": current_sha,
        "expected_case_count": len(expected),
        "recorded_case_count": len(rows),
        "suites": suites,
        "errors": errors,
    }
    output_json = tool_dir / "reports" / "final_parity_audit.json"
    atomic_json(output_json, report)
    lines = [
        "# Final gameplay parity audit",
        "",
        f"- Result: {'PASS' if report['pass'] else 'FAIL'}",
        f"- Current rebuild SHA-256: `{current_sha}`",
        f"- Expected player-operable cases: {len(expected)}",
        f"- Stored canonical result rows: {len(rows)}",
        "",
        "| Suite | Expected | Exact | Explicitly unreachable | Missing/other |",
        "|---|---:|---:|---:|---:|",
    ]
    for suite, counts in suites.items():
        exact = counts.get("exact", 0)
        unreachable = counts.get("not_player_reachable", 0)
        missing_other = counts["expected"] - exact - unreachable
        lines.append(
            f"| {suite} | {counts['expected']} | {exact} | {unreachable} | "
            f"{missing_other} |")
    if errors:
        lines.extend(["", "## Errors", ""])
        lines.extend(f"- {error}" for error in errors[:200])
    (tool_dir / "reports" / "final_parity_audit.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({
        "pass": report["pass"],
        "expected": len(expected),
        "recorded": len(rows),
        "errors": len(errors),
    }))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
