"""Compile the native RLO1 writer and verify its actual bytes through training IO."""
import json
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

import ranker_commander_rollout as rollout
from ranker_commander_train import TrainConfig, build_batch


PROBE_SOURCE = r'''
#include "ranker_ai_commander_rollout.h"
#include <cmath>
#include <fstream>
#include <limits>

int main(int argc, char** argv) {
    if (argc != 4) return 1;
    const std::string mode = argv[3];
    if (mode == "quantize") {
        std::ifstream source(argv[1], std::ios::binary);
        std::ofstream target(argv[2], std::ios::binary);
        u32 count = 0;
        source.read(reinterpret_cast<char*>(&count), sizeof(count));
        for (u32 offset = 0; offset < count;) {
            ranker::CommanderInput input;
            const u32 batch = std::min<u32>(528, count - offset);
            if (!source.read(reinterpret_cast<char*>(input.vector.data()), batch * sizeof(float))) return 19;
            ranker::QuantizeCommanderMap(input);
            target.write(reinterpret_cast<const char*>(input.vector.data()), batch * sizeof(float));
            offset += batch;
        }
        return target ? 0 : 20;
    }
    ranker::CommanderRolloutWriter writer;
    if (!writer.open(argv[1], 3, 901, 17)) return 2;
    ranker::CommanderInput input;
    ranker::CommanderDecision decision;
    for (std::size_t i = 0; i < input.vector.size(); ++i)
        input.vector[i] = (static_cast<int>(i % 33) - 16) / 16.0f;
    const float half_edges[] = {0.33333334f, 1.00048828125f, 1.00146484375f,
        65504.0f, 0x1p-24f, 0x1p-25f, 3.0f * 0x1p-25f, -0.0f,
        -1.00048828125f, -1.00146484375f, -0x1p-24f, -0x1p-25f,
        std::nextafter(0x1p-25f, 1.0f), std::nextafter(0x1p-25f, 0.0f)};
    for (std::size_t i = 0; i < sizeof(half_edges) / sizeof(float); ++i)
        input.vector[i] = half_edges[i];
    for (std::size_t i = 0; i < input.map.size(); ++i)
        input.map[i] = (static_cast<int>(i % 260) - 2) / 255.0f;
    input.map[0] = 0.5f / 255.0f;
    input.map[1] = 1.5f / 255.0f;
    input.map[2] = 2.5f / 255.0f;
    input.map[3] = 254.5f / 255.0f;
    input.map[4] = std::nextafter(1.5f / 255.0f, 0.0f);
    input.map[5] = std::nextafter(1.5f / 255.0f, 1.0f);
    for (std::size_t i = 0; i < input.privileged.size(); ++i)
        input.privileged[i] = (static_cast<int>(i) - 10) / 8.0f;
    std::ofstream raw(argv[2], std::ios::binary);
    raw.write(reinterpret_cast<const char*>(input.map.data()), sizeof(input.map));
    std::ofstream halves(std::string(argv[2]) + ".halves.bin", std::ios::binary);
    halves.write(reinterpret_cast<const char*>(input.vector.data()), sizeof(input.vector));
    halves.write(reinterpret_cast<const char*>(input.privileged.data()), sizeof(input.privileged));
    ranker::QuantizeCommanderMap(input);
    halves.write(reinterpret_cast<const char*>(input.vector.data()), sizeof(input.vector));
    halves.write(reinterpret_cast<const char*>(input.privileged.data()), sizeof(input.privileged));
    halves.close();
    raw.write(reinterpret_cast<const char*>(input.map.data()), sizeof(input.map));
    raw.close();
    for (std::size_t h = 0; h < ranker::kCommanderHeadCount; ++h) {
        const std::size_t start = ranker::kCommanderHeadOffsets[h];
        const std::size_t size = ranker::kCommanderHeadSizes[h];
        decision.action[h] = static_cast<u8>(size - 1);
        for (std::size_t a = 0; a < size; ++a) decision.mask[start + a] = 1;
        decision.logp[h] = -std::log(static_cast<float>(size));
    }
    // H1=TRANSFER makes the unused building anchor a singleton.
    decision.action[1] = 0;
    for (std::size_t a = 0; a < 16; ++a) decision.mask[42 + a] = a == 0;
    decision.logp[1] = 0;
    decision.value = 0.25f;
    std::array<float,4> potential{0.05f,0.02f,0.01f,0.0f};
    if (!writer.append(1,0,false,ranker::CommanderRolloutStatus::decision,input,decision,potential)) return 3;
    if (mode == "single_frame") {
        decision.action.fill(0);
        if (!writer.append(1,0,false,ranker::CommanderRolloutStatus::truncated,input,decision,potential)) return 10;
        writer.close();
        return 0;
    }
    input.vector[0] = 0.5f;
    input.privileged[0] = 2.0f;
    decision.value = 0.3f;
    potential = {0.1f,0.03f,0.02f,0.025f};
    if (!writer.append(33,2,false,ranker::CommanderRolloutStatus::decision,input,decision,potential)) return 4;
    if (mode == "invalid_frames") {
        if (writer.append(33,0,false,ranker::CommanderRolloutStatus::decision,input,decision,potential)) return 11;
        if (writer.append(32,0,false,ranker::CommanderRolloutStatus::decision,input,decision,potential)) return 12;
        if (writer.append(32,0,false,ranker::CommanderRolloutStatus::truncated,input,decision,potential)) return 13;
    }
    input.vector[0] = 0.75f;
    input.privileged[0] = 3.0f;
    decision.value = 0.4f;
    potential = {0.12f,0.04f,0.03f,0.025f};
    const auto status = mode == "win" ?
        ranker::CommanderRolloutStatus::win : ranker::CommanderRolloutStatus::truncated;
    const u32 terminal_frame = mode == "same_frame" ? 33 : 1801;
    if (mode == "same_frame") decision.action.fill(0);
    if (!writer.append(terminal_frame,0,false,status,input,decision,potential)) return 5;
    if (mode == "invalid_frames") {
        if (writer.append(1802,0,false,ranker::CommanderRolloutStatus::decision,input,decision,potential)) return 14;
        if (writer.append(1802,0,false,status,input,decision,potential)) return 15;
    }
    writer.close();
    if (mode == "reopen") {
        if (!writer.open(argv[1],3,901,18)) return 16;
        if (!writer.append(1,0,false,ranker::CommanderRolloutStatus::decision,input,decision,potential)) return 17;
        if (!writer.append(33,0,false,status,input,decision,potential)) return 18;
        writer.close();
    }
    return 0;
}
'''


class NativeRolloutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("g++ unavailable for native RLO1 writer parity")
        cls.temp = tempfile.TemporaryDirectory(prefix="ranker_commander_rollout_")
        cls.directory = Path(cls.temp.name)
        root = Path(__file__).resolve().parents[2]
        source = cls.directory / "probe.cpp"
        source.write_text(PROBE_SOURCE, encoding="utf-8")
        executable = cls.directory / ("probe.exe" if os.name == "nt" else "probe")
        cls.executable = executable
        # Static runtime: avoid a mismatched libstdc++ DLL from another MinGW on PATH.
        subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-static", "-I", str(root / "include"),
                        str(source), str(root / "src/ranker_ai_commander_rollout.cpp"), "-o", str(executable)],
                       check=True, capture_output=True)
        for status in ("win", "truncated", "same_frame", "single_frame", "invalid_frames", "reopen"):
            subprocess.run([str(executable), str(cls.directory / (status + ".rlo")),
                            str(cls.directory / (status + ".maps.bin")), status], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def read(self, status="win"):
        episode = rollout.read_rollout(self.directory / (status + ".rlo"), current_version=18, teacher=False)
        self.addCleanup(episode.records._mmap.close)
        return episode

    def test_native_header_record_crc_and_python_roundtrip(self):
        episode = self.read()
        raw = episode.path.read_bytes()
        self.assertEqual(len(raw), 40 + 3 * 3522)
        self.assertEqual(rollout.HEADER.unpack_from(raw),
                         (b"JWRLO001", 0x1F364207, 2, 3, 901, 17, 528, 2304, 3522))
        for index, record in enumerate(episode.records):
            self.assertEqual(int(record["crc32"]), zlib.crc32(raw[40 + index * 3522:40 + (index + 1) * 3522 - 4]))
        np.testing.assert_array_equal(episode.records["frame"], [1, 33, 1801])
        np.testing.assert_array_equal(episode.records["event"], [0, 2, 0])
        np.testing.assert_array_equal(episode.records["delta_frame"], [1, 32, 1768])
        np.testing.assert_array_equal(episode.records["weight_version"], [17, 17, 17])
        self.assertEqual(episode.raw_records["vector"].dtype, np.dtype("<f2"))
        self.assertEqual(episode.raw_records["privileged"].dtype, np.dtype("<f2"))
        self.assertEqual(episode.raw_records["mask_packed"].shape, (3, 12))
        self.assertFalse(np.any(episode.raw_records["mask_packed"][:, -1] & 0x80))
        np.testing.assert_array_equal(episode.records["action"][:, 1], 0)
        np.testing.assert_array_equal(episode.records["logp"][:, 1], 0)
        python_path = self.directory / "python_roundtrip.rlo"
        rollout.write_rollout(python_path, episode.records, owner=episode.owner,
                             seed=episode.seed, weight_version=episode.weight_version)
        self.assertEqual(raw, python_path.read_bytes())
        sidecar = [json.loads(line) for line in Path(str(episode.path) + ".decisions.jsonl").read_text().splitlines()]
        self.assertEqual([item["status"] for item in sidecar], [0, 0, 1])
        self.assertEqual(sidecar[0]["action"], episode.decisions[0]["action"].tolist())

    def test_map_rounding_matches_inference_and_training(self):
        episode = self.read()
        raw, inference = np.fromfile(self.directory / "win.maps.bin", dtype="<f4").reshape(2, 2304)
        scaled = np.clip(raw, np.float32(0), np.float32(1)) * np.float32(255)
        # C++ round uses ties away from zero. numpy.rint uses ties-to-even;
        # converting to float64 before adding0.5 preserves just-below ties.
        expected_bytes = np.floor(scaled.astype(np.float64) + .5).astype(np.uint8)
        np.testing.assert_array_equal(episode.records["map"][0], expected_bytes)
        np.testing.assert_array_equal(expected_bytes[:6], [1, 2, 3, 255, 1, 2])
        restored = expected_bytes.astype(np.float32) / np.float32(255)
        np.testing.assert_array_equal(inference, restored)
        batch, _ = build_batch([episode], TrainConfig(iteration=300))
        np.testing.assert_array_equal(batch["maps"][0].numpy().reshape(-1), inference)
        np.testing.assert_array_equal(batch["privileged"].numpy(), episode.decisions["privileged"])
        expected_private = (np.arange(32, dtype=np.float32) - 10) / 8
        np.testing.assert_array_equal(episode.records["privileged"][0], expected_private)
        np.testing.assert_array_equal(batch["vector"][0].numpy(), episode.decisions[0]["vector"])
        self.assertEqual(float(batch["privileged"][1, 0]), 2.0)

    def test_native_potentials_terminal_reward_and_truncation_bootstrap(self):
        victory = self.read()
        truncated = self.read("truncated")
        expected_potential = np.array([[.05,.02,.01,0], [.1,.03,.02,.025],
                                       [.12,.04,.03,.025]], dtype=np.float32)
        np.testing.assert_array_equal(victory.records["potential"], expected_potential)
        np.testing.assert_array_equal(victory.records["terminal_reward"][:-1], 0)
        np.testing.assert_array_equal(victory.records["reserved_reward"], 0)
        win = rollout.episode_returns(victory)
        cap = rollout.episode_returns(truncated)
        discounts = .997 ** (np.array([32, 1768]) / 32)
        self.assertAlmostEqual(float(win["terminal"][-1]), 1 + .3 * (1 - 1801 / 60000), places=6)
        np.testing.assert_allclose(win["shape"][-1], -expected_potential[-2], rtol=0, atol=1e-7)
        np.testing.assert_array_equal(cap["terminal"], 0)
        np.testing.assert_allclose(cap["shape"][-1],
                                   discounts[-1] * expected_potential[-1] - expected_potential[-2], rtol=0, atol=1e-7)
        expected_mc = cap["reward"][-1] + discounts[-1] * .4
        self.assertAlmostEqual(float(cap["mc_return"][-1]), expected_mc, places=6)

    def test_native_corruption_and_incomplete_episode_rejected(self):
        original = (self.directory / "win.rlo").read_bytes()
        cases = {"partial": original[:-1], "missing_terminal": original[:-3522],
                 "trailing": original + b"x"}
        for label, offset in (("schema", 8), ("record_size", 36), ("record_crc", 40 + 100)):
            bad = bytearray(original)
            bad[offset] ^= 1
            cases[label] = bytes(bad)
        for label, raw in cases.items():
            with self.subTest(label=label):
                path = self.directory / ("bad_" + label + ".rlo")
                path.write_bytes(raw)
                with self.assertRaises(rollout.RolloutError):
                    rollout.read_rollout(path)

    def test_same_frame_terminal_replaces_unelapsed_action_and_jsonl(self):
        episode = self.read("same_frame")
        self.assertEqual(episode.path.stat().st_size, 40 + 2 * 3522)
        np.testing.assert_array_equal(episode.records["frame"], [1, 33])
        np.testing.assert_array_equal(episode.records["delta_frame"], [1, 32])
        np.testing.assert_array_equal(episode.records["status"], [rollout.DECISION, rollout.TRUNCATED])
        np.testing.assert_array_equal(episode.terminal["action"], 0)
        self.assertEqual(float(episode.terminal["vector"][0]), .75)
        self.assertEqual(float(episode.terminal["privileged"][0]), 3.0)
        self.assertAlmostEqual(float(episode.terminal["value"]), .4)
        sidecar = Path(str(episode.path) + ".decisions.jsonl").read_text()
        entries = [json.loads(line) for line in sidecar.splitlines()]
        self.assertEqual(len(entries), 2)
        self.assertEqual(entries[-1]["action"], [0] * 8)
        self.assertEqual(entries[-1]["status"], rollout.TRUNCATED)
        self.assertEqual(entries[-1]["event"], 0)
        batch, _ = build_batch([episode], TrainConfig(iteration=300))
        self.assertEqual(len(batch["actions"]), 1)
        np.testing.assert_array_equal(batch["actions"][0].numpy(), episode.decisions[0]["action"])

    def test_single_same_frame_decision_is_not_a_trainable_transition(self):
        path = self.directory / "single_frame.rlo"
        self.assertEqual(path.stat().st_size, 40 + 3522)
        with self.assertRaisesRegex(rollout.RolloutError, "decision and terminal"):
            rollout.read_rollout(path)
        entries = [json.loads(line) for line in Path(str(path) + ".decisions.jsonl").read_text().splitlines()]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["status"], rollout.TRUNCATED)

    def test_backward_duplicate_and_post_terminal_writes_do_not_change_episode(self):
        episode = self.read("invalid_frames")
        self.assertEqual(episode.path.read_bytes(), (self.directory / "truncated.rlo").read_bytes())
        np.testing.assert_array_equal(episode.records["frame"], [1, 33, 1801])
        entries = Path(str(episode.path) + ".decisions.jsonl").read_text().splitlines()
        self.assertEqual(len(entries), 3)

    def test_reopen_resets_last_terminal_and_offsets(self):
        episode = self.read("reopen")
        self.assertEqual(episode.weight_version, 18)
        np.testing.assert_array_equal(episode.records["frame"], [1, 33])
        self.assertEqual(episode.path.stat().st_size, 40 + 2 * 3522)

    def test_half_precision_ties_subnormals_and_pre_inference_quantization(self):
        episode = self.read()
        raw, native = np.fromfile(self.directory / "win.maps.bin.halves.bin", dtype="<f4").reshape(2, 560)
        expected = raw.astype(np.float16).astype(np.float32)
        np.testing.assert_array_equal(native.view(np.uint32), expected.view(np.uint32))
        np.testing.assert_array_equal(episode.records["vector"][0].view(np.uint32), expected[:528].view(np.uint32))
        np.testing.assert_array_equal(episode.records["privileged"][0], expected[528:])
        batch, _ = build_batch([episode], TrainConfig(iteration=300))
        np.testing.assert_array_equal(batch["vector"][0].numpy().view(np.uint32), native[:528].view(np.uint32))

    def test_rechecksummed_invalid_compact_metadata_is_rejected(self):
        original = (self.directory / "win.rlo").read_bytes()
        alterations = {
            "delta": ("delta_frame", 2), "version": ("weight_version", 18),
            "reward": ("terminal_reward", .5), "reserved_reward": ("reserved_reward", .5),
        }
        for label, (name, value) in alterations.items():
            records = np.frombuffer(original[40:], dtype=rollout.WIRE_RECORD_DTYPE).copy()
            records[0][name] = value
            records[0]["crc32"] = zlib.crc32(records[0].tobytes()[:-4])
            path = self.directory / ("metadata_" + label + ".rlo")
            path.write_bytes(original[:40] + records.tobytes())
            with self.subTest(label=label), self.assertRaises(rollout.RolloutError):
                rollout.read_rollout(path)
        records = np.frombuffer(original[40:], dtype=rollout.WIRE_RECORD_DTYPE).copy()
        records[0]["mask_packed"][-1] |= 0x80
        records[0]["crc32"] = zlib.crc32(records[0].tobytes()[:-4])
        path = self.directory / "metadata_mask_padding.rlo"
        path.write_bytes(original[:40] + records.tobytes())
        with self.assertRaisesRegex(rollout.RolloutError, "96th"):
            rollout.read_rollout(path)

    def test_all_finite_half_values_and_random_float_rounding(self):
        bits = np.arange(65536, dtype=np.uint16)
        representable = bits[(bits & 0x7c00) != 0x7c00].view(np.float16).astype(np.float32)
        rng = np.random.default_rng(9041)
        candidates = rng.integers(0, 2 ** 32, size=50000, dtype=np.uint32).view(np.float32)
        samples = np.concatenate((representable, candidates[np.isfinite(candidates)]))
        source = self.directory / "rounding_input.bin"
        target = self.directory / "rounding_output.bin"
        source.write_bytes(struct.pack("<I", len(samples)) + samples.astype("<f4").tobytes())
        subprocess.run([str(self.executable), str(source), str(target), "quantize"], check=True, capture_output=True)
        actual = np.fromfile(target, dtype="<f4")
        with np.errstate(over="ignore"):
            expected = samples.astype(np.float16).astype(np.float32)
        np.testing.assert_array_equal(actual.view(np.uint32), expected.view(np.uint32))


if __name__ == "__main__":
    unittest.main()
