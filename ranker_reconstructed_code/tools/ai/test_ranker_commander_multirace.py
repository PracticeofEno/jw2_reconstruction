"""Race migration, immutable ownership and self-play curriculum checks."""
from collections import Counter
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch

import ranker_commander_model as model
import ranker_commander_multirace as races
from ranker_commander_eval import policy_tribes


class MultiraceTests(unittest.TestCase):
    def test_residual_checkpoint_migration_preserves_the_learned_policy(self):
        torch.set_num_threads(1)
        torch.manual_seed(6)
        original = model.CommanderPolicy(9, vector_size=606)
        before = {k: v.clone() for k, v in original.state_dict().items()}
        current = model.upgrade_policy(original)
        vector = torch.randn(12, model.VECTOR_SIZE)
        maps = torch.rand(12, *model.MAP_SHAPE)
        old_masks = torch.ones(12, 95, dtype=torch.bool)
        masks = torch.cat((old_masks[:, :42], torch.zeros(12, model.HEAD_SIZES[0] - 42, dtype=torch.bool), old_masks[:, 42:]), 1)
        with torch.no_grad():
            old = original.sample(vector[:, :606], maps, old_masks, deterministic=True)
            new = current.sample(vector, maps, masks, deterministic=True)
        for field in ("action", "value", "logp"):
            torch.testing.assert_close(new[field], old[field], atol=2e-7, rtol=0)
        for k, value in before.items():
            torch.testing.assert_close(original.state_dict()[k], value, atol=0, rtol=0)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legacy.bin"
            model.export_weights(original, path, allow_legacy=True)
            with self.assertRaises(ValueError):
                model.load_weights(path)
            restored = model.load_weights(path, allow_legacy=True)
            self.assertEqual(restored.vector_size, 606)
            model.export_weights(current, Path(directory) / "new.bin")
            self.assertEqual(model.load_weights(Path(directory) / "new.bin").logit_count, model.LOGIT_COUNT)

    def test_cross_race_jobs_balance_sides_and_keep_a_builtin_quota(self):
        with tempfile.TemporaryDirectory() as directory:
            state = dict(phase="selfplay", seed=20, next_seed=100, scores={}, races={})
            for tribe in range(4):
                path = Path(directory) / f"{tribe}.bin"
                path.write_bytes(bytes([tribe]))
                entry = dict(policy=str(path), sha256=races.sha(path), version=0)
                state["races"][str(tribe)] = dict(policy=str(path), games=0, updates=10, pool=[entry])
            jobs = races.training_jobs(state, 1, 40)
            self.assertEqual(sum(j["opponent_key"] == "builtin" for j in jobs), 8)
            learned = [j for j in jobs if j["opponent_key"] != "builtin"]
            self.assertEqual(Counter(j["train_owner"] for j in learned), {1: 16, 2: 16})
            self.assertEqual(Counter(int(j["opponent_key"].split(":")[1]) for j in learned), {0: 8, 1: 8, 2: 8, 3: 8})
            for job in learned:
                self.assertEqual(policy_tribes(job)[job["train_owner"] - 1], 1)
                expected = state["races"]["1"]["policy"]
                actual = job["opponent_weights"] if job["train_owner"] == 2 else expected
                self.assertEqual(actual, expected)

    def test_bootstrap_gate_requires_completed_balanced_evaluation_and_training(self):
        reports = [dict(valid=True, evaluation_valid=True, tribe=t, deterministic=mode,
                        status=1, commander_metrics={}) for t in range(4) for mode in (False, True) for _ in range(6)]
        gate = lambda rows, updates=8: races.bootstrap_gate(rows, updates=updates, minimum_updates=8, expected_games=48)
        self.assertTrue(gate(reports)["ready"])
        self.assertFalse(gate(reports, 7)["ready"])
        self.assertFalse(gate(reports[:-1])["ready"])
        self.assertFalse(gate([{**r, "tribe": 2} for r in reports])["ready"])
        self.assertFalse(gate([{**r, "status": 3} for r in reports])["ready"])
        self.assertFalse(gate([{**r, "commander_metrics": {"mask_violations": 1}} for r in reports])["ready"])

    def test_zero_tribe_is_valid_but_invalid_and_boolean_values_fail(self):
        self.assertEqual(policy_tribes(dict(own_tribe=0, opponent_policy_tribe=3)), (0, 3))
        for value in (True, -1, 4, 1.5, "1"):
            with self.assertRaises(ValueError):
                policy_tribes(dict(own_tribe=value))

    def test_historical_pool_retains_initial_and_best_after_many_updates(self):
        row = dict(pool=[dict(sha256=str(i), version=i) for i in range(20)],
                   best_evaluated=dict(sha256="3", version=3, score=.8))
        races.trim_pool(row)
        self.assertEqual([entry["version"] for entry in row["pool"]], [0, 3, 14, 15, 16, 17, 18, 19])

    def test_same_version_weights_from_another_race_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "elf.bin"
            path.write_bytes(b"weights")
            metadata = dict(own_tribe=1, schema_crc=model.SCHEMA_CRC, weight_version=0, weights_sha256=races.sha(path))
            Path(str(path) + ".json").write_text(json.dumps(metadata))
            row = dict(policy=str(path), version=0, sha256=races.sha(path))
            races.verify_policy(row, 1)
            with self.assertRaises(ValueError):
                races.verify_policy(row, 3)
            path.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                races.verify_policy(row, 1)


if __name__ == "__main__":
    unittest.main()
