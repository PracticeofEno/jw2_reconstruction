"""Economic policy preservation through real PPO and checkpoint continuation."""
from pathlib import Path
import tempfile
import unittest

import torch

from ranker_commander_combat_ppo import freeze_economy, PROTECTED_HEADS
from ranker_commander_model import CommanderPolicy, HEAD_OFFSETS, HEAD_SIZES, MAP_SHAPE, VECTOR_SIZE, load_weights
from ranker_commander_train import TrainConfig, train_update, save_checkpoint, load_optimizer


class CombatPpoTests(unittest.TestCase):
    def test_frozen_economy_survives_adam_momentum_and_checkpoint_resume(self):
        torch.set_num_threads(2)
        torch.manual_seed(714)
        policy = CommanderPolicy(weight_version=104)
        optimizer = torch.optim.Adam(policy.parameters(), lr=1e-4)
        # Give every parameter nonzero momentum AND stale gradients, as can
        # happen when switching from full-policy training into combat PPO.
        sum((p + .01).square().sum() for p in policy.parameters()).backward()
        optimizer.step()
        reference = CommanderPolicy()
        reference.load_state_dict(policy.state_dict())
        scope = freeze_economy(policy)
        self.assertTrue(all(p.grad is None for n, p in policy.named_parameters() if n in scope['frozen']))
        vector = torch.rand(12, VECTOR_SIZE)
        maps = torch.rand(12, *MAP_SHAPE)
        masks = torch.ones(12, sum(HEAD_SIZES), dtype=torch.bool)
        actions = torch.stack([torch.randint(width, (12,)) for width in HEAD_SIZES], dim=1)
        privileged = torch.rand(12, 32)
        with torch.no_grad():
            before_output = policy.evaluate(vector, maps, actions, masks, privileged)
        batch = dict(vector=vector, maps=maps, actions=actions, masks=masks, privileged=privileged,
                     old_logp=before_output['logp'].sum(1), target=before_output['value'] + .8,
                     advantage=torch.linspace(-1., 1., 12))
        before = {n: p.detach().clone() for n, p in policy.named_parameters()}
        states = {n: {k: v.clone() for k, v in optimizer.state[p].items()}
                  for n, p in policy.named_parameters() if n in scope['frozen']}
        self.assertTrue(all(s['exp_avg'].abs().sum() > 0 for s in states.values()))
        config = TrainConfig(mode='ppo', iteration=1, critic_warmup=1, epochs=2,
                             minibatch=6, learning_rate_initial=1e-4, learning_rate_final=1e-4)
        with tempfile.TemporaryDirectory() as directory:
            for iteration in range(2):
                optimizer, report = train_update(policy, batch, config, optimizer=optimizer, teacher_policy=reference)
                self.assertFalse(report['critic_only'])
                for name, parameter in policy.named_parameters():
                    if name in scope['frozen']:
                        self.assertTrue(torch.equal(before[name], parameter), name)
                        for key, value in states[name].items():
                            self.assertTrue(torch.equal(value, optimizer.state[parameter][key]), (name, key))
                with torch.no_grad():
                    after_output = policy.evaluate(vector, maps, actions, masks, privileged)
                for head in PROTECTED_HEADS:
                    section = slice(HEAD_OFFSETS[head], HEAD_OFFSETS[head] + HEAD_SIZES[head])
                    self.assertTrue(torch.equal(before_output['logits'][:, section], after_output['logits'][:, section]))
                    self.assertTrue(torch.equal(before_output['logp'][:, head], after_output['logp'][:, head]))
                self.assertFalse(torch.equal(before['heads.3.weight'], policy.heads[3].weight))
                self.assertFalse(torch.equal(before['value2.weight'], policy.value2.weight))
                path = Path(directory) / f'policy{iteration}.bin'
                save_checkpoint(policy, path, version=106 + iteration, metadata={'mode': 'ppo'}, optimizer=optimizer)
                policy = load_weights(path)
                scope = freeze_economy(policy)
                optimizer = torch.optim.Adam(policy.parameters())
                load_optimizer(optimizer, str(path) + '.optimizer.npz', version=106 + iteration)
                with torch.no_grad():
                    batch['old_logp'] = policy.evaluate(vector, maps, actions, masks, privileged)['logp'].sum(1)


if __name__ == '__main__':
    unittest.main()
