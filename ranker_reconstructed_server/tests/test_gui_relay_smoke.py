from __future__ import annotations

from pathlib import Path
import sys
import unittest
import argparse


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import gui_relay_smoke  # noqa: E402


class GuiRelaySmokeTests(unittest.TestCase):
    def test_scenario_player_count_reads_legacy_prefix(self) -> None:
        self.assertEqual(gui_relay_smoke.scenario_player_count("(4) Tower.trk"), 4)
        self.assertEqual(gui_relay_smoke.scenario_player_count("Tower.trk"), 0)

    def test_select_scenario_prefers_minimum_supported_player_count(self) -> None:
        items = ["notes.txt", "(2) Chaos.trk", "(4) Cross.trk", "(8) Arena.trk"]

        self.assertEqual(gui_relay_smoke.select_scenario_index(items, 3), 2)

    def test_select_scenario_falls_back_to_first_trk(self) -> None:
        items = ["notes.txt", "(2) Chaos.trk", "(4) Cross.trk"]

        self.assertEqual(gui_relay_smoke.select_scenario_index(items, 8), 1)

    def test_relay_log_summary_counts_expected_members(self) -> None:
        log_text = """\
[rebuild] wizardnet relay configured game=8 member=1 host=yes
[rebuild] wizardnet relay browser join accepted game=8 member=2
[rebuild] wizardnet relay configured game=8 member=2 host=no
[rebuild] wizardnet relay browser join accepted game=8 member=3
[rebuild] wizardnet relay configured game=8 member=3 host=no
[rebuild] wizardnet relay link received phase=browser game=8 member=1 bytes=744 count=1
[rebuild] wizardnet relay link received phase=link game=8 member=2 bytes=20 count=1
[rebuild] wizardnet relay link received phase=link game=8 member=3 bytes=20 count=2
[rebuild] wizardnet relay frame queued game=8 target=2 stream=0 bytes=744 wire_bytes=772 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay frame queued game=8 target=1 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay frame queued game=8 target=2 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay frame queued game=8 target=3 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received game=8 member=1 bytes=36 queued=36 count=1
[rebuild] wizardnet relay mode1 received game=8 member=2 bytes=36 queued=36 count=1
[rebuild] wizardnet relay mode1 received game=8 member=3 bytes=36 queued=36 count=1
"""

        result = gui_relay_smoke.relay_log_summary(log_text, "RoomA", 3)

        self.assertTrue(result["checks"]["expected_configured_members"])
        self.assertTrue(result["checks"]["expected_joined_members"])
        self.assertTrue(result["checks"]["expected_link_received_members"])
        self.assertTrue(result["checks"]["link_frame_queued"])
        self.assertTrue(result["checks"]["relay_payload_encrypted"])
        self.assertTrue(result["checks"]["expected_mode1_members"])
        self.assertTrue(result["checks"]["expected_mode1_targets"])
        self.assertEqual(result["configured_members"], [1, 2, 3])
        self.assertEqual(result["link_received_members"], [1, 2, 3])

    def test_server_summary_args_default_members_match_joiner_count(self) -> None:
        args = argparse.Namespace(
            server_log=Path("server.err.log"),
            joiner_count=2,
            start_game=True,
            server_min_members=None,
            server_min_distinct_peer_hosts=0,
            server_min_distinct_peer_endpoints=None,
            server_min_bidirectional_mode1_members=None,
            server_min_link_frames=1,
            server_min_mode1_frames=1,
            server_min_deliveries=1,
            server_max_no_target=0,
            server_max_invalid_stream=0,
            server_max_invalid_payload=0,
            server_summary_tail_bytes=1024,
        )

        result = gui_relay_smoke.server_summary_args(args, "RoomA")

        self.assertEqual(result.min_members, 3)
        self.assertEqual(result.min_distinct_peer_endpoints, 3)
        self.assertEqual(result.min_bidirectional_mode1_members, 3)


if __name__ == "__main__":
    unittest.main()
