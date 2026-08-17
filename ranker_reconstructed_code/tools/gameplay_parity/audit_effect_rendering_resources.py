#!/usr/bin/env python3
"""Audit every original effect record used by gameplay rendering.

This covers the 61 low-ID attack effects in JW2_12, the 46 high-ID
skill/production effects in JW2_11, and all 139 ground/map-effect definitions
whose sprites come from JW2_07's 121-entry item group.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from audit_attack_effect_resources import (
    MAX_PHASE_REFERENCES,
    PHASES,
    parse_effect_catalog,
)
from inventory_gameplay_matrix import TrcArchive, u32


LOW_EFFECT_COUNT = 0x3D
HIGH_EFFECT_COUNT = 0x2E
HIGH_EFFECT_ID_BASE = 0x3D
EQUIPMENT_RECORD_INDEX = 2
EQUIPMENT_VERSION = 0x65
EQUIPMENT_RECORD_BYTES = 0x28C
EQUIPMENT_MAX_COUNT = 0x97
MAP_EFFECT_ICON_OFFSET = 0x88
MAP_EFFECT_FRAME_PERIOD_OFFSET = 0xB0
JW207_ITEM_RECORD_BASE = 0x324
JW207_ITEM_COUNT = 0x79


def audit_auxiliary_catalog(path: Path, count: int, effect_id_base: int,
        failures: list[str]) -> tuple[int, int, int, int, int]:
    profiles, images = parse_effect_catalog(path, count)
    reference_count = 0
    cross_record_count = 0
    transparent_count = 0

    for profile in profiles:
        modes = tuple(u32(profile.definition, offset)
            for offset in (0x214, 0x218, 0x21C))
        handler = u32(profile.definition, 0x150)
        effect_id = effect_id_base + profile.profile_id
        if any(mode > 9 for mode in modes):
            failures.append(
                f"effect 0x{effect_id:02x} has invalid draw modes {modes}")
        if handler > 4:
            failures.append(
                f"effect 0x{effect_id:02x} has invalid sort handler {handler}")

        visible_frames = 0
        for phase, count_offset, table_offset in PHASES:
            phase_count = u32(profile.definition, count_offset)
            if phase_count > MAX_PHASE_REFERENCES:
                failures.append(
                    f"effect 0x{effect_id:02x} {phase} count {phase_count} "
                    f"exceeds {MAX_PHASE_REFERENCES}")
            for frame in range(min(phase_count, MAX_PHASE_REFERENCES)):
                raw_index = u32(profile.definition, table_offset + frame * 4)
                entry = profile.resource_base + raw_index
                reference_count += 1
                if entry >= len(images):
                    failures.append(
                        f"effect 0x{effect_id:02x} {phase}[{frame}] raw "
                        f"{raw_index} resolves outside {path.name}")
                    continue
                image = images[entry]
                if image.owner_profile != profile.profile_id:
                    cross_record_count += 1
                try:
                    opaque = image.has_opaque_pixel()
                except ValueError as exc:
                    failures.append(
                        f"effect 0x{effect_id:02x} {phase}[{frame}]: {exc}")
                    continue
                if opaque:
                    visible_frames += 1
                else:
                    transparent_count += 1

        # Zero-image action rows and the original no-draw Hide handlers are
        # valid.  A row that actually owns images must reference at least one
        # visible frame unless all three phase counts are zero.
        phase_total = sum(u32(profile.definition, offset)
            for _, offset, _ in PHASES)
        if profile.image_count != 0 and phase_total != 0 and visible_frames == 0:
            failures.append(
                f"effect 0x{effect_id:02x} owns images but has no visible frame")

    return (len(profiles), len(images), reference_count,
        cross_record_count, transparent_count)


def load_map_effect_rows(path: Path) -> list[tuple[int, int]]:
    archive = TrcArchive(path)
    if len(archive.records) <= EQUIPMENT_RECORD_INDEX:
        raise ValueError(f"{path.name} has no equipment record 2")
    data = archive.record(EQUIPMENT_RECORD_INDEX).data
    if len(data) < 8:
        raise ValueError("equipment header is truncated")
    version, count = struct.unpack_from("<II", data, 0)
    if version != EQUIPMENT_VERSION or count >= EQUIPMENT_MAX_COUNT:
        raise ValueError(
            f"unexpected equipment header version={version:#x} count={count}")
    required = 8 + count * EQUIPMENT_RECORD_BYTES
    if len(data) < required:
        raise ValueError("equipment rows are truncated")
    rows: list[tuple[int, int]] = []
    for effect_id in range(count):
        base = 8 + effect_id * EQUIPMENT_RECORD_BYTES
        rows.append((
            u32(data, base + MAP_EFFECT_ICON_OFFSET),
            u32(data, base + MAP_EFFECT_FRAME_PERIOD_OFFSET),
        ))
    return rows


def audit_map_effects(equipment_path: Path, jw207_path: Path,
        failures: list[str]) -> tuple[int, int]:
    rows = load_map_effect_rows(equipment_path)
    jw207 = TrcArchive(jw207_path)
    required_records = JW207_ITEM_RECORD_BASE + JW207_ITEM_COUNT
    if len(jw207.records) < required_records:
        failures.append(
            f"{jw207_path.name} has {len(jw207.records)} records; "
            f"item group needs {required_records}")
    for effect_id, (icon, _) in enumerate(rows):
        if icon >= JW207_ITEM_COUNT:
            failures.append(
                f"map effect {effect_id} icon {icon} is outside JW2_07 item group")

    expected_meat_periods = (0, 3, 3, 3, 1)
    actual_meat_periods = tuple(period for _, period in rows[:5])
    if actual_meat_periods != expected_meat_periods:
        failures.append(
            f"map-effect IDs 0..4 periods {actual_meat_periods} do not match "
            f"original {expected_meat_periods}")
    return len(rows), max((icon for icon, _ in rows), default=0)


def audit(root: Path) -> int:
    failures: list[str] = []
    low = audit_auxiliary_catalog(
        root / "JW2_12.TRC", LOW_EFFECT_COUNT, 0, failures)
    high = audit_auxiliary_catalog(
        root / "JW2_11.TRC", HIGH_EFFECT_COUNT, HIGH_EFFECT_ID_BASE, failures)
    map_count, map_max_icon = audit_map_effects(
        root / "JW2_10.TRC", root / "JW2_07.TRC", failures)

    print("EFFECT_RENDERING_RESOURCE_AUDIT")
    print(f"low_effect_records={low[0]}")
    print(f"low_effect_images={low[1]}")
    print(f"low_effect_phase_frames={low[2]}")
    print(f"low_effect_cross_record_frames={low[3]}")
    print(f"low_effect_transparent_frames={low[4]}")
    print(f"high_effect_records={high[0]}")
    print(f"high_effect_images={high[1]}")
    print(f"high_effect_phase_frames={high[2]}")
    print(f"high_effect_cross_record_frames={high[3]}")
    print(f"high_effect_transparent_frames={high[4]}")
    print(f"map_effect_records={map_count}")
    print(f"map_effect_max_icon={map_max_icon}")
    print(f"map_effect_item_group_size={JW207_ITEM_COUNT}")
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print("EFFECT_RENDERING_RESOURCE_AUDIT_FAIL")
        return 1
    print("EFFECT_RENDERING_RESOURCE_AUDIT_PASS")
    return 0


def main() -> int:
    workspace = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-root", type=Path,
        default=workspace / "RankerOCPV_Win")
    args = parser.parse_args()
    return audit(args.data_root)


if __name__ == "__main__":
    raise SystemExit(main())
