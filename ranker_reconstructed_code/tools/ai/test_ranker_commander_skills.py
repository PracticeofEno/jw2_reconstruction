"""Skill contract migration must preserve every learned race and Adam slot."""
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch

import ranker_commander_model as model
import ranker_commander_rollout as rollout
from ranker_commander_upgrade import upgrade_optimizer


class CommanderSkillsTests(unittest.TestCase):
    def setUp(self):
        torch.set_num_threads(1)
        torch.manual_seed(931)

    def test_642_migration_keeps_learned_race_rows_and_function(self):
        old = model.CommanderPolicy(7, vector_size=642).eval()
        new = model.upgrade_policy(old).eval()
        vector = torch.randn(8, model.VECTOR_SIZE)
        maps = torch.rand(8, *model.MAP_SHAPE)
        old_mask = torch.ones(8, 117, dtype=torch.bool)
        mask = torch.cat((old_mask[:, :64], torch.zeros(8, 32, dtype=torch.bool), old_mask[:, 64:]), 1)
        with torch.no_grad():
            original = old.sample(vector[:, :642], maps, old_mask, deterministic=True)
            upgraded = new.sample(vector, maps, mask, deterministic=True)
        for key in ("action", "logp", "value"):
            torch.testing.assert_close(upgraded[key], original[key], rtol=0, atol=0)
        for name in ("heads.0.weight", "heads.0.bias", "embeddings.0.weight"):
            torch.testing.assert_close(new.state_dict()[name][:64], old.state_dict()[name], rtol=0, atol=0)
        self.assertEqual(torch.count_nonzero(new.vector1.weight[:, 642:]).item(), 0)
        with tempfile.TemporaryDirectory() as directory:
            legacy, current = Path(directory) / "race.bin", Path(directory) / "skills.bin"
            model.export_weights(old, legacy, allow_legacy=True)
            with self.assertRaises(ValueError): model.load_weights(legacy)
            self.assertEqual(model.load_weights(legacy, allow_legacy=True).head_sizes[0], 64)
            model.export_weights(new, current)
            self.assertLess(current.stat().st_size, model.MAX_WEIGHT_BYTES)
            self.assertEqual(model.load_weights(current).vector_size, 1410)

    def test_race_adam_moments_are_copied_and_new_columns_start_zero(self):
        import json
        old = model.CommanderPolicy(3, vector_size=642)
        optimizer = torch.optim.Adam(old.parameters(), lr=2.5e-5)
        sum(p.square().sum() for p in old.parameters()).backward()
        optimizer.step()
        state = optimizer.state_dict()
        arrays = {"schema_crc": np.array(old.schema_crc, dtype=np.uint32), "weight_version": np.array(3, dtype=np.uint32)}
        for index, slots in state["state"].items():
            for key, value in slots.items(): arrays[f"p{index}_{key}"] = value.numpy()
        arrays["metadata"] = np.frombuffer(json.dumps({"param_groups": state["param_groups"],
            "state": {str(i): list(slots) for i, slots in state["state"].items()}}).encode(), dtype=np.uint8)
        new = model.upgrade_policy(old)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "adam.npz"; np.savez(path, **arrays)
            restored, _ = upgrade_optimizer(old, new, path)
        for (name, source), (_, target) in zip(old.named_parameters(), new.named_parameters()):
            for key in ("exp_avg", "exp_avg_sq"):
                value = restored.state[target][key]
                prefix = value[:, :642] if name == "vector1.weight" else value[:64] if name in (
                    "heads.0.weight", "heads.0.bias", "embeddings.0.weight") else value
                torch.testing.assert_close(prefix, optimizer.state[source][key], atol=0, rtol=0)
        self.assertEqual(torch.count_nonzero(restored.state[new.vector1.weight]["exp_avg"][:, 642:]).item(), 0)

    def test_new_skill_actions_have_trainable_probabilities(self):
        policy = model.upgrade_policy(model.CommanderPolicy(vector_size=642))
        vector = torch.randn(4, model.VECTOR_SIZE)
        mask = torch.zeros(4, model.LOGIT_COUNT, dtype=torch.bool)
        for off in model.HEAD_OFFSETS: mask[:, off] = True
        mask[:, 64:96] = True
        actions = torch.zeros(4, 8, dtype=torch.long); actions[:, 0] = torch.tensor([64, 65, 66, 95])
        result = policy.evaluate(vector, torch.zeros(4, *model.MAP_SHAPE), actions, mask)
        (-result["logp"][:, 0].sum()).backward()
        self.assertGreater(policy.heads[0].weight.grad[64:].abs().sum().item(), 0)
        self.assertEqual((model.SCHEMA_CRC, model.VECTOR_SIZE, model.HEAD_SIZES),
                         (rollout.SCHEMA_CRC, rollout.VECTOR_SIZE, rollout.HEAD_SIZES))


if __name__ == "__main__": unittest.main()
