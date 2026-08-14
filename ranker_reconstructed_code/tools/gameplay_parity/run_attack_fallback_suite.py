#!/usr/bin/env python3
"""Run isolated fallback attack replays and promote proven class cases."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from run_attack_parity_suite import (
    DIRECT_DAMAGE_PROFILES,
    atomic_json,
    record_key,
)


def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=tool_dir.parents[2])
    parser.add_argument("--case", action="append", default=[],
                        metavar="UxxxC#")
    parser.add_argument("--trace-interval-ms", type=int, default=250)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()
    root = args.root.resolve()
    inventory = json.loads(
        (tool_dir / "reports" / "gameplay_inventory.json")
        .read_text(encoding="utf-8"))
    manifest = json.loads(
        (tool_dir / "attack_fallback_manifest.json")
        .read_text(encoding="utf-8"))
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded["cases"]}
    units = {row["unit_id"]: row for row in inventory["units"]}
    attacks = {row["attack_id"]: row for row in inventory["attacks"]}
    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact_root = (root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts" / "runs" /
                     f"attack_fallback_{stamp}")
    artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0

    selected = {item.upper() for item in args.case}
    cases = [case for case in manifest["cases"]
             if not selected or
             f"U{case['unit_id']:03d}C{case['target_render_class']}" in
             selected]
    if selected and len(cases) != len(selected):
        found = {f"U{case['unit_id']:03d}C{case['target_render_class']}"
                 for case in cases}
        raise ValueError(f"unknown cases: {sorted(selected - found)}")
    if not cases:
        raise ValueError("no fallback cases selected")

    for number, case in enumerate(cases, 1):
        unit_id = case["unit_id"]
        render_class = case["target_render_class"]
        label = f"U{unit_id:03d}C{render_class}"
        artifact = artifact_root / label
        print(f"[{number:02d}/{len(cases):02d}] {label}",
              flush=True)
        command = [
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script),
            "-ReplayPath", case["replay"],
            "-StartFrame", "25",
            "-EndFrame", str(manifest["end_frame"]),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(args.timeout_seconds),
            "-TraceIntervalMs", str(args.trace_interval_ms),
        ]
        completed = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            timeout=args.timeout_seconds + 60)
        result_path = artifact / "result.json"
        trace = (json.loads(result_path.read_text(encoding="utf-8"))
                 if completed.returncode == 0 and result_path.exists()
                 else {"pass": False, "reason": (
                     completed.stderr.strip() or completed.stdout.strip() or
                     f"trace command exited {completed.returncode}")})
        coverage = trace.get("semantic_coverage") or {}
        links = {tuple(row) for row in coverage.get("unit_effect_links", [])}
        health = coverage.get("unit_health_ranges", {}).get(
            str(case["target_slot"]), {})
        terminal_units = (trace.get("terminal_exact_state") or {}).get(
            "player_units", {})
        terminal_source = terminal_units.get(str(case["source_slot"]), {})
        source_action_cycle_seen = bool(
            terminal_source.get("command_flags", 0) & (0x10 | 0x400))
        primary_id = units[unit_id]["attack_profile"]
        resolved_id = (units[unit_id]["attack_profile_vs_class3"]
                       if render_class == 3 else primary_id)
        damaged = health.get("min", case["target_initial_health"]) < \
            case["target_initial_health"]
        effect_link = (
            resolved_id, case["source_slot"], case["target_slot"]) in links
        direct = resolved_id in DIRECT_DAMAGE_PROFILES
        zero_damage_direct_cycle = bool(
            direct and units[unit_id]["offense"] == 0 and
            source_action_cycle_seen)
        executed = ((damaged or zero_damage_direct_cycle)
                    if direct else effect_link)
        continuous = bool(
            trace.get("pass") and
            trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and
            not trace.get("pair_gaps"))
        accepted_original_rejection = bool(
            case.get("accept_original_rejection") and continuous and
            not executed)
        success = continuous and (executed or accepted_original_rejection)
        old_key = ("attack_target_class", unit_id, render_class)
        old = records.get(old_key, {})
        evidence = {
            **old,
            "kind": "attack_target_class",
            "unit_id": unit_id,
            "unit_name": units[unit_id]["name"],
            "target_render_class": render_class,
            "primary_attack_id": primary_id,
            "resolved_attack_id": resolved_id,
            "resolved_attack_name": attacks[resolved_id]["name"],
            "allowed_target_render_class_mask":
                attacks[primary_id]["target_render_class_mask"],
            "expectation": "execute",
            "result": "exact" if success else "not_exercised",
            "executed": executed,
            "evidence_kind": (
                "continuous_exact_original_rejection"
                if accepted_original_rejection else
                "direct_action_cycle_zero_damage"
                if zero_damage_direct_cycle else
                "target_health_decreased" if direct else
                "effect_source_target_link"),
            "effect_link_seen": effect_link,
            "target_damaged": damaged,
            "source_action_cycle_seen": source_action_cycle_seen,
            "original_rejection_parity_proven":
                accepted_original_rejection,
            "observed_outcome": (
                "rejected_by_original_and_rebuild"
                if accepted_original_rejection else "executed"),
            "terminal_source_state": terminal_source,
            "source_slot": case["source_slot"],
            "target_slot": case["target_slot"],
            "target_health_range": health,
            "replay": case["replay"],
            "replay_sha256": case["replay_sha256"],
            "rebuild_sha256": trace.get("sha256"),
            "trace_artifact": artifact.relative_to(root).as_posix(),
            "frame_range": [25, manifest["end_frame"] - 1],
            "exact_pair_count": trace.get("exact_pair_count", 0),
            "first_exact_frame": trace.get("first_exact_frame"),
            "last_exact_frame": trace.get("last_exact_frame"),
            "pair_gaps": trace.get("pair_gaps", []),
            "used_exact_row_template": case["used_exact_row_template"],
            "evidence_source": "isolated_close_range_fallback",
            "superseded_trace_artifact": old.get("trace_artifact"),
            "trace_reason": trace.get("reason"),
            "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        records[old_key] = evidence
        if not success:
            failures += 1
        print(f"  {'PASS' if success else 'FAIL'}: "
              f"exact_frames={trace.get('exact_pair_count', 0)} "
              f"executed={executed}", flush=True)
        recorded["cases"] = sorted(
            records.values(), key=lambda row: tuple(
                str(value) for value in record_key(row)))
        atomic_json(results_path, recorded)

    # Refresh aggregate field-binding results after promoting fallback cases.
    for binding in inventory["attack_bindings"]:
        unit_id = binding["unit_id"]
        unit = units[unit_id]
        mask = attacks[unit["attack_profile"]]["target_render_class_mask"]
        applicable = ([3] if binding["variant"] == "vs_class3" and
                      mask & (1 << 3) else
                      [value for value in (0, 1, 2, 4)
                       if binding["variant"] == "primary" and
                       mask & (1 << value)])
        rows = [records.get(("attack_target_class", unit_id, value))
                for value in applicable]
        all_classes_exact = bool(applicable) and all(
            row is not None and row.get("result") == "exact"
            for row in rows)
        binding_executed = any(
            row is not None and row.get("executed") and
            row.get("resolved_attack_id") == binding["attack_id"]
            for row in rows)
        complete = all_classes_exact and binding_executed
        evidence_hashes = {
            row.get("rebuild_sha256") for row in rows
            if row is not None and row.get("rebuild_sha256")
        }
        aggregate = {
            "kind": "attack",
            **binding,
            "result": ("not_player_reachable" if not applicable else
                       "exact" if complete else "incomplete"),
            "applicable_target_render_classes": applicable,
            "target_class_case_results": [
                None if row is None else row.get("result") for row in rows],
            "rebuild_sha256": (next(iter(evidence_hashes))
                               if len(evidence_hashes) == 1 else None),
            "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        records[record_key(aggregate)] = aggregate

    recorded["cases"] = sorted(
        records.values(), key=lambda row: tuple(
            str(value) for value in record_key(row)))
    atomic_json(results_path, recorded)
    print(f"completed={len(cases)} failures={failures} "
          f"artifacts={artifact_root}", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"trace timed out: {error}", file=sys.stderr)
        raise SystemExit(2)
