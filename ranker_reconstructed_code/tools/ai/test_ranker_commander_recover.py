"""Builtin-only scheduling and candidate adoption protect existing checkpoints."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import ranker_commander_multirace as races
import ranker_commander_recover as recover


class RecoveryTests(unittest.TestCase):
    def test_builtin_only_schedule_never_trains_tyrano_or_switches_to_selfplay(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            executable = work / "ranker_rebuild.exe"
            executable.write_bytes(b"mock runtime, never executed")
            state = dict(schema_crc=races.SCHEMA_CRC, executable=str(executable), executable_sha256=races.sha(executable),
                         training_sources=races.training_sources(), phase="bootstrap", cycles=0, next_race=0,
                         config=dict(active_races=[1, 3, 0], builtin_only=True, threads=1),
                         races={str(t): dict(ready=True, version=20, policy=f"race{t}.bin") for t in range(4)},
                         catalog_sha256=races.sha(Path(races.__file__).with_name("commander_races.json")),
                         skills_catalog_sha256=races.sha(Path(races.__file__).with_name("commander_skills.json")))
            tyrano = deepcopy(state["races"]["2"])
            races.write_json(work / "state.json", state)
            scheduled = []
            with patch.object(races, "train_race", side_effect=lambda a, s, t: scheduled.append(t)):
                races.train(SimpleNamespace(work_dir=work, cycles=2))
            final = json.loads((work / "state.json").read_text())
            self.assertEqual(scheduled, [1, 3, 0, 1, 3, 0])
            self.assertEqual(final["phase"], "bootstrap")
            self.assertEqual(final["races"]["2"], tyrano)

    def test_builtin_override_balances_full_speed_opponents_even_if_parent_was_selfplay(self):
        state = dict(seed=1, next_seed=1800000000, phase="selfplay", config=dict(builtin_only=True),
                     races={"1": dict(games=24, updates=24)})
        jobs = races.training_jobs(state, 1, 12)
        self.assertEqual([j["tribe"] for j in jobs], [0, 1, 2, 3] * 3)
        self.assertTrue(all(j["opp_slow"] == 0 and j["curriculum"] == 2 and
                            j["opponent_key"] == "builtin" and j["own_tribe"] == 1 for j in jobs))
        self.assertFalse(any("opponent_weights" in j for j in jobs))

    def test_candidate_needs_improvement_in_complete_comparable_games(self):
        baseline = dict(games=48, coverage_complete=True, mask_violations=0,
                        argmax_win_rate=2 / 24, sampling_win_rate=3 / 24)
        candidate = dict(baseline, argmax_win_rate=4 / 24)
        self.assertTrue(recover.prefer_candidate(baseline, candidate))
        self.assertFalse(recover.prefer_candidate(baseline, baseline))
        for changes in (dict(argmax_win_rate=3 / 24), dict(sampling_win_rate=2 / 24),
                        dict(coverage_complete=False), dict(mask_violations=1), dict(games=24)):
            self.assertFalse(recover.prefer_candidate(baseline, dict(candidate, **changes)))

    def test_invalid_active_races_are_rejected(self):
        for order in ([], [1, 1], [True], [4]):
            with self.assertRaises(ValueError):
                races.active_order(dict(config=dict(active_races=order)))


if __name__ == "__main__":
    unittest.main()
