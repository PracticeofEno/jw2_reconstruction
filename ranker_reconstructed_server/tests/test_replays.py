from __future__ import annotations

from pathlib import Path
import tempfile
import time
import unittest

from ranker_server.replays import (
    ReplayCatalog,
    build_replay_filename,
    fnv1a64,
    sanitize_replay_filename,
)


class ReplayCatalogTests(unittest.TestCase):
    def test_catalog_commit_reload_and_filename_sanitization(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            upload = root / ".upload.part"
            replay_bytes = b"ranker replay payload"
            upload.write_bytes(replay_bytes)

            catalog = ReplayCatalog(root)
            commit = catalog.commit_upload(
                upload,
                display_name="../한글:대전?.ply",
                uploader="Player",
                byte_count=len(replay_bytes),
                game_type=2,
                game_id=17,
            )
            entry = commit.entry
            self.assertTrue(commit.stored)

            self.assertEqual(entry.display_name, "한글_대전_.ply")
            self.assertEqual(entry.stored_name, entry.display_name)
            self.assertEqual(catalog.path_for(entry).read_bytes(), replay_bytes)
            self.assertEqual(fnv1a64(replay_bytes), 0xCD5E69C2B959B959)

            reloaded = ReplayCatalog(root)
            restored = reloaded.get(entry.replay_id)
            self.assertIsNotNone(restored)
            assert restored is not None
            self.assertEqual(restored.uploader, "Player")
            self.assertEqual(restored.game_type, 2)
            self.assertEqual(restored.game_id, 17)
            self.assertEqual(len(restored.content_sha256), 64)

    def test_sanitize_replay_filename_never_returns_a_path(self) -> None:
        self.assertEqual(sanitize_replay_filename(r"..\..\Replay"), "Replay.ply")
        self.assertEqual(sanitize_replay_filename(""), "Replay.ply")

    def test_filename_uses_map_players_and_windows_safe_timestamp(self) -> None:
        timestamp = time.struct_time((2026, 8, 21, 14, 5, 6, 4, 233, -1))
        self.assertEqual(
            build_replay_filename(
                r"Maps\rank\Cross:Roads.trc", ("Alice", "Bob"), timestamp
            ),
            "[Cross_Roads]_AlicevsBob_2026_08_21_14-05-06.ply",
        )
        self.assertEqual(
            build_replay_filename("Arena v1.2", ("Alice", "Bob"), timestamp),
            "[Arena v1.2]_AlicevsBob_2026_08_21_14-05-06.ply",
        )

    def test_catalog_deduplicates_match_and_content_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = ReplayCatalog(root)

            first_upload = root / ".first.part"
            first_upload.write_bytes(b"first participant replay")
            first = catalog.commit_upload(
                first_upload,
                display_name="First.ply",
                uploader="Host",
                byte_count=first_upload.stat().st_size,
                game_type=2,
                game_id=7,
                match_key="match-seven",
            )
            self.assertTrue(first.stored)

            viewpoint_upload = root / ".viewpoint.part"
            viewpoint_upload.write_bytes(b"different local camera data")
            same_match = catalog.commit_upload(
                viewpoint_upload,
                display_name="Second.ply",
                uploader="Join",
                byte_count=viewpoint_upload.stat().st_size,
                game_type=2,
                game_id=7,
                match_key="match-seven",
            )
            self.assertFalse(same_match.stored)
            self.assertEqual(same_match.entry.replay_id, first.entry.replay_id)
            self.assertFalse(viewpoint_upload.exists())

            content_upload = root / ".content.part"
            content_upload.write_bytes(b"first participant replay")
            same_content = catalog.commit_upload(
                content_upload,
                display_name="Third.ply",
                uploader="Other",
                byte_count=content_upload.stat().st_size,
                game_type=1,
                game_id=8,
                match_key="match-eight",
            )
            self.assertFalse(same_content.stored)
            self.assertEqual(same_content.entry.replay_id, first.entry.replay_id)
            self.assertEqual(len(catalog.entries()), 1)

    def test_catalog_adds_suffix_when_distinct_replays_share_a_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = ReplayCatalog(root)
            stored_names = []
            for index in range(2):
                upload = root / f".collision-{index}.part"
                upload.write_bytes(f"distinct-{index}".encode("ascii"))
                result = catalog.commit_upload(
                    upload,
                    display_name="Same Name.ply",
                    uploader="Player",
                    byte_count=upload.stat().st_size,
                    game_type=2,
                    game_id=10 + index,
                    match_key=f"match-{index}",
                )
                stored_names.append(result.entry.stored_name)

            self.assertEqual(stored_names, ["Same Name.ply", "Same Name_2.ply"])


if __name__ == "__main__":
    unittest.main()
