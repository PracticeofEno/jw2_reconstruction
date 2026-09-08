"""Focused elapsed-frame return configuration and shaping diagnostics checks."""
from pathlib import Path
import unittest

import numpy as np

from ranker_commander_rollout import Episode, RECORD_DTYPE, WIN, episode_returns
from ranker_commander_train import TrainConfig, build_batch


def episode_fixture():
    records = np.zeros(4, dtype=RECORD_DTYPE)
    records["frame"] = [0, 32, 96, 128]
    records["value"] = [1, 2, 4, 9]
    records["status"][-1] = WIN
    records["terminal_reward"][-1] = 1.2
    return Episode(Path("synthetic.rlo"), 1, 1, 0, records)


class ReturnConfigurationTests(unittest.TestCase):
    def test_invalid_return_settings_are_rejected(self):
        for name, values in (("gamma", [0, -0.1, 1.01, float("nan"), float("inf")]),
                             ("gae_lambda", [-0.1, 1.01, float("nan"), float("inf")])):
            for value in values:
                with self.subTest(name=name, value=value), self.assertRaisesRegex(ValueError, name):
                    TrainConfig(**{name: value})
        self.assertEqual(TrainConfig(gamma=1, gae_lambda=0).gae_lambda, 0)
        self.assertEqual(TrainConfig(gamma=1, gae_lambda=1).gamma, 1)

    def test_build_batch_uses_elapsed_frame_gamma_and_lambda(self):
        batch, _ = build_batch([episode_fixture()], TrainConfig(gamma=.9, gae_lambda=.5))
        # dt = [1, 2, 1]; TD = [.8, 1.24, -2.8]. The two-frame
        # transition discounts the GAE carry by .9**2 * .5**2.
        expected_advantage = np.array([1.10285, .673, -2.8])
        expected_return = expected_advantage + np.array([1, 2, 4])
        np.testing.assert_allclose(batch["target"].numpy(), expected_return, atol=2e-7)
        np.testing.assert_allclose(batch["advantage"].numpy(),
            (expected_advantage - expected_advantage.mean()) / expected_advantage.std(), atol=2e-7)

    def test_bc_target_remains_monte_carlo(self):
        episode = episode_fixture()
        batch, _ = build_batch([episode], TrainConfig(mode="bc", gamma=.9, gae_lambda=0))
        np.testing.assert_allclose(batch["target"].numpy(), [.8748, .972, 1.2], atol=2e-7)

    def test_step_shaping_exposes_cancellation_without_halving(self):
        episode = episode_fixture()
        episode.records["potential"][:, 0] = [0, .4, .2, 0]
        config = TrainConfig(gamma=1)
        _, metrics = build_batch([episode], config)
        self.assertAlmostEqual(metrics["discounted_abs_shape_mean"], 0)
        self.assertAlmostEqual(metrics["discounted_abs_step_shape_mean"], .8)
        self.assertFalse(metrics["shaping_halved"])
        self.assertEqual(config.shaping_scale, 1)

    def test_halving_recomputes_with_configured_discounts(self):
        episode = episode_fixture()
        episode.records["potential"][:, 0] = [10, 8, 4, 0]
        config = TrainConfig(gamma=.8, gae_lambda=.5)
        batch, metrics = build_batch([episode], config)
        self.assertTrue(metrics["shaping_halved"])
        self.assertEqual(config.shaping_scale, .5)
        expected = episode_returns(episode, gamma=.8, gae_lambda=.5, shaping_scale=.5)
        np.testing.assert_allclose(batch["target"].numpy(), expected["return"])
        # The guard keeps its original pre-halving scale while the new step
        # magnitude reports the final rewards actually used for training.
        self.assertAlmostEqual(metrics["discounted_abs_shape_mean"], 10, places=5)
        self.assertAlmostEqual(metrics["discounted_abs_step_shape_mean"], 5, places=5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
