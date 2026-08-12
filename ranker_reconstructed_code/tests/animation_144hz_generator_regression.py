#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
import json
import struct
import tempfile


TOOL = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "animation_144hz"
    / "unit_sprite_assets.py"
)
SPEC = importlib.util.spec_from_file_location("unit_sprite_assets", TOOL)
assert SPEC is not None and SPEC.loader is not None
assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assets
SPEC.loader.exec_module(assets)


def frame(rows: list[list[int]], offset_x: int = 0) -> object:
    payload = bytearray()
    for row in rows:
        encoded = bytearray()
        column = 0
        while column < len(row):
            if row[column] != 0:
                encoded.append(row[column])
                column += 1
                continue
            run = 1
            while column + run < len(row) and row[column + run] == 0:
                run += 1
            encoded.extend((0, run))
            column += run
        payload.extend(len(encoded).to_bytes(2, "little"))
        payload.extend(encoded)
    metadata = (
        len(rows[0]), len(rows), offset_x & 0xFFFFFFFF, 0,
        0, 0,
    )
    return assets.SpriteFrame(metadata, bytes(payload))


def main() -> int:
    similar_source = frame([
        [0, 2, 2, 0],
        [2, 3, 3, 2],
        [0, 2, 2, 0],
    ])
    similar_target = frame([
        [0, 2, 2, 0],
        [2, 3, 3, 2],
        [0, 2, 2, 0],
    ], 1)
    frames, mode, coverage = assets.build_hybrid_morph_frames(
        similar_source, similar_target
    )
    assert mode == "pixel_correspondence"
    assert coverage > 0.9
    assert len(frames) == assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT

    disjoint_target = frame([
        [2, 2, 0, 0, 0, 0],
        [2, 2, 0, 0, 0, 0],
    ])
    bridge_1 = frame([
        [0, 2, 2, 0, 0, 0],
        [0, 2, 2, 0, 0, 0],
    ], 4)
    bridge_2 = frame([
        [0, 0, 2, 2, 0, 0],
        [0, 0, 2, 2, 0, 0],
    ], 8)
    bridge_3 = frame([
        [0, 0, 3, 3, 0, 0],
        [0, 0, 3, 3, 0, 0],
    ], 12)
    bridge_4 = frame([
        [0, 0, 0, 3, 3, 0],
        [0, 0, 0, 3, 3, 0],
    ], 16)
    bridge_5 = frame([
        [0, 0, 0, 0, 3, 3],
        [0, 0, 0, 0, 3, 3],
    ], 18)
    disjoint_source = frame([
        [0, 0, 0, 0, 3, 3],
        [0, 0, 0, 0, 3, 3],
    ], 20)
    candidates = [
        disjoint_target, bridge_1, bridge_2, bridge_3,
        bridge_4, bridge_5, disjoint_source,
    ]
    frames, mode, coverage = assets.build_hybrid_morph_frames(
        disjoint_source, disjoint_target, candidates, 6, 0
    )
    assert mode == "optical_flow"
    assert coverage < 0.7
    assert len(frames) == assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT

    long_bridge = [
        frame([[2 + (index % 2), 2 + (index % 2)]], index * 2)
        for index in range(15)
    ]
    selected = assets.select_original_bridge_frames(long_bridge, 14, 0)
    assert selected == [
        long_bridge[13], long_bridge[12], long_bridge[10], long_bridge[9],
        long_bridge[8], long_bridge[7], long_bridge[6], long_bridge[5],
        long_bridge[3], long_bridge[2], long_bridge[1],
    ]

    direction_sequence = [
        frame([[0, 2, 2, 0], [2, 2, 2, 2], [0, 2, 2, 0]]),
        frame([[0, 2, 2, 0], [2, 2, 2, 2], [0, 2, 2, 0]], 1),
        frame([[2, 2, 0, 0], [2, 2, 2, 0], [2, 2, 0, 0]], 20),
        frame([[2, 2, 0, 0], [2, 2, 2, 0], [2, 2, 0, 0]], 21),
    ]
    assert assets.is_local_view_direction_boundary(direction_sequence, 1, 2)
    direction_frames, direction_mode, _coverage = (
        assets.build_hybrid_morph_frames(
            direction_sequence[1], direction_sequence[2],
            direction_sequence, 1, 2,
        )
    )
    assert direction_mode == "direction_distance_field"
    assert len(direction_frames) == assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
    assert all(direction_frames)
    maximum_endpoint_components = max(
        len(assets.dot_components(assets.endpoint_dots(direction_sequence[1]), False)),
        len(assets.dot_components(assets.endpoint_dots(direction_sequence[2]), False)),
    )
    assert all(
        len(assets.dot_components(generated, False)) <= maximum_endpoint_components
        for generated in direction_frames
    )

    with tempfile.TemporaryDirectory() as temporary_directory:
        archive_bytes = b"TRC test fixture"
        fake_archive = type("FakeArchive", (), {"data": archive_bytes})()
        override_path = Path(temporary_directory) / "overrides.json"
        override_path.write_text(json.dumps({
            "schema": 1,
            "source_archive_sha256": assets.hashlib.sha256(
                archive_bytes
            ).hexdigest(),
            "source_archive_crc32": (
                f"{assets.binascii.crc32(archive_bytes) & 0xffffffff:08x}"
            ),
            "intermediate_frames_per_transition": (
                assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
            ),
            "overrides": [{
                "unit_type": 0,
                "group": 0,
                "source": 2,
                "target": 3,
                "frames": [
                    [[step, step + 1, 2]]
                    for step in range(assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT)
                ],
            }],
        }), encoding="utf-8")
        loaded_overrides = assets.load_keyframe_overrides(
            fake_archive, override_path
        )
        assert loaded_overrides[(0, 0, 2, 3)] == [
            {(step, step + 1): 2}
            for step in range(assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT)
        ]
        archive_result = assets.build_archive_transition(
            similar_source,
            similar_target,
            False,
            authored_frames=loaded_overrides[(0, 0, 2, 3)],
        )
        assert archive_result[-2] == "authored_keyframes"
        assert archive_result[-1] == 1.0

    direction_source = frame([
        [0, 0, 2, 0, 0],
        [0, 2, 2, 2, 0],
        [2, 2, 2, 2, 2],
        [0, 2, 2, 2, 0],
        [0, 0, 2, 0, 0],
        [0, 0, 2, 0, 0],
        [0, 0, 2, 0, 0],
    ])
    direction_target = frame([
        [0, 0, 0, 0, 0, 0, 0],
        [0, 0, 3, 3, 0, 0, 0],
        [3, 3, 3, 3, 3, 3, 3],
    ])
    assert assets.should_use_single_silhouette_visual_morph(
        direction_source, direction_target
    )
    direction_frames, direction_mode, _coverage = (
        assets.build_hybrid_morph_frames(
            direction_source, direction_target
        )
    )
    assert direction_mode == "direction_single_silhouette"
    assert len(direction_frames) == assets.VISUAL_ARCHIVE_INTERMEDIATE_COUNT
    assert all(direction_frames)

    # Direction rows are not a flat cycle. The old generator manufactured
    # 2->3 and 5->0 at row boundaries, which produced the broken BuildMan
    # turns. Temporal edges stay inside each row and direction changes connect
    # equal action phases instead.
    definition = bytearray(assets.UNIT_DEFINITION_BYTES)
    frame_table = [0, 0, 1, 1, 2, 2, 1, 1, 0]
    for index, value in enumerate(frame_table):
        struct.pack_into("<I", definition, 0x140C + index * 4, value)
    for index in range(len(frame_table), 64):
        struct.pack_into("<I", definition, 0x140C + index * 4, 0)
    for direction, row_base in enumerate((0, 0, 3, 3, 3, 3, 3, 3, 3)):
        struct.pack_into("<I", definition, 0x2248 + direction * 4, row_base)
    graph_unit = assets.UnitRecord(
        0, "TEST", "BuildMan rows", bytes(definition), b"",
        [[similar_source, similar_source, similar_source,
          similar_target, similar_target, similar_target]] +
        [[] for _ in range(assets.UNIT_IMAGE_GROUPS - 1)],
        len(definition),
    )
    graph = set(assets.animation_transition_pairs(graph_unit))
    assert (0, 0, 1) in graph and (0, 1, 2) in graph
    assert (0, 3, 4) in graph and (0, 4, 5) in graph
    assert (0, 0, 3) in graph and (0, 1, 4) in graph and (0, 2, 5) in graph
    assert (0, 2, 3) not in graph and (0, 5, 0) not in graph
    temporal_graph = set(assets.animation_temporal_transition_pairs(graph_unit))
    assert (0, 0, 1) in temporal_graph and (0, 1, 2) in temporal_graph
    assert (0, 3, 4) in temporal_graph and (0, 4, 5) in temporal_graph
    assert (0, 0, 3) not in temporal_graph
    assert (0, 1, 4) not in temporal_graph
    assert (0, 2, 5) not in temporal_graph
    assert (0, 2, 3) not in temporal_graph and (0, 5, 0) not in temporal_graph
    print("144 Hz animation generator regression: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
