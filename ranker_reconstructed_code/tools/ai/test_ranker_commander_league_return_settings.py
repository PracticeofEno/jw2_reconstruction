"""League return settings survive real checkpoint and optimizer transitions."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
import torch

import ranker_commander_league as league
from ranker_commander_model import CommanderPolicy, load_weights
from ranker_commander_rollout import MAP_SHAPE, RECORD_DTYPE, WIN, write_rollout
from ranker_commander_train import build_batch, load_optimizer, save_checkpoint


class LeagueReturnSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_legacy_and_non_ppo_metadata_keep_default_discounts(self):
        expected = {"gamma": .997, "gae_lambda": .95}
        for metadata in ({}, {"mode": "ppo"}, {"mode": "bc", "training_config": {
                "gamma": .8, "gae_lambda": 0}}):
            with self.subTest(metadata=metadata):
                self.assertEqual(league._return_settings(metadata), expected)

    def test_invalid_saved_discounts_are_rejected(self):
        for name, value in (("gamma", 0), ("gamma", float("nan")),
                            ("gae_lambda", -1), ("gae_lambda", float("inf"))):
            with self.subTest(name=name, value=value), self.assertRaisesRegex(ValueError, name):
                league._return_settings({"mode": "ppo", "training_config": {name: value}})

    @staticmethod
    def collect_fixture(install, weights, directory, jobs, **kwargs):
        policy = load_weights(weights)
        reports = []
        for index, job in enumerate(jobs):
            records = np.zeros(4, dtype=RECORD_DTYPE)
            records["frame"] = [0, 32, 96, 128]
            records["status"][-1] = WIN
            records["terminal_reward"][-1] = 1.2
            with torch.no_grad():
                sampled = policy.sample(torch.zeros(3, policy.vector_size),
                    torch.zeros(3, *MAP_SHAPE), torch.ones(3, 95, dtype=torch.bool),
                    torch.zeros(3, 32))
            for field in ("action", "mask", "logp"):
                records[field][:-1] = sampled[field].numpy()
            records["value"][:-1] = sampled["value"].reshape(-1).numpy()
            rollout = write_rollout(Path(directory) / f"game_{index}.rlo", records,
                seed=job["seed"], weight_version=policy.weight_version)
            reports.append({**job, "valid": True, "win": True, "rollout": str(rollout)})
        return reports

    def test_discounts_survive_resume_champion_and_exploiter_reset(self):
        def check_checkpoint(path, expected, *, optimizer=False):
            metadata = json.loads(path.with_suffix(".bin.json").read_text())
            actual = {name: metadata["training_config"][name] for name in expected}
            self.assertEqual(actual, expected)
            if optimizer:
                policy = load_weights(path)
                restored = load_optimizer(torch.optim.Adam(policy.parameters()),
                    path.with_suffix(".bin.optimizer.npz"), version=policy.weight_version)
                self.assertTrue(restored.state_dict()["state"])

        observed = []

        def capture_batch(episodes, config):
            observed.append({"gamma": config.gamma, "gae_lambda": config.gae_lambda})
            return build_batch(episodes, config)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            initial = root / "initial.bin"
            original = {"gamma": .99, "gae_lambda": .98}
            replacement = {"gamma": .97, "gae_lambda": .7}
            save_checkpoint(CommanderPolicy(), initial, version=1, metadata={"mode": "ppo",
                "iteration": 149, "training_config": original})
            state = {"policy": str(initial), "updates": 150}
            args = SimpleNamespace(work_dir=root, state=root / "state.json", install_dir=root,
                seed=1, games=1, workers=1, exe=None, keep_sleep=False, keep_rollouts=True,
                max_updates=1, evaluate_every=1, exploiter_every=0, exploiter_reset=20,
                target_replacements=10)
            catalog = {(a, b): list(range(1 + index * 5, 6 + index * 5))
                for index, (a, b) in enumerate((a, b) for a in range(4) for b in range(4) if a != b)}
            mapping = {pair: seeds[0] for pair, seeds in catalog.items()}
            # Collect synthetic rollouts and force the promotion transition;
            # this checks training persistence, never a gameplay result.
            with patch.object(league, "run_games", side_effect=self.collect_fixture), \
                    patch.object(league, "collect_evaluation", return_value=[]), \
                    patch.object(league, "champion_gate", return_value={"promote": True}), \
                    patch.object(league, "build_batch", side_effect=capture_batch), \
                    redirect_stdout(io.StringIO()):
                for _ in range(2):
                    league.run_league(args, state, mapping, catalog)
                    check_checkpoint(Path(state["policy"]), original, optimizer=True)
                    check_checkpoint(Path(state["champion"]), original)
                league.train_exploiter(args, state, root / "exploiter_first")
                check_checkpoint(Path(state["exploiter"]), original, optimizer=True)
                check_checkpoint(root / "pool" / "exploiter_00002.bin", original)

                # A continuing exploiter inherits its own settings even if the
                # champion changes; its next reset inherits the new champion.
                champion = Path(state["champion"])
                policy = load_weights(champion)
                save_checkpoint(policy, champion, version=policy.weight_version,
                    metadata={"mode": "ppo", "training_config": replacement})
                state["league_updates"] = 3
                league.train_exploiter(args, state, root / "exploiter_resume")
                check_checkpoint(Path(state["exploiter"]), original, optimizer=True)
                state["league_updates"] = 20
                league.train_exploiter(args, state, root / "exploiter_reset")
                check_checkpoint(Path(state["exploiter"]), replacement, optimizer=True)
                check_checkpoint(root / "pool" / "exploiter_00020.bin", replacement)
            self.assertEqual(observed, [original] * 4 + [replacement])


if __name__ == "__main__":
    unittest.main(verbosity=2)
