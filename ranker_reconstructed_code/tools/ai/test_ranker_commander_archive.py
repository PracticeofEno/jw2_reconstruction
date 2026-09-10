import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from ranker_commander_archive import archive_file, candidates, restore_file, retain_marker


class ArchiveTests(unittest.TestCase):
    def test_verified_round_trip_retains_exact_bytes_and_timestamp(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / "commander.rlo"
            original = bytes(range(256)) * 2000
            source.write_bytes(original)
            before = source.stat()
            saved = archive_file(source, root)
            self.assertGreater(saved, 0)
            self.assertFalse(source.exists())
            archive = root / "commander.rlo.gz"
            metadata = json.loads((root / "commander.rlo.gz.json").read_text())
            self.assertEqual(metadata["source_sha256"], hashlib.sha256(original).hexdigest())
            self.assertEqual(restore_file(archive, root), source.resolve())
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(source.stat().st_mtime_ns, before.st_mtime_ns)
            self.assertTrue(retain_marker(source).is_file())
            self.assertEqual(archive_file(source, root), 0)

    def test_only_completed_losing_evaluations_are_candidates(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            work = root / "campaign/training_run"
            work.mkdir(parents=True)
            (work / "state.json").write_text("{}")
            evaluation = work / "elf/baseline"
            rows = []
            for index, (tribe, status, valid) in enumerate(((1,2,True), (1,1,True), (2,2,True), (1,3,False))):
                source = evaluation / f"argmax/game_{index:05d}/output/commander.rlo"
                source.parent.mkdir(parents=True)
                source.write_bytes(b"fixture")
                rows.append(dict(rollout=str(source), own_tribe=tribe, status=status,
                                 valid=valid, evaluation_valid=valid, teacher=False))
            result = dict(gate=dict(coverage_complete=True), reports=rows)
            self.assertEqual(list(candidates(root)), [])
            (evaluation / "evaluation.json").write_text(json.dumps(result))
            self.assertEqual(list(candidates(root)), [Path(rows[0]["rollout"]).resolve()])
            result["gate"]["coverage_complete"] = False
            (evaluation / "evaluation.json").write_text(json.dumps(result))
            self.assertEqual(list(candidates(root)), [])

    def test_outside_source_and_corrupt_archive_never_replace_raw_file(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            root = base / "workspace"
            root.mkdir()
            outside = base / "commander.rlo"
            outside.write_bytes(b"keep")
            with self.assertRaises(ValueError):
                archive_file(outside, root)
            self.assertEqual(outside.read_bytes(), b"keep")
            source = root / "commander.rlo"
            source.write_bytes(b"retained" * 1000)
            archive_file(source, root)
            archive = root / "commander.rlo.gz"
            archive.write_bytes(b"corrupt")
            with self.assertRaises(OSError):
                restore_file(archive, root)
            self.assertFalse(source.exists())


if __name__ == "__main__":
    unittest.main()
