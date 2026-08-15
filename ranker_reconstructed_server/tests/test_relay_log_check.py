from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import relay_log_check  # noqa: E402


COMBINED_LOG = """\
[rebuild] wizardnet relay configured game=7 member=1 host=yes
[rebuild] link relay route initialized game=7 local_member=1 host=yes local_slot=0
[rebuild] wizardnet relay browser join queued game=7 room=RoomA bytes=70
[rebuild] wizardnet relay configured game=7 member=2 host=no
[rebuild] wizardnet relay browser join accepted game=7 member=2
[rebuild] link relay route initialized game=7 local_member=2 host=no local_slot=1
[rebuild] link relay join accepted slot=1 local_member=2 peer_socket=1879048193
[rebuild] wizardnet relay frame queued game=7 target=2 stream=0 bytes=744 wire_bytes=772 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay frame queued game=7 target=1 stream=0 bytes=20 wire_bytes=48 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay link received phase=browser game=7 member=1 bytes=744 count=1
[rebuild] wizardnet relay link received phase=link game=7 member=2 bytes=20 count=1
[rebuild] link countdown timer set value=6 timer=1
[rebuild] link countdown complete calling start_game
[rebuild] wizardnet relay frame queued game=7 target=2 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay frame queued game=7 target=1 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received member=2 bytes=36 queued=36 count=1
[rebuild] wizardnet relay mode1 received member=1 bytes=36 queued=36 count=1
"""


BAD_LATE_SESSION = """\
[rebuild] wizardnet relay configured game=8 member=1 host=yes
[rebuild] link relay route initialized game=8 local_member=1 host=yes local_slot=0
[rebuild] wizardnet relay browser join queued game=8 room=RoomA bytes=70
[rebuild] wizardnet relay configured game=8 member=2 host=no
[rebuild] wizardnet relay browser join accepted game=8 member=2
[rebuild] link relay route initialized game=8 local_member=2 host=no local_slot=1
[rebuild] link relay join accepted slot=1 local_member=2 peer_socket=1879048193
[rebuild] wizardnet relay frame queued game=8 target=2 stream=0 bytes=744 wire_bytes=772 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay frame queued game=8 target=1 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received ignored member=1 bytes=36 queued=36 count=1
"""


DELAYED_OLD_GAME_LINE = """\
[rebuild] wizardnet relay mode1 received ignored member=2 game=6 bytes=36 queued=36 count=1
"""


class RelayLogCheckTests(unittest.TestCase):
    def test_combined_game_id_scope_keeps_host_and_joiner_evidence(self) -> None:
        result = self.check_log(COMBINED_LOG, role="combined", game_id=7)

        self.assertTrue(result["ok"])
        self.assertEqual(result["scope"], "room_RoomA_game_7")
        self.assertEqual(result["effective_game_id"], 7)
        self.assertEqual(result["observed_game_ids"], [7])
        self.assertTrue(result["checks"]["host_host_configured"])
        self.assertTrue(result["checks"]["joiner_joiner_configured"])

    def test_combined_game_id_scope_excludes_later_sessions(self) -> None:
        result = self.check_log(
            COMBINED_LOG + BAD_LATE_SESSION,
            role="combined",
            game_id=7,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["problem_lines"], [])

    def test_room_scope_infers_effective_game_id(self) -> None:
        result = self.check_log(COMBINED_LOG, role="combined", game_id=None)

        self.assertTrue(result["ok"])
        self.assertEqual(result["effective_game_id"], 7)
        self.assertEqual(result["observed_game_ids"], [7])

    def test_log_check_requires_encrypted_relay_payload_evidence(self) -> None:
        plaintext_log = (
            COMBINED_LOG.replace(" wire_bytes=772 crypto=yes", "")
            .replace(" wire_bytes=64 crypto=yes", "")
            .replace(" wire_bytes=48 crypto=yes", "")
        )

        result = self.check_log(plaintext_log, role="combined", game_id=7)

        self.assertFalse(result["ok"])
        self.assertIn("relay_payload_encrypted", result["missing_checks"])

    def test_room_and_game_id_scope_uses_latest_matching_room(self) -> None:
        reused_game_id_log = COMBINED_LOG.replace("RoomA", "OldRoom") + COMBINED_LOG

        result = self.check_log(reused_game_id_log, role="combined", game_id=7)

        self.assertTrue(result["ok"])
        self.assertEqual(result["scope"], "room_RoomA_game_7")
        self.assertFalse(any("OldRoom" in line for line in result["relay_lines"]))

    def test_game_id_scope_filters_delayed_lines_from_other_games(self) -> None:
        mixed_text = COMBINED_LOG.replace(
            "[rebuild] wizardnet relay frame queued game=7 target=2 stream=0",
            DELAYED_OLD_GAME_LINE
            + "[rebuild] wizardnet relay frame queued game=7 target=2 stream=0",
            1,
        )

        result = self.check_log(mixed_text, role="combined", game_id=7)

        self.assertTrue(result["ok"])
        self.assertEqual(result["problem_lines"], [])
        self.assertFalse(any("game=6" in line for line in result["relay_lines"]))

    def check_log(
        self, text: str, *, role: str = "combined", game_id: int | None = None
    ) -> dict[str, object]:
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "Jw2.log"
            log.write_text(text, encoding="utf-8")
            args = argparse.Namespace(
                log=log,
                role=role,
                room="RoomA",
                game_id=game_id,
                require_mode1=True,
                forbid_direct_transport=True,
                tail_bytes=0,
                show_lines=80,
            )
            return relay_log_check.check_log(args)


if __name__ == "__main__":
    unittest.main()
