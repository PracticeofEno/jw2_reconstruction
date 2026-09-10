"""Focused residual/observation migration and numeric Adam tests, no gameplay."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

import numpy as np
import torch

import ranker_commander_model as model
import ranker_commander_upgrade as upgrade


class CommanderAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        cls.temp = tempfile.TemporaryDirectory(prefix="commander_adapter_")
        cls.directory = Path(cls.temp.name)
        cls.probe = None
        compiler = shutil.which("g++")
        if compiler:
            root = Path(__file__).resolve().parents[2]
            cls.probe = cls.directory / ("probe.exe" if os.name == "nt" else "probe")
            subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-static", "-I", str(root / "include"),
                            str(root / "tests/ai_commander_model_probe.cpp"),
                            str(root / "src/ranker_ai_commander_model.cpp"), "-o", str(cls.probe)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fixture(self):
        with torch.random.fork_rng(devices=[]):
            torch.manual_seed(762)
            source = model.CommanderPolicy(9, vector_size=542).eval()
            upgraded = model.upgrade_policy(source, adapter_seed=951)
            vector = torch.randn(5, model.VECTOR_SIZE)
            maps = torch.randint(0, 256, (5, 12, 16, 16)).float() / 255
            private = torch.randn(5, 32)
            masks = torch.ones(5, model.LOGIT_COUNT, dtype=torch.bool)
            masks[1, :model.HEAD_SIZES[0]] = False
            masks[1, 14] = True
            masks[1, model.HEAD_OFFSETS[2]:model.HEAD_OFFSETS[3]] = False
            masks[1, model.HEAD_OFFSETS[2]+1] = True
            masks[2, model.HEAD_OFFSETS[2]:model.HEAD_OFFSETS[3]] = False
            masks[2, model.HEAD_OFFSETS[2]] = True
        masks[:, 42:model.HEAD_SIZES[0]] = False
        return source, upgraded, vector, maps, private, masks

    def native(self, policy, vector, maps, private, masks, *, legacy=False):
        if self.probe is None:
            self.skipTest("g++ unavailable for native adapter parity")
        path = self.directory / ("legacy.bin" if legacy else "new.bin")
        model.export_weights(policy, path, allow_legacy=legacy)
        input_path, output_path = self.directory / "input.bin", self.directory / "output.bin"
        with input_path.open("wb") as stream:
            stream.write(struct.pack("<I", len(vector)))
            for row in range(len(vector)):
                for array in (vector[row], maps[row], private[row]):
                    stream.write(array.numpy().astype("<f4").tobytes())
                stream.write(masks[row].numpy().astype("u1").tobytes())
        command = [str(self.probe), str(path), str(input_path), str(output_path)]
        if legacy:
            command.append("--allow-legacy")
        subprocess.run(command, check=True, capture_output=True)
        dtype = np.dtype([("action", "u1", 8), ("mask", "u1", model.LOGIT_COUNT), ("logp", "<f4", 8),
                          ("logits", "<f4", model.LOGIT_COUNT), ("value", "<f4")])
        return np.fromfile(output_path, dtype=dtype)

    def test_zero_extension_preserves_exact_python_and_native_outputs(self):
        source, upgraded, vector, maps, private, masks = self.fixture()
        with torch.no_grad():
            old = source.sample(vector[:, :542].contiguous(), maps[:, :9].contiguous(), torch.cat((masks[:, :42], masks[:, model.HEAD_SIZES[0]:]), 1), private, deterministic=True)
            new = upgraded.sample(vector, maps, masks, private, deterministic=True)
        for name in old:
            actual = torch.cat((new[name][:, :42], new[name][:, model.HEAD_SIZES[0]:]), 1) if name in ("mask", "logits") else new[name]
            torch.testing.assert_close(actual, old[name], rtol=0, atol=5e-7 if name == "entropy" else 0, msg=name)
        original = self.native(source, vector, maps, private, masks, legacy=True)
        extended = self.native(upgraded, vector, maps, private, masks)
        for name in original.dtype.names:
            np.testing.assert_array_equal(original[name], extended[name], err_msg=name)
        self.assertEqual(len(list(upgraded.parameters())), 67)
        self.assertEqual(list(source.state_dict()), list(upgraded.state_dict())[:39])
        self.assertEqual(int(torch.count_nonzero(upgraded.vector1.weight[:, 542:])), 0)
        self.assertEqual(int(torch.count_nonzero(upgraded.conv1.weight[:, 9:])), 0)

    def test_nonlinear_residual_native_parity_and_privileged_separation(self):
        _, policy, vector, maps, private, masks = self.fixture()
        with torch.no_grad():
            for adapter in policy.head_adapters.values():
                adapter.up.weight.copy_(torch.linspace(-.08, .09, adapter.up.weight.numel()).reshape_as(adapter.up.weight))
                adapter.up.bias.fill_(.01)
            policy.vector1.weight[:, 542:].fill_(.003)
            policy.conv1.weight[:, 9:].fill_(.002)
            expected = policy.sample(vector, maps, masks, private, deterministic=True)
            changed = policy.sample(vector, maps, masks, private + 2, deterministic=True)
        torch.testing.assert_close(changed["logits"], expected["logits"], rtol=0, atol=0)
        actual = self.native(policy, vector, maps, private, masks)
        for name in ("action", "mask"):
            np.testing.assert_array_equal(actual[name], expected[name].numpy())
        for name in ("logits", "logp", "value"):
            np.testing.assert_allclose(actual[name], expected[name].numpy(), rtol=0, atol=1e-4)

    def test_zero_up_has_live_output_gradient_then_down_gradient_and_interaction(self):
        adapter = model.CommanderHeadAdapter(280, 8)
        x = torch.ones(4, 280)
        adapter(x).sum().backward()
        self.assertGreater(float(adapter.up.weight.grad.abs().sum()), 0)
        self.assertEqual(float(adapter.down.weight.grad.abs().sum()), 0)
        with torch.no_grad():
            adapter.down.weight.zero_()
            adapter.down.bias.fill_(-.5)
            adapter.down.weight[0, 0] = 1
            adapter.down.weight[0, 256] = 1
            adapter.up.weight.zero_()
            adapter.up.weight[0, 0] = 1
        x = torch.zeros(4, 280)
        x[1, 0] = x[3, 0] = 1
        x[2, 256] = x[3, 256] = 1
        out = adapter(x)[:, 0]
        self.assertNotEqual(float((out[3] - out[1] - out[2] + out[0]).detach()), 0)
        adapter.zero_grad(set_to_none=True)
        out.sum().backward()
        self.assertGreater(float(adapter.down.weight.grad.abs().sum()), 0)

    def test_explicit_legacy_io_and_upgrade_rng_preservation(self):
        source, policy, *_ = self.fixture()
        path = self.directory / "strict_legacy.bin"
        with self.assertRaises(ValueError):
            model.export_weights(source, path)
        model.export_context_weights(source, path)
        with self.assertRaises(ValueError):
            model.load_weights(path)
        torch.manual_seed(45)
        initial_rng = torch.get_rng_state().clone()
        restored = model.load_weights(path, allow_legacy=True)
        upgraded = model.upgrade_policy(restored)
        torch.testing.assert_close(torch.get_rng_state(), initial_rng, rtol=0, atol=0)
        self.assertEqual(upgraded.vector_size, model.VECTOR_SIZE)
        if self.probe:
            self.assertEqual(subprocess.run([str(self.probe), str(path)], capture_output=True).returncode, 2)
        legacy = model.CommanderPolicy(3, vector_size=528)
        context = model.upgrade_legacy_policy(legacy)
        self.assertEqual(context.vector_size, 542)
        self.assertEqual(context.conv1.in_channels, 9)
        latest = model.upgrade_policy(legacy)
        self.assertEqual(latest.vector_size, model.VECTOR_SIZE)
        torch.testing.assert_close(latest.vector1.weight[:, :528], legacy.vector1.weight, rtol=0, atol=0)

    def test_numeric_adam_migration_preserves_old_slots_and_rejects_bad_layout(self):
        source, policy, *_ = self.fixture()
        old = torch.optim.Adam(source.parameters(), lr=.0001)
        for index, parameter in enumerate(source.parameters()):
            old.state[parameter] = {"step": torch.tensor(float(240 + index)),
                                    "exp_avg": torch.full_like(parameter, .001 * (index + 1)),
                                    "exp_avg_sq": torch.full_like(parameter, .002 * (index + 1))}
        path = self.directory / "source.optimizer.npz"
        self.write_legacy_optimizer(old, path, source)
        upgraded, proof = upgrade.upgrade_optimizer(source, policy, path)
        self.assertEqual(len(proof["source_steps"]), 39)
        for (name, a), (_, b) in zip(source.named_parameters(), policy.named_parameters()):
            for slot, original in old.state[a].items():
                actual = upgraded.state[b][slot]
                if slot != "step" and name == "vector1.weight":
                    self.assertEqual(int(torch.count_nonzero(actual[:, 542:])), 0)
                    actual = actual[:, :542]
                elif slot != "step" and name == "conv1.weight":
                    self.assertEqual(int(torch.count_nonzero(actual[:, 9:])), 0)
                    actual = actual[:, :9]
                elif slot != "step" and name in ("heads.0.weight", "heads.0.bias", "embeddings.0.weight"):
                    self.assertEqual(int(torch.count_nonzero(actual[42:])), 0)
                    actual = actual[:42]
                torch.testing.assert_close(actual, original, rtol=0, atol=0)
        for parameter in list(policy.parameters())[39:]:
            for value in upgraded.state[parameter].values():
                self.assertEqual(int(torch.count_nonzero(value)), 0)
        with np.load(path, allow_pickle=False) as data:
            arrays = {key: data[key].copy() for key in data.files}
        metadata = json.loads(arrays["metadata"].tobytes())
        metadata["param_groups"][0]["params"] = list(reversed(metadata["param_groups"][0]["params"]))
        arrays["metadata"] = np.frombuffer(json.dumps(metadata).encode(), dtype=np.uint8)
        np.savez(path, **arrays)
        with self.assertRaises(ValueError):
            upgrade.upgrade_optimizer(source, policy, path)

    @staticmethod
    def write_legacy_optimizer(optimizer, path, source):
        state = optimizer.state_dict()
        arrays = {"schema_crc": np.array(source.schema_crc, dtype=np.uint32),
                  "weight_version": np.array(source.weight_version, dtype=np.uint32)}
        for index, slots in state["state"].items():
            for name, value in slots.items():
                arrays[f"p{index}_{name}"] = value.numpy()
        arrays["metadata"] = np.frombuffer(json.dumps({"param_groups": state["param_groups"],
            "state": {str(index): list(slots) for index, slots in state["state"].items()}}).encode(), dtype=np.uint8)
        np.savez(path, **arrays)

    def test_migration_publishes_unapproved_new_files_and_refuses_overwrite(self):
        source, *_ = self.fixture()
        original = self.directory / "migration_source.bin"
        output = self.directory / "migration_result.bin"
        model.export_context_weights(source, original)
        before = hashlib.sha256(original.read_bytes()).hexdigest()
        record = upgrade.migrate(original, output)
        self.assertFalse(record["source_admission_migrated"])
        self.assertFalse(record["bc_gate"]["passed"])
        self.assertEqual(model.load_weights(output).vector_size, model.VECTOR_SIZE)
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), before)
        with self.assertRaises(FileExistsError):
            upgrade.migrate(original, output)


if __name__ == "__main__":
    unittest.main()
