"""Transfer-history, DAgger provenance and explicit legacy migration checks."""
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

import ranker_commander_context as context
import ranker_commander_model as model
import ranker_commander_rollout as rollout


def legacy_records():
    records = np.zeros(6, dtype=rollout._record_dtype(False, vector_size=528))
    records["frame"] = [1, 33, 65, 97, 129, 193]
    records["delta_frame"] = [1, 32, 32, 32, 32, 64]
    records["status"][-1] = rollout.WIN
    records["teacher"] = 0
    records["weight_version"] = 7
    records["mask"] = 1
    records["action"][:-1, 0] = [38, 39, 40, 41, 38]
    # MAIN -> GUARD succeeds; empty GUARD -> MAIN does not; MAIN -> RAID
    # succeeds; RAID -> MAIN succeeds; the final empty MAIN transfer does not.
    records["vector"][0, 166] = .15
    records["vector"][2, 166] = .1
    records["vector"][3, 208] = .1
    records["vector"][1:, 518:526] = (
        records["action"][:-1].astype(np.float32) /
        np.asarray(rollout.HEAD_SIZES, dtype=np.float32)).astype(np.float16)
    records["terminal_reward"] = rollout.terminal_rewards(records["frame"], records["status"])
    return records


def write_legacy_rollout(path, records=None):
    records = legacy_records() if records is None else records
    wire = np.zeros(len(records), dtype=rollout._record_dtype(True, vector_size=528))
    for name in wire.dtype.names:
        wire[name] = np.packbits(records["mask"], axis=-1, bitorder="little") if name == "mask_packed" else records[name]
    for record in wire:
        record["crc32"] = zlib.crc32(record.tobytes()[:-4])
    path.write_bytes(rollout.HEADER.pack(rollout.MAGIC, 0x1F364207, 2, 1, 901, 7,
                                       528, rollout.LEGACY_MAP_SIZE, wire.dtype.itemsize) + wire.tobytes())


def write_legacy_weights(path, policy):
    payload = bytearray()
    for name, shape in model.tensor_shapes(528).items():
        raw = policy.state_dict()[name].numpy().astype("<f4").tobytes()
        encoded = name.encode("ascii")
        payload += struct.pack("<HH", len(encoded), len(shape))
        payload += struct.pack("<" + "I" * len(shape), *shape)
        payload += struct.pack("<I", len(raw)) + encoded + raw
    path.write_bytes(model.HEADER.pack(model.MAGIC, 1, policy.weight_version,
                                      0x1F364207, 528, rollout.LEGACY_MAP_SIZE, model.PRIVILEGED_SIZE,
                                      8, model.LOGIT_COUNT, len(model.tensor_shapes(528)),
                                      len(payload), zlib.crc32(payload)) + payload)


class CommanderContextTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="commander_context_test_")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def episode(self, records=None):
        records = legacy_records() if records is None else records
        return rollout.Episode(self.directory / "source.rlo", 1, 901, 7, records)

    def test_default_style_and_never_used_transfer_have_distinct_flags(self):
        encoded = context.encode_context(440, [0, 440, 1, 0], 0)
        np.testing.assert_array_equal(encoded[:4], np.array([440, 0, 439, 440], np.float32) / np.float32(6000))
        np.testing.assert_array_equal(encoded[4:8], [0, 1, 1, 0])
        np.testing.assert_array_equal(encoded[8:], [.25, 0, 1, 0, 0, 0])
        np.testing.assert_array_equal(context.encode_context(60000, [0] * 4, 0)[:4], np.ones(4))

    def test_native_view_and_binary16_context_match_python(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ unavailable for commander context parity probe")
        root = Path(__file__).resolve().parents[2]
        probe = self.directory / ("context_probe.exe" if os.name == "nt" else "context_probe")
        subprocess.run([
            compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-static", "-I", str(root / "include"),
            str(root / "tests/ai_commander_context_probe.cpp"),
            str(root / "src/ranker_ai_commander.cpp"),
            str(root / "src/ranker_ai_expansion.cpp"),
            str(root / "src/ranker_ai_commander_rollout.cpp"), "-o", str(probe),
        ], check=True, capture_output=True, timeout=120)
        timings = [(1, [0, 0, 0, 0]), (440, [0, 1, 439, 440]),
                   (6001, [0, 1, 4901, 5969]), (60000, [0, 1, 54000, 59999])]
        cases = [(frame, clocks, variant) for variant in (0, 1, 2, 7, 15, 100, 0xFFFFFFFF)
                 for frame, clocks in timings]
        source, output = self.directory / "context_input.bin", self.directory / "context_output.bin"
        source.write_bytes(struct.pack("<I", len(cases)) + b"".join(
            struct.pack("<6I", frame, variant, *clocks) for frame, clocks, variant in cases))
        subprocess.run([str(probe), str(source), str(output)], check=True, capture_output=True, timeout=30)
        actual = np.fromfile(output, dtype="<f4").reshape(len(cases), 2, 14)
        expected = np.asarray([context.encode_context(frame, clocks, variant)
                               for frame, clocks, variant in cases])
        np.testing.assert_array_equal(actual[:, 0], expected)
        np.testing.assert_array_equal(actual[:, 1], expected.astype(np.float16).astype(np.float32))

    def test_teacher_variant_u32_and_context_validation(self):
        for variant in (0, 1, 15, 0xFFFFFFFF):
            params = context.teacher_parameters(variant)
            self.assertTrue(2 <= params.opening_velocis <= 8)
            self.assertTrue(-3000 <= params.expansion_shift <= 3000)
            self.assertTrue(0 <= params.target_priority <= 2)
        for variant in (-1, 0x100000000, True, 1.5):
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                context.teacher_parameters(variant)
        with self.assertRaises(ValueError):
            context.encode_context(3, [0, 0, 0, 4], 0)
        with self.assertRaises(ValueError):
            context.encode_context(3, [0], 0)

    def test_context_uses_predecision_state_and_only_nonempty_executed_transfers(self):
        original = legacy_records()
        before = original.tobytes()
        converted = context.migrate_episode(self.episode(original), teacher_variant=13)
        expected_clocks = [[0, 0, 0, 0], [1, 0, 0, 0], [1, 0, 0, 0],
                           [1, 0, 65, 0], [1, 0, 65, 97], [1, 0, 65, 97]]
        for row, clocks in zip(converted.records, expected_clocks):
            expected = context.encode_context(int(row["frame"]), clocks, 13).astype(np.float16).astype(np.float32)
            np.testing.assert_array_equal(row["vector"][528:], expected)
        np.testing.assert_array_equal(converted.records["vector"][:, :528], original["vector"])
        self.assertEqual(before, original.tobytes())

    def test_incomplete_and_relabelled_histories_are_rejected(self):
        cases = []
        truncated = legacy_records()
        truncated["frame"][0] = 2
        truncated["delta_frame"][:2] = [2, 31]
        cases.append(truncated)
        relabeled = legacy_records()
        relabeled["action"][0, 0] = 0
        cases.append(relabeled)
        initialized = legacy_records()
        initialized["vector"][0, 526] = .1
        cases.append(initialized)
        masked = legacy_records()
        masked["mask"][0, 38] = 0
        cases.append(masked)
        for index, records in enumerate(cases):
            with self.subTest(case=index), self.assertRaises(rollout.RolloutError):
                context.migrate_episode(self.episode(records), teacher_variant=0)

    def test_silent_rejections_do_not_discard_observable_transfer_history(self):
        records = legacy_records()
        records["vector"][:, 527] = .2
        converted = context.migrate_episode(self.episode(records), teacher_variant=0)
        self.assertEqual(converted.records["vector"][-1, 532], 1)

    def test_dagger_labels_are_preserved_but_do_not_drive_clocks(self):
        source, destination = self.directory / "source.rlo", self.directory / "converted.rlo"
        write_legacy_rollout(source)
        labels = np.zeros(6, dtype=rollout.LABEL_RECORD)
        labels["mask_packed"] = np.packbits(np.ones((6, rollout.MASK_SIZE), dtype=np.uint8), axis=-1, bitorder="little")
        # The teacher wants GUARD -> MAIN throughout, regardless of execution.
        labels["action"][:-1, 0] = 39
        label_bytes = rollout.LABEL_MAGIC + labels.tobytes()
        Path(str(source) + ".teacher.bin").write_bytes(label_bytes)
        original_bytes = source.read_bytes()
        with self.assertRaises(rollout.RolloutError):
            rollout.read_rollout(source)
        metadata = context.migrate_rollout_file(source, destination, teacher_variant=5)
        self.assertEqual(source.read_bytes(), original_bytes)
        self.assertEqual(Path(str(destination) + ".teacher.bin").read_bytes(), label_bytes)
        with self.assertRaises(rollout.RolloutError):
            rollout.read_rollout(destination)
        converted = rollout.read_rollout(destination, allow_legacy=True)
        try:
            original = legacy_records()
            for field in ("action", "teacher", "logp"):
                np.testing.assert_array_equal(converted.records[field], original[field])
            self.assertEqual(converted.records["vector"][-1, 533], 0)
            relabeled = rollout.relabel_with_teacher(converted)
            np.testing.assert_array_equal(relabeled.records["action"][:-1, 0], [39] * 5)
            self.assertEqual(relabeled.records["vector"][-1, 533], 0)
        finally:
            converted.close()
        self.assertEqual(metadata["teacher_variant"], 5)
        self.assertTrue(metadata["teacher_labels_sha256"])
        self.assertEqual(json.loads(Path(str(destination) + ".context.json").read_text())["source_sha256"], metadata["source_sha256"])

    def test_relabel_does_not_mutate_in_memory_migrated_behavior(self):
        episode = self.episode()
        converted = context.migrate_episode(episode, teacher_variant=0)
        original = converted.records.tobytes()
        labels = np.zeros(6, dtype=rollout.LABEL_RECORD)
        labels["mask_packed"] = np.packbits(np.ones((6, rollout.MASK_SIZE), dtype=np.uint8), axis=-1, bitorder="little")
        Path(str(episode.path) + ".teacher.bin").write_bytes(rollout.LABEL_MAGIC + labels.tobytes())
        relabeled = rollout.relabel_with_teacher(converted)
        self.assertEqual(converted.records.tobytes(), original)
        np.testing.assert_array_equal(relabeled.records["teacher"], np.ones(6))

    def test_rollout_migration_is_explicit_and_never_overwrites(self):
        source, destination = self.directory / "source.rlo", self.directory / "output.rlo"
        write_legacy_rollout(source)
        with self.assertRaises(ValueError):
            context.migrate_rollout_file(source, destination)
        with self.assertRaises(ValueError):
            context.migrate_rollout_file(source, source, teacher_variant=0)
        destination.write_text("keep me")
        with self.assertRaises(FileExistsError):
            context.migrate_rollout_file(source, destination, teacher_variant=0)
        self.assertEqual(destination.read_text(), "keep me")

    def test_migration_rejects_orphaned_consumer_sidecars(self):
        source = self.directory / "source.rlo"
        write_legacy_rollout(source)
        self.assertFalse(Path(str(source) + ".teacher.bin").exists())
        destination = self.directory / "orphaned_labels.rlo"
        labels = np.zeros(6, dtype=rollout.LABEL_RECORD)
        labels["mask_packed"] = np.packbits(np.ones((6, rollout.MASK_SIZE), dtype=np.uint8), axis=-1, bitorder="little")
        stale_labels = Path(str(destination) + ".teacher.bin")
        stale_label_bytes = rollout.LABEL_MAGIC + labels.tobytes()
        stale_labels.write_bytes(stale_label_bytes)
        with self.assertRaises(FileExistsError):
            context.migrate_rollout_file(source, destination, teacher_variant=0)
        self.assertEqual(stale_labels.read_bytes(), stale_label_bytes)
        self.assertFalse(destination.exists())
        self.assertFalse(Path(str(destination) + ".context.json").exists())

        legacy_weights = self.directory / "legacy.bin"
        write_legacy_weights(legacy_weights, model.CommanderPolicy(7, vector_size=528))
        for index, suffix in enumerate((".json", ".optimizer.npz")):
            with self.subTest(sidecar=suffix):
                destination = self.directory / f"orphaned_checkpoint_{index}.bin"
                sidecar = Path(str(destination) + suffix)
                stale_bytes = b"existing checkpoint sidecar must remain untouched"
                sidecar.write_bytes(stale_bytes)
                with self.assertRaises(FileExistsError):
                    context.migrate_weights_file(legacy_weights, destination)
                self.assertEqual(sidecar.read_bytes(), stale_bytes)
                self.assertFalse(destination.exists())
                self.assertFalse(Path(str(destination) + ".context.json").exists())

    def test_job_variant_requires_matching_provenance(self):
        episode = self.episode()
        job = self.directory / "job.json"
        data = {"seed": 901, "rollout": str(episode.path), "teacher_variant": 6, "valid": True}
        job.write_text(json.dumps(data))
        self.assertEqual(context.variant_from_job(job, episode)[0], 6)
        data.pop("teacher_variant")
        job.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "no verifiable teacher variant"):
            context.variant_from_job(job, episode)
        data["command"] = ["ranker_rebuild.exe", "-AICOMMANDER", f"-AIROLLOUT:{episode.path}"]
        job.write_text(json.dumps(data))
        self.assertEqual(context.variant_from_job(job, episode)[0], 0)
        data["teacher_variant"] = 1
        job.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "disagrees"):
            context.variant_from_job(job, episode)
        data["seed"] = 1
        job.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "policy seed"):
            context.variant_from_job(job, episode)
        data["seed"] = 901
        data["valid"] = False
        job.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "validated completed game"):
            context.variant_from_job(job, episode)
        data["valid"] = True
        data.pop("teacher_variant")
        data["command"][-1] = "-AIROLLOUT:some_other_game.rlo"
        job.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "does not identify"):
            context.variant_from_job(job, episode)

    def test_weights_migration_preserves_legacy_predictions_and_requires_revalidation(self):
        torch.set_num_threads(1)
        torch.manual_seed(571)
        legacy = model.CommanderPolicy(7, vector_size=528).eval()
        source, destination = self.directory / "legacy.bin", self.directory / "context.bin"
        write_legacy_weights(source, legacy)
        original_bytes = source.read_bytes()
        with self.assertRaises(ValueError):
            model.load_weights(source)
        metadata = context.migrate_weights_file(source, destination)
        with self.assertRaises(ValueError):
            model.load_weights(destination)
        upgraded = model.load_weights(destination, allow_legacy=True).eval()
        self.assertTrue(metadata["requires_bc_revalidation"])
        self.assertEqual(upgraded.weight_version, 7)
        self.assertEqual(source.read_bytes(), original_bytes)
        np.testing.assert_array_equal(upgraded.vector1.weight.detach().numpy()[:, 528:], np.zeros((256, 14)))
        vector = torch.rand(3, 528)
        maps = torch.rand(3, model.LEGACY_MAP_SIZE)
        private = torch.rand(3, model.PRIVILEGED_SIZE)
        masks = torch.ones(3, model.LOGIT_COUNT, dtype=torch.bool)
        with torch.no_grad():
            old = legacy.sample(vector, maps, masks, private, deterministic=True)
            new = upgraded.sample(torch.cat([vector, torch.randn(3, 14)], 1), maps, masks, private, deterministic=True)
        torch.testing.assert_close(old["logits"], new["logits"], rtol=0, atol=2e-7)
        torch.testing.assert_close(old["value"], new["value"], rtol=0, atol=2e-7)
        torch.testing.assert_close(old["action"], new["action"])
        with self.assertRaises(FileExistsError):
            context.migrate_weights_file(source, destination)


if __name__ == "__main__":
    unittest.main()
