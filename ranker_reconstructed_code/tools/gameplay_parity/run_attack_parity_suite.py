#!/usr/bin/env python3
"""Run every unit attack against render classes 0 through 4."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from parity_result_store import atomic_json, record_key


DIRECT_DAMAGE_PROFILES = {0, 1, 8, 12, 13, 17, 18, 19, 26, 27, 29, 35, 36}

def main() -> int:
    tool_dir = Path(__file__).resolve().parent
    root_default = tool_dir.parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=root_default)
    parser.add_argument("--batch", type=int, action="append", default=[])
    parser.add_argument("--class", dest="render_class", type=int,
                        action="append", default=[])
    parser.add_argument("--start-frame", type=int, default=25)
    parser.add_argument("--end-frame", type=int, default=180)
    parser.add_argument("--trace-interval-ms", type=int, default=100)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()

    root = args.root.resolve()
    report = json.loads((tool_dir / "reports" / "gameplay_inventory.json")
                        .read_text(encoding="utf-8"))
    manifest = json.loads((tool_dir / "attack_batch_manifest.json")
                          .read_text(encoding="utf-8"))
    results_path = tool_dir / "parity_results.json"
    recorded = json.loads(results_path.read_text(encoding="utf-8"))
    records = {record_key(row): row for row in recorded.get("cases", [])}
    units = {row["unit_id"]: row for row in report["units"]}
    attacks = {row["attack_id"]: row for row in report["attacks"]}
    batches = [
        row for row in manifest["batches"]
        if (not args.batch or row["batch_index"] in args.batch) and
        (not args.render_class or row["render_class"] in args.render_class)
    ]
    if not batches:
        raise ValueError("no manifest batches matched the filters")

    trace_script = (root / "ranker_reconstructed_code" / "tools" /
                    "replay_debug" / "trace_replay_divergence.ps1")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact_root = (root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts" / "runs" /
                     f"attack_parity_{stamp}")
    artifact_root.mkdir(parents=True, exist_ok=False)
    failures = 0

    for number, batch in enumerate(batches, 1):
        batch_index = batch["batch_index"]
        render_class = batch["render_class"]
        label = f"B{batch_index:02d}C{render_class}"
        artifact = artifact_root / label
        command = [
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", str(trace_script),
            "-ReplayPath", batch["replay"],
            "-StartFrame", str(args.start_frame),
            "-EndFrame", str(args.end_frame),
            "-OutputDirectory", str(artifact),
            "-TimeoutSeconds", str(args.timeout_seconds),
            "-TraceIntervalMs", str(args.trace_interval_ms),
        ]
        print(f"[{number:02d}/{len(batches):02d}] {label} "
              f"sources={len(batch['cases'])}", flush=True)
        completed = subprocess.run(
            command, cwd=root, capture_output=True, text=True,
            timeout=args.timeout_seconds + 60)
        result_path = artifact / "result.json"
        if completed.returncode == 0 and result_path.exists():
            trace = json.loads(result_path.read_text(encoding="utf-8"))
        else:
            trace = {
                "pass": False,
                "reason": (completed.stderr.strip() or
                           completed.stdout.strip() or
                           f"trace command exited {completed.returncode}"),
            }
        coverage = trace.get("semantic_coverage") or {}
        links = {tuple(row) for row in coverage.get("unit_effect_links", [])}
        health_ranges = coverage.get("unit_health_ranges", {})
        player_ranges = coverage.get("player_unit_state_ranges", {})
        continuous_exact = bool(
            trace.get("pass") and
            trace.get("first_exact_frame") is not None and
            trace.get("last_exact_frame") is not None and
            not trace.get("pair_gaps"))
        batch_failures = 0

        for case in batch["cases"]:
            unit_id = case["unit_id"]
            unit = units[unit_id]
            primary_id = unit["attack_profile"]
            resolved_id = (unit["attack_profile_vs_class3"]
                           if render_class == 3 else primary_id)
            allowed_mask = attacks[primary_id]["target_render_class_mask"]
            allowed = bool(allowed_mask & (1 << render_class))
            source_slot = case["source_slot"]
            target_slot = case["target"]["slot"]
            initial_health = case["target"]["initial_health"]
            health = health_ranges.get(str(target_slot), {})
            target_damaged = health.get("min", initial_health) < initial_health
            effect_link_seen = (resolved_id, source_slot, target_slot) in links
            source_action_cycle_seen = any(
                value & (0x10 | 0x400)
                for value in player_ranges.get(
                    str(source_slot), {}).get("command_flag_values", []))
            if resolved_id in DIRECT_DAMAGE_PROFILES:
                zero_damage_cycle = bool(
                    units[unit_id]["offense"] == 0 and
                    source_action_cycle_seen)
                executed = target_damaged or zero_damage_cycle
                evidence_kind = ("direct_action_cycle_zero_damage"
                                 if zero_damage_cycle else
                                 "target_health_decreased")
            else:
                executed = effect_link_seen
                evidence_kind = "effect_source_target_link"

            if not continuous_exact:
                verdict = "divergent"
            elif allowed and not executed:
                verdict = "not_exercised"
            elif not allowed and executed:
                verdict = "gate_failed"
            else:
                verdict = "exact"
            expectation = "execute" if allowed else "reject_by_primary_profile_mask"
            row = {
                "kind": "attack_target_class",
                "unit_id": unit_id,
                "unit_name": unit["name"],
                "target_render_class": render_class,
                "primary_attack_id": primary_id,
                "resolved_attack_id": resolved_id,
                "resolved_attack_name": attacks[resolved_id]["name"],
                "allowed_target_render_class_mask": allowed_mask,
                "expectation": expectation,
                "result": verdict,
                "executed": executed,
                "evidence_kind": evidence_kind,
                "effect_link_seen": effect_link_seen,
                "target_damaged": target_damaged,
                "source_action_cycle_seen": source_action_cycle_seen,
                "source_slot": source_slot,
                "target_slot": target_slot,
                "target_health_range": health,
                "replay": batch["replay"],
                "replay_sha256": batch["replay_sha256"],
                "rebuild_sha256": trace.get("sha256"),
                "trace_artifact": artifact.relative_to(root).as_posix(),
                "frame_range": [args.start_frame, args.end_frame - 1],
                "exact_pair_count": trace.get("exact_pair_count", 0),
                "first_exact_frame": trace.get("first_exact_frame"),
                "last_exact_frame": trace.get("last_exact_frame"),
                "pair_gaps": trace.get("pair_gaps", []),
                "used_exact_row_template": case["used_exact_row_template"],
                "trace_reason": trace.get("reason"),
                "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
            }
            records[record_key(row)] = row
            batch_failures += verdict != "exact"

        recorded["schema"] = 1
        recorded["cases"] = sorted(
            records.values(), key=lambda row: tuple(
                str(value) for value in record_key(row)))
        atomic_json(results_path, recorded)
        failures += batch_failures
        status = "PASS" if batch_failures == 0 else f"FAIL({batch_failures})"
        print(f"  {status}: exact_frames={trace.get('exact_pair_count', 0)} "
              f"links={len(links)}", flush=True)

    # Derive the 159 catalog field bindings from the finer 400 class cases.
    for binding in report["attack_bindings"]:
        unit_id = binding["unit_id"]
        unit = units[unit_id]
        primary_id = unit["attack_profile"]
        mask = attacks[primary_id]["target_render_class_mask"]
        if binding["variant"] == "vs_class3":
            applicable_classes = [3] if mask & (1 << 3) else []
        else:
            applicable_classes = [
                value for value in (0, 1, 2, 4) if mask & (1 << value)
            ]
        evidence = [
            records.get(("attack_target_class", unit_id, value))
            for value in applicable_classes
        ]
        all_classes_exact = bool(applicable_classes) and all(
            row is not None and row.get("result") == "exact"
            for row in evidence)
        binding_executed = any(
            row is not None and row.get("executed") and
            row.get("resolved_attack_id") == binding["attack_id"]
            for row in evidence)
        complete = all_classes_exact and binding_executed
        if not applicable_classes:
            verdict = "not_player_reachable"
        elif complete:
            verdict = "exact"
        else:
            verdict = "incomplete"
        evidence_hashes = {
            row.get("rebuild_sha256") for row in evidence
            if row is not None and row.get("rebuild_sha256")
        }
        aggregate = {
            "kind": "attack",
            **binding,
            "result": verdict,
            "applicable_target_render_classes": applicable_classes,
            "target_class_case_results": [
                None if row is None else row.get("result") for row in evidence
            ],
            "rebuild_sha256": (next(iter(evidence_hashes))
                               if len(evidence_hashes) == 1 else None),
            "recorded_at_utc": datetime.now(timezone.utc).isoformat(),
        }
        records[record_key(aggregate)] = aggregate

    recorded["cases"] = sorted(
        records.values(), key=lambda row: tuple(
            str(value) for value in record_key(row)))
    atomic_json(results_path, recorded)
    print(f"completed_batches={len(batches)} failed_cases={failures} "
          f"artifacts={artifact_root}", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as error:
        print(f"trace timed out: {error}", file=sys.stderr)
        raise SystemExit(2)
