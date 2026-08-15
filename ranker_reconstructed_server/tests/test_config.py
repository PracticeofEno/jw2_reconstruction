from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from ranker_server.config import load_config


class ServerConfigTests(unittest.TestCase):
    def test_load_config_resolves_relay_evidence_dir_relative_to_config(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            config_path = base / "config.json"
            config_path.write_text(
                json.dumps(
                    {
                        "listen": {},
                        "server": {},
                        "logging": {},
                        "data": {
                            "account_file": "data/accounts.json",
                            "relay_evidence_dir": "../debug_artifacts/relay_evidence",
                        },
                    }
                ),
                encoding="utf-8",
            )

            config = load_config(config_path)

            self.assertEqual(config.account_file, base / "data/accounts.json")
            self.assertEqual(
                config.relay_evidence_dir,
                base.parent / "debug_artifacts/relay_evidence",
            )


if __name__ == "__main__":
    unittest.main()
