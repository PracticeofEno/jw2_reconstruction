"""Tests only the commander model: run python -m unittest discover -s tools/ai
-p test_ranker_commander_model.py from ranker_reconstructed_code.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

import numpy as np
import torch

import ranker_commander_model as model


class CommanderModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        torch.manual_seed(941)
        cls.policy = model.CommanderPolicy(17).eval()
        cls.temp = tempfile.TemporaryDirectory(prefix="ranker_commander_model_")
        cls.directory = Path(cls.temp.name)
        cls.weights = cls.directory / "weights.bin"
        model.export_weights(cls.policy, cls.weights)
        compiler = shutil.which("g++")
        cls.probe = None
        if compiler:
            root = Path(__file__).resolve().parents[2]
            cls.probe = cls.directory / ("probe.exe" if os.name == "nt" else "probe")
            # Static runtime: the probe must not pick up a mismatched
            # libstdc++/libgcc DLL from another MinGW install on PATH.
            subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-static", "-I", str(root / "include"),
                            str(root / "tests/ai_commander_model_probe.cpp"),
                            str(root / "src/ranker_ai_commander_model.cpp"), "-o", str(cls.probe)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fixture(self, count=4):
        generator = torch.Generator().manual_seed(9301)
        vectors = torch.rand((count, model.VECTOR_SIZE), generator=generator) * 2 - 1
        # Exactly the deployment's byte quantization, with nonzero spatial patterns.
        maps = torch.randint(0, 256, (count, *model.MAP_SHAPE), generator=generator).float() / 255
        privileged = torch.rand((count, 32), generator=generator)
        masks = torch.ones((count, 95), dtype=torch.bool)
        if count > 1:
            masks[1, :42] = False
            masks[1, 14] = True  # tower build exercises the H1b autoregressive head
            masks[1, 58:62] = False
            masks[1, 59] = True  # MAIN leaves intent, anchor and ROE relevant
        if count > 2:
            masks[2, 58:62] = False
            masks[2, 58] = True  # irrelevant intent/anchor/ROE become singleton zero
        if count > 3:
            masks[3, ::3] = False
        return vectors, maps, privileged, masks

    def run_probe(self, vectors, maps, private, masks):
        if self.probe is None:
            self.skipTest("g++ unavailable for standalone C++ parity probe")
        input_path = self.directory / "input.bin"
        output_path = self.directory / "output.bin"
        with input_path.open("wb") as output:
            output.write(struct.pack("<I", len(vectors)))
            for vector, grid, privileged, mask in zip(vectors, maps, private, masks):
                for values in (vector, grid, privileged):
                    output.write(values.numpy().astype("<f4").tobytes())
                output.write(mask.numpy().astype("u1").tobytes())
        subprocess.run([str(self.probe), str(self.weights), str(input_path), str(output_path)], check=True, capture_output=True)
        dtype = np.dtype([("action", "u1", 8), ("mask", "u1", 95), ("logp", "<f4", 8),
                          ("logits", "<f4", 95), ("value", "<f4")])
        return np.fromfile(output_path, dtype=dtype)

    def test_cpp_python_nonzero_convolution_and_prefix_parity(self):
        vectors, maps, private, masks = self.fixture()
        with torch.no_grad():
            expected = self.policy.sample(vectors, maps, masks, private, deterministic=True)
            zero_map = self.policy.sample(vectors, maps * 0, masks, private, deterministic=True)
        self.assertGreater(float((expected["logits"] - zero_map["logits"]).abs().max()), 1e-5)
        actual = self.run_probe(vectors, maps, private, masks)
        np.testing.assert_array_equal(actual["action"], expected["action"].numpy())
        np.testing.assert_array_equal(actual["mask"], expected["mask"].numpy())
        for name in ("logits", "logp", "value"):
            np.testing.assert_allclose(actual[name], expected[name].numpy(), rtol=0, atol=1e-4)
        print("commander C++/Python maximum logit error:", np.abs(actual["logits"] - expected["logits"].numpy()).max())

    def test_privileged_vector_never_changes_actor(self):
        vectors, maps, private, masks = self.fixture(2)
        vectors[1] = vectors[0]
        maps[1] = maps[0]
        masks[1] = masks[0]
        private[1] = private[0] + 4
        with torch.no_grad():
            result = self.policy.sample(vectors, maps, masks, private, deterministic=True)
            changed = self.policy.sample(vectors, maps, masks, private + 4, deterministic=True)
        # Compare identical batch positions: GEMM kernels may reduce duplicate
        # rows in different orders, independently of the privileged features.
        torch.testing.assert_close(result["logits"], changed["logits"], rtol=0, atol=0)
        self.assertGreater(float((result["value"] - changed["value"]).abs().max()), .001)
        actual = self.run_probe(vectors, maps, private, masks)
        np.testing.assert_array_equal(actual["logits"][0], actual["logits"][1])
        self.assertNotEqual(actual["value"][0], actual["value"][1])

    def test_recorded_masks_singletons_and_gradient(self):
        policy = model.load_weights(self.weights)
        vectors, maps, private, masks = self.fixture(3)
        with torch.no_grad():
            sampled = policy.sample(vectors, maps, masks, private, deterministic=True)
        self.assertTrue(torch.equal(sampled["action"][2, 3:6], torch.zeros(3, dtype=torch.long)))
        self.assertTrue(torch.equal(sampled["logp"][2, 3:6], torch.zeros(3)))
        result = policy.evaluate(vectors, maps, sampled["action"], sampled["mask"], private)
        torch.testing.assert_close(result["logp"], sampled["logp"], atol=0, rtol=0)
        loss = -result["logp"].sum() - .01 * result["entropy"].sum() + result["value"].square().sum()
        loss.backward()
        self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all() for p in policy.parameters()))
        # Prove future heads actually depend on embeddings of a recorded prefix.
        action = sampled["action"].clone()
        unrestricted = torch.ones_like(masks)
        action[:, 0] = (action[:, 0] + 1) % 42
        changed = policy.evaluate(vectors, maps, action, unrestricted, private)
        self.assertGreater(float((changed["logits"][:, 42:] - result["logits"][:, 42:]).abs().max().detach()), .001)

    def test_atomic_roundtrip_and_nonfinite_export_rejected(self):
        restored = model.load_weights(self.weights)
        self.assertEqual(restored.weight_version, 17)
        self.assertEqual(set(restored.state_dict()), set(model.tensor_shapes()))
        for name, value in self.policy.state_dict().items():
            torch.testing.assert_close(restored.state_dict()[name], value, rtol=0, atol=0)
        path = self.directory / "atomic.bin"
        model.export_weights(restored, path, 22)
        original = path.read_bytes()
        with torch.no_grad():
            restored.conv1.weight[0, 0, 0, 0] = torch.nan
        with self.assertRaises(ValueError):
            model.export_weights(restored, path, 23)
        self.assertEqual(path.read_bytes(), original)
        self.assertFalse(list(self.directory.glob("*.tmp")))

    def test_invalid_files_rejected_by_both_loaders(self):
        original = self.weights.read_bytes()
        corruptions = {}
        corruptions["truncated"] = original[:-4]
        corruptions["trailing"] = original + b"abcd"
        for label, offset, value in (("schema", 16, 0), ("shape", 60, 128),
                                      ("count", 44, 0), ("oversized", 48, 0xFFFFFFFF)):
            data = bytearray(original)
            struct.pack_into("<I", data, offset, value)
            if offset >= model.HEADER.size:
                struct.pack_into("<I", data, 52, zlib.crc32(data[model.HEADER.size:]))
            corruptions[label] = bytes(data)
        bad_crc = bytearray(original)
        bad_crc[-1] ^= 1
        corruptions["crc"] = bad_crc
        # First tensor starts at56: lengths4 + shape8 + bytecount4 + name14.
        data_start = model.HEADER.size + 4 + 8 + 4 + len("vector1.weight")
        for label, change_at in (("name", data_start - len("vector1.weight")), ("nan", data_start)):
            data = bytearray(original)
            if label == "name":
                data[change_at] = ord("x")
            else:
                struct.pack_into("<f", data, change_at, float("nan"))
            struct.pack_into("<I", data, 52, zlib.crc32(data[model.HEADER.size:]))
            corruptions[label] = bytes(data)
        # A correctly checksummed payload with extra bytes must also fail.
        data = bytearray(original + b"tail")
        struct.pack_into("<I", data, 48, len(data) - model.HEADER.size)
        struct.pack_into("<I", data, 52, zlib.crc32(data[model.HEADER.size:]))
        corruptions["checked_trailing"] = bytes(data)
        for label, data in corruptions.items():
            with self.subTest(label=label):
                path = self.directory / (label + ".bin")
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    model.load_weights(path)
                if self.probe is not None:
                    result = subprocess.run([str(self.probe), str(path)], capture_output=True)
                    self.assertEqual(result.returncode, 2, result.stderr.decode())

    def test_invalid_inputs_and_illegal_actions_rejected(self):
        vectors, maps, private, masks = self.fixture(1)
        masks[:, :42] = False
        with self.assertRaises(ValueError):
            self.policy.sample(vectors, maps, masks, private)
        masks[:] = True
        vectors[0, 0] = float("nan")
        with self.assertRaises(ValueError):
            self.policy.sample(vectors, maps, masks, private)
        vectors[0, 0] = 0
        actions = torch.zeros((1, 8), dtype=torch.long)
        actions[0, 0] = 42
        with self.assertRaises(ValueError):
            self.policy.evaluate(vectors, maps, actions, masks, private)

    def test_pcg_reference_parity(self):
        if self.probe is None:
            self.skipTest("g++ unavailable")
        result = subprocess.run([str(self.probe), "--rng"], capture_output=True, text=True, check=True)
        rng = model.CommanderPcg32(901, 3, 17)
        self.assertEqual([int(line) for line in result.stdout.splitlines()], [rng.next() for _ in range(32)])
        other = model.CommanderPcg32(901, 4, 17)
        self.assertNotEqual(model.CommanderPcg32(901, 3, 17).next(), other.next())


if __name__ == "__main__":
    unittest.main()
