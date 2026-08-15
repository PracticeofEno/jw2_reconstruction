#!/usr/bin/env python3
"""Audit every unit attack binding against the original JW2_12 visuals.

The original effect renderer addresses images as ``record_base + raw_index``.
That detail matters both for the first real image (global resource entry zero)
and for profile 4, whose impact table intentionally reaches profile 5 images.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from inventory_gameplay_matrix import TrcArchive, embedded_name, u32


UNIT_COUNT = 0xAA
ATTACK_PROFILE_COUNT = 0x3D
EFFECT_DEFINITION_BYTES = 0xADC
PALETTE_BYTES = 0x400
RESOURCE_HEADER_BYTES = 0x20
MAX_PHASE_REFERENCES = 128

IMAGE_COUNT_OFFSET = 0x200
PHASES = (
    ("startup", 0x220, 0x230),
    ("active", 0x224, 0x430),
    ("impact", 0x228, 0x630),
)


@dataclass(frozen=True)
class EffectImage:
    owner_profile: int
    local_index: int
    width: int
    height: int
    payload: bytes

    def has_opaque_pixel(self) -> bool:
        cursor = 0
        opaque = False
        for _ in range(self.height):
            if cursor + 2 > len(self.payload):
                raise ValueError("truncated RLE row header")
            row_bytes = struct.unpack_from("<H", self.payload, cursor)[0]
            cursor += 2
            row_end = cursor + row_bytes
            if row_end > len(self.payload):
                raise ValueError("truncated RLE row")
            x = 0
            while x < self.width and cursor < row_end:
                token = self.payload[cursor]
                cursor += 1
                if token == 0:
                    if cursor >= row_end:
                        raise ValueError("truncated RLE transparent run")
                    x += self.payload[cursor]
                    cursor += 1
                else:
                    opaque = True
                    x += 1
            cursor = row_end
        return opaque


@dataclass(frozen=True)
class EffectProfile:
    profile_id: int
    name: str
    definition: bytes
    resource_base: int
    image_count: int


def parse_effect_catalog(path: Path) -> tuple[list[EffectProfile], list[EffectImage]]:
    archive = TrcArchive(path)
    if len(archive.records) < ATTACK_PROFILE_COUNT:
        raise ValueError(f"JW2_12 has only {len(archive.records)} records")

    profiles: list[EffectProfile] = []
    images: list[EffectImage] = []
    for profile_id in range(ATTACK_PROFILE_COUNT):
        record = archive.record(profile_id)
        # The original loader treats sub-0x14 Null.wpn rows as unloaded,
        # zero-filled catalog entries.  They allocate no resources and do not
        # move the global image base.
        if len(record.data) < 0x14:
            definition = bytes(EFFECT_DEFINITION_BYTES)
        elif len(record.data) < EFFECT_DEFINITION_BYTES:
            raise ValueError(f"profile {profile_id} has a truncated definition")
        else:
            definition = record.data[:EFFECT_DEFINITION_BYTES]
        image_count = u32(definition, IMAGE_COUNT_OFFSET)
        base = len(images)
        profiles.append(EffectProfile(
            profile_id,
            embedded_name(definition, record.name, 0x108),
            definition,
            base,
            image_count,
        ))
        if image_count == 0:
            continue

        cursor = EFFECT_DEFINITION_BYTES + PALETTE_BYTES
        for local_index in range(image_count):
            if cursor + RESOURCE_HEADER_BYTES > len(record.data):
                raise ValueError(
                    f"profile {profile_id} image {local_index} has a truncated header")
            width = u32(record.data, cursor)
            height = u32(record.data, cursor + 4)
            payload_size = u32(record.data, cursor + 0x18)
            cursor += RESOURCE_HEADER_BYTES
            end = cursor + payload_size
            if end > len(record.data):
                raise ValueError(
                    f"profile {profile_id} image {local_index} has a truncated payload")
            images.append(EffectImage(
                profile_id, local_index, width, height, record.data[cursor:end]))
            cursor = end
    return profiles, images


def attack_bindings(path: Path) -> list[tuple[int, str, str, int]]:
    archive = TrcArchive(path)
    if len(archive.records) < UNIT_COUNT:
        raise ValueError(f"JW2_09 has only {len(archive.records)} unit records")
    bindings: list[tuple[int, str, str, int]] = []
    for unit_id in range(UNIT_COUNT):
        record = archive.record(unit_id)
        definition = record.data
        name = embedded_name(definition, record.name)
        primary_profile = u32(definition, 0x1A0)
        class3_profile = u32(definition, 0x1A4)
        # Match the gameplay inventory's reachability rule.  Profile zero is
        # normally an absent alternate row, except for BuildMan's explicit
        # primary attack capability (definition flag bit five).
        if primary_profile or (u32(definition, 0x1EC) & (1 << 5)):
            bindings.append((unit_id, name, "primary", primary_profile))
        if class3_profile:
            bindings.append((unit_id, name, "class3", class3_profile))
    return bindings


def phase_references(profile: EffectProfile) -> list[tuple[str, int, int]]:
    references: list[tuple[str, int, int]] = []
    for phase, count_offset, table_offset in PHASES:
        count = min(u32(profile.definition, count_offset), MAX_PHASE_REFERENCES)
        for frame in range(count):
            references.append(
                (phase, frame, u32(profile.definition, table_offset + frame * 4)))
    return references


def audit(unit_path: Path, effect_path: Path) -> int:
    bindings = attack_bindings(unit_path)
    profiles, images = parse_effect_catalog(effect_path)
    used_profile_ids = sorted({profile_id for *_, profile_id in bindings})
    failures: list[str] = []
    resolved_entries: set[int] = set()
    cross_record: list[tuple[int, str, int, int, EffectImage]] = []
    transparent: list[tuple[int, str, int, int]] = []
    reference_count = 0
    visual_profiles = 0

    for profile_id in used_profile_ids:
        if profile_id >= len(profiles):
            failures.append(f"used profile {profile_id} is outside JW2_12")
            continue
        profile = profiles[profile_id]
        references = phase_references(profile)
        if profile.image_count == 0:
            continue
        visual_profiles += 1
        if not references:
            failures.append(
                f"profile {profile_id} ({profile.name}) declares images but no phase frames")
            continue
        opaque_count = 0
        for phase, frame, raw_index in references:
            reference_count += 1
            entry = profile.resource_base + raw_index
            if entry >= len(images):
                failures.append(
                    f"profile {profile_id} {phase}[{frame}] raw {raw_index} "
                    f"resolves past resource store ({entry} >= {len(images)})")
                continue
            image = images[entry]
            resolved_entries.add(entry)
            try:
                opaque = image.has_opaque_pixel()
            except ValueError as exc:
                failures.append(
                    f"profile {profile_id} {phase}[{frame}] entry {entry}: {exc}")
                continue
            if opaque:
                opaque_count += 1
            else:
                transparent.append((profile_id, phase, frame, entry))
            if image.owner_profile != profile_id:
                cross_record.append((profile_id, phase, frame, raw_index, image))
        if opaque_count == 0:
            failures.append(
                f"profile {profile_id} ({profile.name}) has no visible referenced frame")

    giant = profiles[2]
    giant_active_zero = any(
        phase == "active" and raw_index == 0 and giant.resource_base + raw_index == 0
        for phase, _, raw_index in phase_references(giant))
    if not giant_active_zero:
        failures.append("Giant profile 2 active frame zero does not resolve to resource entry zero")

    source_units = len({unit_id for unit_id, *_ in bindings})
    print("ATTACK_EFFECT_RESOURCE_AUDIT")
    print(f"unit_definitions={UNIT_COUNT}")
    print(f"attack_source_types={source_units}")
    print(f"unit_profile_bindings={len(bindings)}")
    print(f"used_profiles={len(used_profile_ids)}")
    print(f"visual_profiles={visual_profiles}")
    print(f"direct_profiles={len(used_profile_ids) - visual_profiles}")
    print(f"catalog_images={len(images)}")
    print(f"referenced_phase_frames={reference_count}")
    print(f"unique_referenced_images={len(resolved_entries)}")
    print(f"cross_record_references={len(cross_record)}")
    print(f"intentional_transparent_frames={len(transparent)}")
    print(f"giant_active_resource_entry_zero={'PASS' if giant_active_zero else 'FAIL'}")
    for profile_id, phase, frame, entry in transparent:
        print(f"INFO transparent profile={profile_id} phase={phase} frame={frame} entry={entry}")
    for profile_id, phase, frame, raw_index, image in cross_record:
        print(
            "INFO cross-record "
            f"profile={profile_id} phase={phase} frame={frame} raw={raw_index} "
            f"owner_profile={image.owner_profile} owner_local={image.local_index}")
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print("ATTACK_EFFECT_RESOURCE_AUDIT_FAIL")
        return 1
    print("ATTACK_EFFECT_RESOURCE_AUDIT_PASS")
    return 0


def main() -> int:
    workspace = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--unit-archive", type=Path,
        default=workspace / "RankerOCPV_Win" / "JW2_09.TRC")
    parser.add_argument(
        "--effect-archive", type=Path,
        default=workspace / "RankerOCPV_Win" / "JW2_12.TRC")
    args = parser.parse_args()
    return audit(args.unit_archive, args.effect_archive)


if __name__ == "__main__":
    raise SystemExit(main())
