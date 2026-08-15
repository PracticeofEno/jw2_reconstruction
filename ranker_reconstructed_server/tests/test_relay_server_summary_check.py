from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import relay_server_summary_check  # noqa: E402


class RelayServerSummaryCheckTests(unittest.TestCase):
    def check_sample(self, text: str, **overrides) -> dict[str, object]:
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "server.err.log"
            log.write_text(text, encoding="utf-8")
            args = argparse.Namespace(
                log=log,
                room=overrides.get("room", "RoomA"),
                game_id=overrides.get("game_id"),
                min_link_frames=overrides.get("min_link_frames", 1),
                min_mode1_frames=overrides.get("min_mode1_frames", 1),
                min_deliveries=overrides.get("min_deliveries", 1),
                min_members=overrides.get("min_members", 0),
                min_distinct_peer_hosts=overrides.get("min_distinct_peer_hosts", 0),
                min_distinct_peer_endpoints=overrides.get(
                    "min_distinct_peer_endpoints", 0
                ),
                min_bidirectional_mode1_members=overrides.get(
                    "min_bidirectional_mode1_members", 0
                ),
                max_no_target=overrides.get("max_no_target", 0),
                max_invalid_stream=overrides.get("max_invalid_stream", 0),
                max_invalid_payload=overrides.get("max_invalid_payload", 0),
                tail_bytes=0,
                show_lines=5,
            )
            return relay_server_summary_check.check_summary(args)

    def test_matching_summary_passes_release_gate_criteria(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0\n"
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["missing_checks"], [])
        self.assertEqual(result["summary"]["game_id"], 7)

    def test_invalid_stream_fails_by_default(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=1\n"
        )

        self.assertFalse(result["ok"])
        self.assertIn("invalid_stream_max", result["missing_checks"])

    def test_invalid_payload_fails_by_default(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0 invalid_payload=1\n"
        )

        self.assertFalse(result["ok"])
        self.assertIn("invalid_payload_max", result["missing_checks"])

    def test_legacy_summary_without_invalid_payload_defaults_to_zero(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0\n"
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["summary"]["invalid_payload"], 0)

    def test_latest_matching_summary_is_used(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=0 link_bytes=0 mode1_frames=0 mode1_bytes=0 "
            "deliveries=0 no_target=0 invalid_stream=0\n"
            "2026-08-14 INFO ranker_server: relay summary game=8 name=RoomA "
            "link_frames=2 link_bytes=20 mode1_frames=3 mode1_bytes=108 "
            "deliveries=5 no_target=0 invalid_stream=0\n"
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["summary"]["game_id"], 8)

    def test_extended_member_peer_summary_can_require_distinct_hosts(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0 members=2 "
            "distinct_peer_hosts=2 member_peers=1@198.51.100.1:5000,2@203.0.113.2:5001 "
            "member_frames=1:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4;"
            "2:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4\n",
            min_members=2,
            min_distinct_peer_hosts=2,
            min_bidirectional_mode1_members=2,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["summary"]["members"], 2)
        self.assertEqual(result["summary"]["distinct_peer_hosts"], 2)
        self.assertEqual(result["summary"]["distinct_peer_endpoints"], 2)
        self.assertEqual(
            result["summary"]["parsed_member_peers"][1]["endpoint"],
            "198.51.100.1:5000",
        )
        self.assertEqual(result["summary"]["bidirectional_mode1_members"], 2)

    def test_distinct_peer_endpoint_requirement_fails_for_reused_endpoint(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0 members=2 "
            "distinct_peer_hosts=1 member_peers=1@198.51.100.1:5000,2@198.51.100.1:5000 "
            "member_frames=1:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4;"
            "2:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4\n",
            min_distinct_peer_endpoints=2,
        )

        self.assertFalse(result["ok"])
        self.assertIn("distinct_peer_endpoints_min", result["missing_checks"])

    def test_bidirectional_mode1_requirement_fails_for_one_way_summary(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0 members=2 "
            "distinct_peer_hosts=2 member_peers=1@198.51.100.1:5000,2@203.0.113.2:5001 "
            "member_frames=1:link_tx=2:link_rx=2:mode1_tx=8:mode1_rx=0;"
            "2:link_tx=2:link_rx=2:mode1_tx=0:mode1_rx=8\n",
            min_bidirectional_mode1_members=2,
        )

        self.assertFalse(result["ok"])
        self.assertIn(
            "bidirectional_mode1_members_min", result["missing_checks"]
        )

    def test_distinct_host_requirement_fails_for_old_summary_format(self) -> None:
        result = self.check_sample(
            "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
            "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
            "deliveries=12 no_target=0 invalid_stream=0\n",
            min_distinct_peer_hosts=2,
        )

        self.assertFalse(result["ok"])
        self.assertIn("distinct_peer_hosts_min", result["missing_checks"])


if __name__ == "__main__":
    unittest.main()
