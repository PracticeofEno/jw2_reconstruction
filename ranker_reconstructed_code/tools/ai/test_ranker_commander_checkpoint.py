from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import ranker_commander_checkpoint as checkpoint
from ranker_commander_checkpoint import digest, inside, verify, write


class CheckpointIntegrityTests(unittest.TestCase):
    def test_bundle_paths_cannot_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for path in ('../policy.bin', '/policy.bin', 'C:/policy.bin', 'files\\..\\policy.bin'):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    inside(root, path)

    def test_corruption_and_unfetched_lfs_pointer_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            weight = root / 'policy.bin'
            weight.write_bytes(b'original checkpoint bytes')
            write(root / 'manifest.json', dict(schema=1, sources={}, documents={}, completed_rounds=2,
                files={'policy.bin': dict(stored='policy.bin', sha256=digest(weight), bytes=weight.stat().st_size)}))
            self.assertEqual(verify(root, load=False)['completed_rounds'], 2)
            for damaged in (b'truncated', b'version https://git-lfs.github.com/spec/v1\n'):
                weight.write_bytes(damaged)
                with self.assertRaisesRegex(ValueError, 'checkpoint hash mismatch'):
                    verify(root, load=False)

    def test_source_newlines_are_portable_but_semantic_edits_are_not(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'source.py'
            path.write_bytes(b'value = 1\r\n')
            expected = digest(path, text=True)
            path.write_bytes(b'value = 1\n')
            self.assertEqual(digest(path, text=True), expected)
            path.write_bytes(b'value = 2\n')
            self.assertNotEqual(digest(path, text=True), expected)

    def test_long_experiment_paths_keep_weight_sidecars_together(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            campaign = root / 'campaign'
            weight = root / ('long_experiment_' * 7) / 'race0/policy.bin'
            weight.parent.mkdir(parents=True)
            weight.write_bytes(b'checkpoint')
            write(weight.with_suffix('.bin.json'), {'weights_sha256': digest(weight)})
            reference = dict(path=str(weight), sha256=digest(weight))
            write(campaign / 'contract.json', dict(scope='all_policy_parameters', pins=[reference]))
            write(campaign / 'initial_selection.json', dict(current={'0': reference}))
            with patch.object(checkpoint, 'ROOT', root), \
                 patch.object(checkpoint.subprocess, 'check_output', return_value='fixture'), \
                 patch.object(checkpoint.importlib.metadata, 'version', return_value='fixture'):
                checkpoint.export(campaign, root / 'bundle')
            files = checkpoint.read(root / 'bundle/manifest.json')['files']
            stored = [Path(item['stored']) for item in files.values()]
            self.assertTrue(all(len(str(path)) < 50 for path in stored))
            self.assertEqual(stored[0].parent, stored[1].parent)


if __name__ == '__main__':
    unittest.main()
