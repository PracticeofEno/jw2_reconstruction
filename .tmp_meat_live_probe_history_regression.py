"""Deterministic regression for the meat live probe's frame-history join.

This test imports the read-only probe but replaces both process capture
functions with scripted snapshots.  It therefore opens no process, creates no
GUI, and writes no external state.
"""

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PROBE_PATH = ROOT / ".tmp_meat_live_probe.py"


def load_probe():
    spec = importlib.util.spec_from_file_location(
        "meat_live_probe_history_regression_target", PROBE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load meat live probe")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ScriptedCapture:
    def __init__(self, samples, side):
        self._samples = iter(samples)
        self.side = side
        self.calls = 0

    def __call__(self, _process, attempts=8):
        if attempts != 2:
            raise AssertionError(
                f"history join passed unexpected capture attempts={attempts}")
        self.calls += 1
        sample = next(self._samples)
        if isinstance(sample, tuple):
            frame, revision = sample
        else:
            frame, revision = sample, f"frame-{sample}"
        return {
            "frame": frame,
            "rng": [frame, frame * 3, frame * 7],
            "units": {self.side: revision},
            "effects": [(self.side, frame, revision)],
        }


def exercise_with_captures(probe, original_capture, rebuild_capture,
                           histories, attempts):
    saved_original = probe.capture_stable_original
    saved_rebuild = probe.capture_stable_rebuild
    probe.capture_stable_original = original_capture
    probe.capture_stable_rebuild = rebuild_capture
    try:
        return probe.capture_stable_pair(
            object(), object(), histories, attempts=attempts)
    finally:
        probe.capture_stable_original = saved_original
        probe.capture_stable_rebuild = saved_rebuild


def test_latest_candidate_requires_following_frame(probe):
    histories = probe.new_stable_pair_histories()

    # A changed second read is not quiescent.  The late state must repeat before
    # it becomes a candidate, and remains unpairable until frame 51 is seen.
    pair = exercise_with_captures(
        probe,
        ScriptedCapture([(50, "original-early"),
                         (50, "original-late"),
                         (50, "original-late")], "original"),
        ScriptedCapture([(50, "rebuild-early"),
                         (50, "rebuild-late"),
                         (50, "rebuild-late")], "rebuild"),
        histories,
        attempts=3,
    )
    assert pair is None
    assert histories["original"]["finalized"] == {}
    assert histories["rebuild"]["finalized"] == {}
    assert histories["original"]["candidate"]["units"] == {
        "original": "original-late"}
    assert histories["rebuild"]["candidate"]["units"] == {
        "rebuild": "rebuild-late"}

    # Observing frame 51 finalizes the latest frame-50 candidates.  It must not
    # expose either early/mid-tick candidate.
    pair = exercise_with_captures(
        probe,
        ScriptedCapture([(51, "original-next")], "original"),
        ScriptedCapture([(51, "rebuild-next")], "rebuild"),
        histories,
        attempts=1,
    )
    assert pair is not None
    assert pair["frame"] == 50
    assert pair["original"]["units"] == {"original": "original-late"}
    assert pair["rebuild"]["units"] == {"rebuild": "rebuild-late"}
    assert histories["original"]["candidate"] is None
    assert histories["rebuild"]["candidate"] is None


def test_one_frame_skew_join(probe):
    # No same-iteration pair exists: (40, 39), (41, 40), then (42, 41).
    # Original frame 40 is finalized on original 41; rebuild frame 40 is only
    # finalized on rebuild 41 one iteration later.  Only the finalized history
    # join can pair them.
    original_frames = [40, 40, 41, 41, 42, 42]
    rebuild_frames = [39, 39, 40, 40, 41, 41]
    assert all(left != right for left, right in
               zip(original_frames, rebuild_frames))

    original_capture = ScriptedCapture(original_frames, "original")
    rebuild_capture = ScriptedCapture(rebuild_frames, "rebuild")
    histories = probe.new_stable_pair_histories()
    pair = exercise_with_captures(
        probe, original_capture, rebuild_capture, histories, attempts=6)

    assert pair is not None
    assert pair["frame"] == 40
    assert pair["original"]["units"] == {"original": "frame-40"}
    assert pair["rebuild"]["units"] == {"rebuild": "frame-40"}
    assert original_capture.calls == 5
    assert rebuild_capture.calls == 5
    # Consumed/older finalized frames are removed, while newer unmatched data
    # and each side's current candidate remain available.
    assert set(histories["original"]["finalized"]) == {41}
    assert histories["rebuild"]["finalized"] == {}
    assert histories["original"]["candidate"] is None
    assert histories["rebuild"]["candidate"] is None


def test_unmatched_histories_are_bounded(probe):
    sample_count = 102
    original_capture = ScriptedCapture(
        [frame for frame in range(sample_count) for _ in range(2)],
        "original")
    rebuild_capture = ScriptedCapture(
        [frame for frame in range(1000, 1000 + sample_count)
         for _ in range(2)], "rebuild")
    histories = probe.new_stable_pair_histories()
    pair = exercise_with_captures(
        probe, original_capture, rebuild_capture, histories,
        attempts=sample_count * 2)

    assert pair is None
    original_finalized = histories["original"]["finalized"]
    rebuild_finalized = histories["rebuild"]["finalized"]
    assert len(original_finalized) == probe.PAIR_HISTORY_LIMIT == 64
    assert len(rebuild_finalized) == probe.PAIR_HISTORY_LIMIT
    assert set(original_finalized) == set(range(37, 101))
    assert set(rebuild_finalized) == set(range(1037, 1101))
    assert histories["original"]["candidate"]["frame"] == 101
    assert histories["rebuild"]["candidate"]["frame"] == 1101


def test_explicit_tracking_and_type_flag_exclusion(probe):
    assert probe.explicit_meat_track_slots([7, 9], 21) == {7, 9, 21}
    assert probe.explicit_meat_track_slots([], None) == set()

    row = {
        "type": 11,
        "owner": 1,
        "type_flags": 0xDEADBEEF,
        "max_health": 100,
        "health": 80,
        "action_mode": 4,
        "cargo": 0,
        "x": 10,
        "y": 20,
    }
    assert "type_flags" not in probe.meat_unit_projection(row)

    before = {
        "effects": [],
        "units": {
            7: dict(row),
            99: dict(row),
        },
    }
    after = {
        "effects": [],
        "units": {
            7: dict(row, action_mode=3, health=81),
            99: dict(row, action_mode=3, health=81),
        },
    }
    event = probe.snapshots_diff(before, after, {7})
    assert [change["slot"] for change in event["consumed"]] == [7]


def test_rng_phase_barrier(probe):
    pair = {
        "original": {"rng": [10, 20, 30]},
        "rebuild": {"rng": [10, 20, 30]},
    }
    assert probe.pair_phase_aligned(pair)
    pair["rebuild"]["rng"][2] += 1
    assert not probe.pair_phase_aligned(pair)


def main():
    probe = load_probe()
    test_latest_candidate_requires_following_frame(probe)
    test_one_frame_skew_join(probe)
    test_unmatched_histories_are_bounded(probe)
    test_explicit_tracking_and_type_flag_exclusion(probe)
    test_rng_phase_barrier(probe)
    print("MEAT_LIVE_HISTORY_JOIN_PASS finalized=yes skew=1 "
          "history_limit=64 explicit_slots=yes rng_phase_barrier=yes")


if __name__ == "__main__":
    main()
