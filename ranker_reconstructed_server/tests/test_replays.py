from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from ranker_server.replays import ReplayCatalog, fnv1a64, sanitize_replay_filename


class ReplayCatalogTests(unittest.TestCase):
    def test_catalog_commit_reload_and_filename_sanitization(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            upload = root / ".upload.part"
            replay_bytes = b"ranker replay payload"
            upload.write_bytes(replay_bytes)

            catalog = ReplayCatalog(root)
            entry = catalog.commit_upload(
                upload,
                display_name="../한글:대전?.ply",
                uploader="Player",
                byte_count=len(replay_bytes),
                game_type=2,
                game_id=17,
            )

            self.assertEqual(entry.display_name, "한글_대전_.ply")
            self.assertEqual(catalog.path_for(entry).read_bytes(), replay_bytes)
            self.assertEqual(fnv1a64(replay_bytes), 0xCD5E69C2B959B959)

            reloaded = ReplayCatalog(root)
            restored = reloaded.get(entry.replay_id)
            self.assertIsNotNone(restored)
            assert restored is not None
            self.assertEqual(restored.uploader, "Player")
            self.assertEqual(restored.game_type, 2)
            self.assertEqual(restored.game_id, 17)

    def test_sanitize_replay_filename_never_returns_a_path(self) -> None:
        self.assertEqual(sanitize_replay_filename(r"..\..\Replay"), "Replay.ply")
        self.assertEqual(sanitize_replay_filename(""), "Replay.ply")


if __name__ == "__main__":
    unittest.main()
