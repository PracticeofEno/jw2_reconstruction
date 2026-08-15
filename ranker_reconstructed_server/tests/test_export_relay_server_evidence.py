from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import export_relay_server_evidence  # noqa: E402


class ExportRelayServerEvidenceTests(unittest.TestCase):
    def test_build_evidence_wraps_validated_summary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = base / "server.err.log"
            log.write_text(
                "2026-08-14 INFO ranker_server: relay summary game=7 name=RoomA "
                "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
                "deliveries=12 no_target=0 invalid_stream=0 members=2 "
                "distinct_peer_hosts=2 "
                "member_peers=1@198.51.100.1:5000,2@203.0.113.2:5001 "
                "member_frames=1:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4;"
                "2:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4\n",
                encoding="utf-8",
            )
            args = argparse.Namespace(
                log=log,
                room="RoomA",
                game_id=None,
                min_link_frames=1,
                min_mode1_frames=1,
                min_deliveries=1,
                min_members=2,
                min_distinct_peer_hosts=2,
                min_distinct_peer_endpoints=2,
                min_bidirectional_mode1_members=2,
                max_no_target=0,
                max_invalid_stream=0,
                max_invalid_payload=0,
                tail_bytes=0,
                show_lines=5,
            )

            evidence = export_relay_server_evidence.build_evidence(args)

            self.assertTrue(evidence["ok"])
            self.assertEqual(evidence["room"], "RoomA")
            self.assertEqual(
                evidence["server_summary"]["summary"]["bidirectional_mode1_members"],
                2,
            )

    def test_main_writes_output_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            log = base / "server.err.log"
            output = base / "evidence.json"
            log.write_text(
                "2026-08-14 INFO ranker_server: relay summary game=8 name=RoomB "
                "link_frames=2 link_bytes=20 mode1_frames=2 mode1_bytes=72 "
                "deliveries=2 no_target=0 invalid_stream=0 members=2 "
                "distinct_peer_hosts=1 "
                "member_peers=1@198.51.100.1:5000,2@198.51.100.1:5001 "
                "member_frames=1:link_tx=1:link_rx=1:mode1_tx=1:mode1_rx=1;"
                "2:link_tx=1:link_rx=1:mode1_tx=1:mode1_rx=1\n",
                encoding="utf-8",
            )
            argv = sys.argv
            try:
                sys.argv = [
                    "export_relay_server_evidence.py",
                    str(log),
                    "--room",
                    "RoomB",
                    "--output",
                    str(output),
                ]
                exit_code = export_relay_server_evidence.main()
            finally:
                sys.argv = argv

            self.assertEqual(exit_code, 0)
            written = json.loads(output.read_text(encoding="utf-8"))
            self.assertTrue(written["ok"])
            self.assertEqual(written["server_summary"]["summary"]["game_id"], 8)


if __name__ == "__main__":
    unittest.main()
