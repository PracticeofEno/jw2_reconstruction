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


if __name__ == "__main__":
    unittest.main()
