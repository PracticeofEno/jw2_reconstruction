"""Recovery comparison progress must remain observable before PPO starts."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import ranker_commander_cohort_report as report


class RecoveryReportingTests(unittest.TestCase):
    def test_transient_windows_reader_lock_does_not_stop_reporting(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "observer.json"
            destination.write_text("old")
            original = Path.replace
            attempts = []

            def busy_once(source, target):
                attempts.append(source)
                if len(attempts) == 1:
                    self.assertEqual(destination.read_text(), "old")
                    raise PermissionError("destination briefly held by a reader")
                return original(source, target)

            with patch.object(Path, "replace", busy_once), patch.object(report.time, "sleep"):
                report.atomic_write(destination, "new")
            self.assertEqual(destination.read_text(), "new")
            self.assertEqual(len(attempts), 2)

    def test_checkpoint_evaluation_reports_progress_and_deduplicates_retries(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            policy = work / "elf/update_00026/policy.bin"
            policy.parent.mkdir(parents=True)
            policy.write_bytes(b"fixture")
            evaluation = policy.parent / "evaluation"
            for mode, valid in (("argmax", True), ("argmax_retry", True), ("sampling", False)):
                receipt = evaluation / mode / "game_00000/output/job.json"
                receipt.parent.mkdir(parents=True)
                receipt.write_text(json.dumps(dict(seed=123, deterministic=mode != "sampling",
                                                  valid=valid, evaluation_valid=valid)))
            state = dict(status="checkpoint_saved", phase="bootstrap", pid=1, active=None,
                         config=dict(evaluation_games=48), races={"1":dict(policy=str(policy),version=10)})
            current = report.progress(work, state)
            self.assertEqual((current["status"], current["race"], current["version"], current["completed"]),
                             ("evaluating", "elf", 10, 1))
            (evaluation / "evaluation.json").write_text("{}")
            self.assertNotIn("completed", report.progress(work, state))

    def test_comparison_progress_does_not_require_a_ppo_cohort(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            directory = work / "elf/baseline"
            result = directory / "argmax/game_00000/output/ai_selfplay_result.json"
            result.parent.mkdir(parents=True)
            result.write_text("{}")
            state = dict(status="recovery_comparison", phase="bootstrap", pid=1,
                         active=dict(tribe=1, policy="baseline"), config=dict(evaluation_games=48),
                         races={"1": dict(comparison_paths=dict(baseline=str(directory)))})
            current = report.progress(work, state)
            self.assertEqual((current["race"], current["completed"], current["jobs"]), ("elf", 1, 48))

    def test_recovery_selection_is_reported_once_without_claiming_ppo_games(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            gate = dict(games=48, argmax_win_rate=0.1, sampling_win_rate=0.2)
            row = dict(recovery_selection="elf", selected="baseline", measurements=dict(baseline=gate, candidate=gate))
            (work / "runner.stdout.log").write_text(json.dumps(row) + "\n", encoding="utf-8")
            state = dict(status="ready_to_train")
            events = report.scan(work, [], state)
            self.assertEqual(len(events), 1)
            self.assertEqual(events[0]["kind"], "recovery_selection")
            self.assertEqual(report.scan(work, events, state), [])


if __name__ == "__main__":
    unittest.main()
