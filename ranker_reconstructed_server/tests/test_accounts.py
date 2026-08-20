from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from ranker_server.accounts import AccountStore


class AccountStoreTests(unittest.TestCase):
    def test_account_hash_is_persisted_and_reloaded(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "accounts.json"
            store = AccountStore(path)
            self.assertTrue(store.create("PlayerOne", "secret12", {"avatar": 1}))
            self.assertFalse(store.create("playerone", "otherpass", {}))
            self.assertTrue(store.verify("PLAYERONE", "secret12"))
            self.assertFalse(store.verify("PlayerOne", "wrongpass"))

            serialized = path.read_text(encoding="utf-8")
            self.assertNotIn("secret12", serialized)
            reloaded = AccountStore(path)
            self.assertTrue(reloaded.verify("PlayerOne", "secret12"))

    def test_profile_value_update_is_persisted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "accounts.json"
            store = AccountStore(path)
            self.assertTrue(store.create("Marker", "secret12", {"lobby_mark": 0}))
            self.assertEqual(store.profile_value("marker", "lobby_mark"), 0)
            self.assertTrue(store.set_profile_value("MARKER", "lobby_mark", 4))
            self.assertFalse(store.set_profile_value("missing", "lobby_mark", 1))

            reloaded = AccountStore(path)
            self.assertEqual(reloaded.profile_value("Marker", "lobby_mark"), 4)
            self.assertEqual(reloaded.profile_value("Marker", "unknown", 3), 3)

    def test_normal_and_rank_statistics_are_separate_and_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "accounts.json"
            store = AccountStore(path)
            self.assertTrue(store.create("Player", "secret12", {}))

            self.assertTrue(store.record_match("player", "normal", "wins", "match-a"))
            self.assertFalse(store.record_match("PLAYER", "normal", "wins", "match-a"))
            self.assertTrue(store.record_match("Player", "rank", "draws", "match-b"))
            self.assertEqual(
                store.statistics("Player", "normal"),
                {"wins": 1, "losses": 0, "draws": 0},
            )
            self.assertEqual(
                store.statistics("Player", "rank"),
                {"wins": 0, "losses": 0, "draws": 1},
            )
            self.assertEqual(store.rank_points("Player"), 1)

            reloaded = AccountStore(path)
            self.assertEqual(reloaded.statistics("Player", "normal")["wins"], 1)
            self.assertEqual(reloaded.statistics("Player", "rank")["draws"], 1)
            self.assertFalse(
                reloaded.record_match("Player", "normal", "losses", "match-a")
            )


if __name__ == "__main__":
    unittest.main()
