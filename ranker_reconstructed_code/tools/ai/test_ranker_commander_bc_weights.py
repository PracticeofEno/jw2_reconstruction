"""Targeted tests for episode weights in the BC actor loss."""
from copy import deepcopy
import math
from pathlib import Path
import unittest

import numpy as np
import torch

torch.set_num_threads(1)
import ranker_commander_train as train
from ranker_commander_model import CommanderPolicy, VECTOR_SIZE
from ranker_commander_rollout import Episode, RECORD_DTYPE, HEAD_OFFSETS, WIN


def episode(seed, count=3):
    rows = np.zeros(count + 1, dtype=RECORD_DTYPE)
    rows['frame'] = np.arange(1, count + 2) * 32
    rows['teacher'] = 1
    rows['status'][-1] = WIN
    rows['terminal_reward'][-1] = 1.2
    for offset in HEAD_OFFSETS:
        rows['mask'][:-1, offset] = 1
    rows['mask'][:-1, 1] = 1
    rows['action'][:-1, 0] = seed % 2
    rows['vector'][:, 8] = seed / 10
    return Episode(Path(f'episode_{seed}.rlo'), 1, seed, 0, rows)


class ToyPolicy(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.logits = torch.nn.Parameter(torch.tensor([.2, -.1]))

    def evaluate(self, vector, maps, actions, masks, privileged):
        logp = self.logits.log_softmax(0)[actions[:, 0]].unsqueeze(1)
        return {'logp': logp, 'value': torch.zeros(len(actions), 1)}


class EpisodeWeightingTests(unittest.TestCase):
    def test_lazy_weights_follow_episode_indices_after_shuffle(self):
        episodes = [episode(2, 2), episode(3, 3)]
        batch, _ = train.build_batch(episodes, train.TrainConfig(mode='bc'),
                                     bc_episode_weights=[2, 1])
        selected = batch.select(torch.tensor([4, 0, 2, 1, 3]))
        self.assertEqual(selected['bc_weight'].tolist(), [1, 2, 1, 2, 1])
        self.assertEqual(selected['actions'][:, 0].tolist(), [1, 0, 1, 0, 1])
        self.assertIn('bc_weight', dict(batch.items()))

    def test_unit_weights_preserve_default_parameters_and_adam(self):
        episodes = [episode(2), episode(3)]
        config = train.TrainConfig(mode='bc', epochs=1, minibatch=6,
                                   bc_rare_weight=8, bc_class_power=.5)
        plain, _ = train.build_batch(episodes, deepcopy(config))
        explicit, _ = train.build_batch(episodes, deepcopy(config), bc_episode_weights=[1, 1])
        torch.manual_seed(7)
        first = CommanderPolicy()
        second = deepcopy(first)
        oa, ma = train.train_update(first, plain, deepcopy(config))
        ob, mb = train.train_update(second, explicit, deepcopy(config))
        self.assertEqual(ma, mb)
        for name, value in first.state_dict().items():
            self.assertTrue(torch.equal(value, second.state_dict()[name]), name)
        for key, state in oa.state_dict()['state'].items():
            for name, value in state.items():
                self.assertTrue(torch.equal(value, ob.state_dict()['state'][key][name]))

    def test_episode_weight_composes_with_existing_rare_and_class_factors(self):
        policy = ToyPolicy()
        before = policy.logits.detach().clone()
        batch = {'vector': torch.zeros(3, VECTOR_SIZE), 'maps': torch.zeros(3, 9, 16, 16),
                 'masks': torch.ones(3, 95, dtype=torch.bool),
                 'actions': torch.zeros(3, 8, dtype=torch.long),
                 'privileged': torch.zeros(3, 16), 'old_logp': torch.zeros(3),
                 'advantage': torch.zeros(3), 'target': torch.tensor([1., 2., 3.]),
                 'bc_weight': torch.tensor([2., 2., 1.])}
        batch['actions'][-1, 0] = 1
        config = train.TrainConfig(mode='bc', epochs=1, minibatch=3, bc_rare_weight=1,
                                   bc_class_power=.5, learning_rate_initial=.01, learning_rate_final=.01)
        optimizer = torch.optim.SGD(policy.parameters(), lr=.01)
        _, metrics = train.train_update(policy, batch, config, optimizer=optimizer)
        # Two NOOP examples have weight 2 each. The non-NOOP has weight 2*sqrt(2).
        target = torch.tensor([2 / (2 + math.sqrt(2)), math.sqrt(2) / (2 + math.sqrt(2))])
        expected_loss = -(target * before.log_softmax(0)).sum().item()
        self.assertAlmostEqual(metrics['actor_loss'], expected_loss, places=6)
        expected = before - .01 * (before.softmax(0) - target)
        torch.testing.assert_close(policy.logits, expected, rtol=1e-6, atol=1e-7)
        self.assertAlmostEqual(metrics['value_loss'], 7 / 3, places=6)

    def test_invalid_episode_weights_and_ppo_use_are_rejected(self):
        episodes = [episode(2), episode(3)]
        for weights in ([1], [1, 0], [1, -1], [1, math.nan], [1, math.inf], [[1], [1]]):
            with self.subTest(weights=weights), self.assertRaises(ValueError):
                train.build_batch(episodes, train.TrainConfig(mode='bc'), bc_episode_weights=weights)
        with self.assertRaises(ValueError):
            train.build_batch(episodes, train.TrainConfig(mode='ppo'), bc_episode_weights=[1, 1])


if __name__ == '__main__':
    unittest.main(verbosity=2)
