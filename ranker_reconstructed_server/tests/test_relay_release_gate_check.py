from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import relay_release_gate_check  # noqa: E402


HOST_LOG = """\
[rebuild] wizardnet relay configured game=7 member=1 host=yes
[rebuild] wizardnet relay host configured game=7 room=RoomA member=1
[rebuild] link relay route initialized game=7 local_member=1 host=yes local_slot=0
[rebuild] wizardnet relay frame queued game=7 target=2 stream=0 bytes=744 wire_bytes=772 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay link received phase=link game=7 member=2 bytes=20 count=1
[rebuild] link countdown timer set value=6 timer=1
[rebuild] link countdown complete calling start_game
[rebuild] wizardnet relay frame queued game=7 target=2 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received game=7 member=2 bytes=36 queued=36 count=1
"""

JOINER_LOG = """\
[rebuild] wizardnet relay browser join queued game=7 room=RoomA bytes=70
[rebuild] wizardnet relay browser join accepted game=7 member=2
[rebuild] wizardnet relay configured game=7 member=2 host=no
[rebuild] link relay route initialized game=7 local_member=2 host=no local_slot=1
[rebuild] link relay join accepted slot=1 local_member=2 peer_socket=1879048193
[rebuild] wizardnet relay frame queued game=7 target=1 stream=0 bytes=20 wire_bytes=48 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay link received phase=browser game=7 member=1 bytes=744 count=1
[rebuild] link countdown timer set value=6 timer=1
[rebuild] link countdown complete calling start_game
[rebuild] wizardnet relay frame queued game=7 target=1 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received game=7 member=1 bytes=36 queued=36 count=1
"""


BAD_LATE_HOST_LOG = """\
[rebuild] wizardnet relay configured game=8 member=1 host=yes
[rebuild] link relay route initialized game=8 local_member=1 host=yes local_slot=0
[rebuild] wizardnet relay frame queued game=8 target=2 stream=0 bytes=744 wire_bytes=772 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay link received phase=link game=8 member=2 bytes=20 count=1
[rebuild] link countdown timer set value=6 timer=1
[rebuild] link countdown complete calling start_game
[rebuild] wizardnet relay frame queued game=8 target=2 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received ignored game=8 member=2 bytes=36 queued=36 count=1
"""


BAD_LATE_JOINER_LOG = """\
[rebuild] wizardnet relay browser join queued game=8 room=RoomA bytes=70
[rebuild] wizardnet relay browser join accepted game=8 member=2
[rebuild] wizardnet relay configured game=8 member=2 host=no
[rebuild] link relay route initialized game=8 local_member=2 host=no local_slot=1
[rebuild] link relay join accepted slot=1 local_member=2 peer_socket=1879048193
[rebuild] wizardnet relay frame queued game=8 target=1 stream=0 bytes=20 wire_bytes=48 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay link received phase=browser game=8 member=1 bytes=744 count=1
[rebuild] link countdown timer set value=6 timer=1
[rebuild] link countdown complete calling start_game
[rebuild] wizardnet relay frame queued game=8 target=1 stream=1 bytes=36 wire_bytes=64 crypto=yes crypto_key=room count=1
[rebuild] wizardnet relay mode1 received ignored game=8 member=1 bytes=36 queued=36 count=1
"""


def server_log(
    *,
    invalid_stream: int = 0,
    invalid_payload: int = 0,
    distinct_peer_hosts: int | None = None,
    room: str = "RoomA",
) -> str:
    extra = ""
    if distinct_peer_hosts is not None:
        extra = (
            " members=2 "
            f"distinct_peer_hosts={distinct_peer_hosts} "
            "member_peers=1@198.51.100.1:5000,2@203.0.113.2:5001 "
            "member_frames=1:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4;"
            "2:link_tx=2:link_rx=2:mode1_tx=4:mode1_rx=4"
        )
    return (
        f"2026-08-14 INFO ranker_server: relay summary game=7 name={room} "
        "link_frames=4 link_bytes=100 mode1_frames=8 mode1_bytes=288 "
        f"deliveries=12 no_target=0 invalid_stream={invalid_stream} "
        f"invalid_payload={invalid_payload}{extra}\n"
    )


class RelayReleaseGateCheckTests(unittest.TestCase):
    def test_cli_defaults_are_two_pc_release_strict(self) -> None:
        parser = relay_release_gate_check.build_arg_parser()

        args = parser.parse_args(
            [
                "--host-log",
                "host.log",
                "--joiner-log",
                "joiner.log",
                "--server-log",
                "server.err.log",
                "--room",
                "RoomA",
            ]
        )

        self.assertTrue(args.require_client_reports)
        self.assertTrue(args.require_no_radmin)
        self.assertTrue(args.require_distinct_client_machines)
        self.assertTrue(args.require_distinct_client_networks)
        self.assertEqual(args.min_members, 2)
        self.assertEqual(args.min_distinct_peer_endpoints, 2)
        self.assertEqual(args.min_bidirectional_mode1_members, 2)

    def test_cli_allow_flags_relax_lab_checks_intentionally(self) -> None:
        parser = relay_release_gate_check.build_arg_parser()

        args = parser.parse_args(
            [
                "--host-log",
                "host.log",
                "--joiner-log",
                "joiner.log",
                "--server-log",
                "server.err.log",
                "--room",
                "RoomA",
                "--allow-missing-client-reports",
                "--allow-radmin",
                "--allow-same-machine",
                "--allow-same-network",
                "--min-members",
                "0",
                "--min-distinct-peer-endpoints",
                "0",
                "--min-bidirectional-mode1-members",
                "0",
            ]
        )

        self.assertFalse(args.require_client_reports)
        self.assertFalse(args.require_no_radmin)
        self.assertFalse(args.require_distinct_client_machines)
        self.assertFalse(args.require_distinct_client_networks)
        self.assertEqual(args.min_members, 0)
        self.assertEqual(args.min_distinct_peer_endpoints, 0)
        self.assertEqual(args.min_bidirectional_mode1_members, 0)

    def check_sample(
        self,
        *,
        host_text: str = HOST_LOG,
        joiner_text: str = JOINER_LOG,
        invalid_stream: int = 0,
        invalid_payload: int = 0,
        distinct_peer_hosts: int | None = None,
        host_client_report: dict[str, object] | None = None,
        joiner_client_report: dict[str, object] | None = None,
        use_server_evidence: bool = False,
        use_server_evidence_dir: bool = False,
        server_room: str = "RoomA",
        evidence_room: str | None = None,
        evidence_min_distinct_peer_endpoints: int | None = None,
        evidence_min_bidirectional_mode1_members: int | None = None,
        require_client_reports: bool = False,
        require_no_radmin: bool = False,
        require_distinct_client_machines: bool = False,
        require_distinct_client_networks: bool = False,
        allow_different_exe_hash: bool = False,
        min_distinct_peer_endpoints: int = 0,
        min_bidirectional_mode1_members: int = 0,
    ) -> dict[str, object]:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            host_log = base / "host.log"
            joiner_log = base / "joiner.log"
            server = base / "server.err.log"
            host_report_path = base / "host.json"
            joiner_report_path = base / "joiner.json"
            host_log.write_text(host_text, encoding="utf-8")
            joiner_log.write_text(joiner_text, encoding="utf-8")
            server.write_text(
                server_log(
                    invalid_stream=invalid_stream,
                    invalid_payload=invalid_payload,
                    distinct_peer_hosts=distinct_peer_hosts,
                    room=server_room,
                ),
                encoding="utf-8",
            )
            server_evidence_path = base / "server_evidence.json"
            server_log_path: Path | None = server
            server_evidence_dir_path: Path | None = None
            if use_server_evidence:
                summary_args = argparse.Namespace(
                    log=server,
                    room=evidence_room or "RoomA",
                    game_id=None,
                    min_link_frames=1,
                    min_mode1_frames=1,
                    min_deliveries=1,
                    min_members=0,
                    min_distinct_peer_hosts=0,
                    min_distinct_peer_endpoints=(
                        min_distinct_peer_endpoints
                        if evidence_min_distinct_peer_endpoints is None
                        else evidence_min_distinct_peer_endpoints
                    ),
                    min_bidirectional_mode1_members=(
                        min_bidirectional_mode1_members
                        if evidence_min_bidirectional_mode1_members is None
                        else evidence_min_bidirectional_mode1_members
                    ),
                    max_no_target=0,
                    max_invalid_stream=0,
                    max_invalid_payload=0,
                    tail_bytes=0,
                    show_lines=5,
                )
                summary = relay_release_gate_check.relay_server_summary_check.check_summary(
                    summary_args
                )
                server_evidence_path.write_text(
                    json.dumps(
                        {
                            "ok": summary["ok"],
                            "server_summary": summary,
                        }
                    ),
                    encoding="utf-8",
                )
                server_log_path = None
            if use_server_evidence_dir:
                if not use_server_evidence:
                    summary_args = argparse.Namespace(
                        log=server,
                        room=evidence_room or "RoomA",
                        game_id=None,
                        min_link_frames=1,
                        min_mode1_frames=1,
                        min_deliveries=1,
                        min_members=0,
                        min_distinct_peer_hosts=0,
                        min_distinct_peer_endpoints=(
                            min_distinct_peer_endpoints
                            if evidence_min_distinct_peer_endpoints is None
                            else evidence_min_distinct_peer_endpoints
                        ),
                        min_bidirectional_mode1_members=(
                            min_bidirectional_mode1_members
                            if evidence_min_bidirectional_mode1_members is None
                            else evidence_min_bidirectional_mode1_members
                        ),
                        max_no_target=0,
                        max_invalid_stream=0,
                        max_invalid_payload=0,
                        tail_bytes=0,
                        show_lines=5,
                    )
                    summary = relay_release_gate_check.relay_server_summary_check.check_summary(
                        summary_args
                    )
                    server_evidence_path.write_text(
                        json.dumps(
                            {
                                "ok": summary["ok"],
                                "room": summary["summary"]["room"],
                                "game_id": summary["summary"]["game_id"],
                                "server_summary": summary,
                            }
                        ),
                        encoding="utf-8",
                    )
                server_evidence_dir_path = base / "server_evidence"
                server_evidence_dir_path.mkdir()
                (server_evidence_dir_path / "relay_old_game99_OtherRoom.json").write_text(
                    json.dumps({"ok": False, "room": "OtherRoom", "game_id": 99}),
                    encoding="utf-8",
                )
                (server_evidence_dir_path / "relay_new_game7_RoomA.json").write_text(
                    server_evidence_path.read_text(encoding="utf-8"),
                    encoding="utf-8",
                )
                (
                    server_evidence_dir_path / "newer_client_report_RoomA.json"
                ).write_text(
                    json.dumps(
                        {
                            "ok": True,
                            "role": "host",
                            "room": "RoomA",
                            "game_id": 7,
                            "log": {"ok": True},
                        }
                    ),
                    encoding="utf-8",
                )
                server_log_path = None
            if host_client_report is not None:
                host_report_path.write_text(
                    json.dumps(host_client_report), encoding="utf-8"
                )
            if joiner_client_report is not None:
                joiner_report_path.write_text(
                    json.dumps(joiner_client_report), encoding="utf-8"
                )
            args = argparse.Namespace(
                host_log=host_log,
                joiner_log=joiner_log,
                server_log=server_log_path,
                server_evidence=(
                    server_evidence_path if use_server_evidence else None
                ),
                server_evidence_dir=server_evidence_dir_path,
                host_client_report=(
                    host_report_path if host_client_report is not None else None
                ),
                joiner_client_report=(
                    joiner_report_path if joiner_client_report is not None else None
                ),
                room="RoomA",
                server_host="115.22.136.89",
                server_port=19777,
                game_id=None,
                require_client_reports=require_client_reports,
                require_no_radmin=require_no_radmin,
                require_distinct_client_machines=require_distinct_client_machines,
                require_distinct_client_networks=require_distinct_client_networks,
                allow_different_exe_hash=allow_different_exe_hash,
                require_mode1=True,
                forbid_direct_transport=True,
                min_link_frames=1,
                min_mode1_frames=1,
                min_deliveries=1,
                min_members=0,
                min_distinct_peer_hosts=0,
                min_distinct_peer_endpoints=min_distinct_peer_endpoints,
                min_bidirectional_mode1_members=min_bidirectional_mode1_members,
                max_no_target=0,
                max_invalid_stream=0,
                max_invalid_payload=0,
                tail_bytes=0,
                show_lines=80,
                server_show_lines=5,
            )
            return relay_release_gate_check.check_release_gate(args)

    def test_release_gate_passes_when_all_sections_pass(self) -> None:
        result = self.check_sample()

        self.assertTrue(result["ok"])
        self.assertEqual(result["failing_sections"], [])
        self.assertTrue(result["host"]["ok"])
        self.assertTrue(result["joiner"]["ok"])
        self.assertTrue(result["server_summary"]["ok"])
        self.assertEqual(result["effective_game_id"], 7)

    def test_release_gate_accepts_exported_server_evidence(self) -> None:
        result = self.check_sample(
            distinct_peer_hosts=2,
            use_server_evidence=True,
            min_distinct_peer_endpoints=2,
            min_bidirectional_mode1_members=2,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["server_summary"]["summary"]["game_id"], 7)

    def test_release_gate_accepts_latest_matching_server_evidence_dir(self) -> None:
        result = self.check_sample(
            distinct_peer_hosts=2,
            use_server_evidence_dir=True,
            min_distinct_peer_endpoints=2,
            min_bidirectional_mode1_members=2,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["server_summary"]["summary"]["game_id"], 7)
        self.assertEqual(result["server_summary"]["candidate_count"], 1)
        self.assertTrue(result["server_summary"]["evidence_path"].endswith(".json"))

    def test_release_gate_fails_when_server_evidence_dir_has_no_room(self) -> None:
        result = self.check_sample(
            server_room="RoomB",
            evidence_room="RoomB",
            use_server_evidence_dir=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("server_summary", result["failing_sections"])
        self.assertIn(
            "server_evidence_dir_has_matching_room",
            result["server_summary"]["missing_checks"],
        )

    def test_release_gate_revalidates_exported_server_evidence_criteria(self) -> None:
        result = self.check_sample(
            use_server_evidence=True,
            min_distinct_peer_endpoints=2,
            evidence_min_distinct_peer_endpoints=0,
        )

        self.assertFalse(result["ok"])
        self.assertIn("server_summary", result["failing_sections"])
        self.assertIn(
            "distinct_peer_endpoints_min",
            result["server_summary"]["missing_checks"],
        )
        self.assertTrue(result["server_summary"]["checks"]["server_evidence_ok"])

    def test_release_gate_rejects_server_evidence_for_different_room(self) -> None:
        result = self.check_sample(
            use_server_evidence=True,
            server_room="RoomB",
            evidence_room="RoomB",
        )

        self.assertFalse(result["ok"])
        self.assertIn("server_summary", result["failing_sections"])
        self.assertIn("room_matches", result["server_summary"]["missing_checks"])

    def test_server_game_id_scopes_client_logs_to_matching_session(self) -> None:
        result = self.check_sample(
            host_text=HOST_LOG + BAD_LATE_HOST_LOG,
            joiner_text=JOINER_LOG + BAD_LATE_JOINER_LOG,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["effective_game_id"], 7)
        self.assertEqual(result["host"]["scope"], "room_RoomA_game_7_host")
        self.assertEqual(result["joiner"]["scope"], "room_RoomA_game_7_joiner")

    def test_release_gate_fails_when_client_log_lacks_server_game_id(self) -> None:
        result = self.check_sample(host_text=HOST_LOG.replace("game=7", "game=8"))

        self.assertFalse(result["ok"])
        self.assertEqual(result["failing_sections"], ["host"])
        self.assertEqual(result["host"]["scope"], "game_7_missing")

    def test_release_gate_fails_when_server_summary_fails(self) -> None:
        result = self.check_sample(invalid_stream=1)

        self.assertFalse(result["ok"])
        self.assertEqual(result["failing_sections"], ["server_summary"])

    def test_release_gate_fails_when_server_saw_invalid_payload(self) -> None:
        result = self.check_sample(invalid_payload=1)

        self.assertFalse(result["ok"])
        self.assertEqual(result["failing_sections"], ["server_summary"])
        self.assertIn(
            "invalid_payload_max", result["server_summary"]["missing_checks"]
        )

    def test_summary_text_lists_failing_sections_and_missing_checks(self) -> None:
        result = self.check_sample(invalid_stream=1)

        summary = relay_release_gate_check.format_release_gate_summary(result)

        self.assertIn("WizardNet relay final gate: FAIL", summary)
        self.assertIn("failing_sections=server_summary", summary)
        self.assertIn("- server_summary: FAIL", summary)
        self.assertIn("invalid_stream_max", summary)

    def test_write_text_output_creates_parent_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "nested" / "summary.txt"

            relay_release_gate_check.write_text_output(output_path, "summary\n")

            self.assertEqual(output_path.read_text(encoding="utf-8"), "summary\n")

    def test_release_gate_requires_room_marker_in_joiner_log(self) -> None:
        result = self.check_sample(
            joiner_text=JOINER_LOG.replace(" room=RoomA", "")
        )

        self.assertFalse(result["ok"])
        self.assertEqual(result["failing_sections"], ["joiner"])
        self.assertIn("room_seen", result["joiner"]["missing_checks"])

    def test_release_gate_accepts_client_network_and_no_radmin_reports(self) -> None:
        result = self.check_sample(
            host_client_report=client_report("host"),
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
            require_no_radmin=True,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["host_client_report"]["missing_checks"], [])
        self.assertEqual(result["joiner_client_report"]["missing_checks"], [])
        self.assertEqual(result["client_deployment_pair"]["missing_checks"], [])

    def test_release_gate_fails_when_required_client_report_is_missing(self) -> None:
        result = self.check_sample(
            host_client_report=client_report("host"),
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("joiner_client_report", result["failing_sections"])

    def test_release_gate_fails_when_client_report_has_active_radmin(self) -> None:
        report = client_report("host")
        report["environment"]["checks"]["no_active_radmin_adapters"] = False

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
            require_no_radmin=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])

    def test_release_gate_fails_when_client_report_uses_forbidden_default_route(
        self,
    ) -> None:
        report = client_report("host")
        report["environment"]["checks"][
            "no_forbidden_default_gateway_interfaces"
        ] = False

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
            require_no_radmin=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn(
            "no_forbidden_default_gateway_interfaces",
            result["host_client_report"]["missing_checks"],
        )

    def test_release_gate_fails_when_client_report_lacks_live_network(self) -> None:
        report = client_report("host")
        report.pop("network")

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn("network_present", result["host_client_report"]["missing_checks"])

    def test_release_gate_fails_when_client_report_has_multiple_processes(self) -> None:
        report = client_report("host")
        report["network"]["checks"]["single_live_process"] = False

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn(
            "single_live_process",
            result["host_client_report"]["missing_checks"],
        )

    def test_release_gate_fails_when_environment_query_failed(self) -> None:
        report = client_report("host")
        report["environment"]["checks"]["environment_queries_ok"] = False

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
            require_no_radmin=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn(
            "environment_queries_ok",
            result["host_client_report"]["missing_checks"],
        )

    def test_release_gate_fails_when_client_report_server_differs(self) -> None:
        report = client_report("host")
        report["server"] = "198.51.100.4:19777"

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn("server_matches", result["host_client_report"]["missing_checks"])

    def test_release_gate_fails_when_deployment_ini_server_mismatches(self) -> None:
        report = client_report("host")
        report["deployment"]["ok"] = False
        report["deployment"]["checks"]["ini_server_matches"] = False

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn(
            "deployment_ini_server_matches",
            result["host_client_report"]["missing_checks"],
        )

    def test_release_gate_fails_when_client_reports_use_different_exe_hashes(self) -> None:
        joiner_report = client_report("joiner")
        joiner_report["deployment"]["exe"]["sha256"] = "BBBB"

        result = self.check_sample(
            host_client_report=client_report("host"),
            joiner_client_report=joiner_report,
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("client_deployment_pair", result["failing_sections"])
        self.assertIn(
            "exe_hashes_match",
            result["client_deployment_pair"]["missing_checks"],
        )

    def test_release_gate_can_allow_different_exe_hashes(self) -> None:
        joiner_report = client_report("joiner")
        joiner_report["deployment"]["exe"]["sha256"] = "BBBB"

        result = self.check_sample(
            host_client_report=client_report("host"),
            joiner_client_report=joiner_report,
            require_client_reports=True,
            allow_different_exe_hash=True,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["client_deployment_pair"]["missing_checks"], [])

    def test_release_gate_fails_when_client_report_game_id_differs(self) -> None:
        report = client_report("host")
        report["effective_game_id"] = 8

        result = self.check_sample(
            host_client_report=report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("host_client_report", result["failing_sections"])
        self.assertIn("game_id_matches", result["host_client_report"]["missing_checks"])

    def test_release_gate_accepts_distinct_client_machines(self) -> None:
        result = self.check_sample(
            host_client_report=client_report(
                "host", hostname="same-name", machine_identity="AAAA"
            ),
            joiner_client_report=client_report(
                "joiner", hostname="same-name", machine_identity="BBBB"
            ),
            require_client_reports=True,
            require_distinct_client_machines=True,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["client_pair"]["missing_checks"], [])

    def test_release_gate_fails_when_client_reports_share_machine_identity(self) -> None:
        result = self.check_sample(
            host_client_report=client_report(
                "host", hostname="pc-a", machine_identity="SAME"
            ),
            joiner_client_report=client_report(
                "joiner", hostname="pc-b", machine_identity="SAME"
            ),
            require_client_reports=True,
            require_distinct_client_machines=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("client_pair", result["failing_sections"])
        self.assertIn(
            "machine_identities_distinct", result["client_pair"]["missing_checks"]
        )

    def test_release_gate_fails_when_machine_identity_is_missing(self) -> None:
        host_report = client_report("host")
        del host_report["environment"]["machine"]["identity"]

        result = self.check_sample(
            host_client_report=host_report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
            require_distinct_client_machines=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("client_pair", result["failing_sections"])
        self.assertIn(
            "host_machine_identity_present",
            result["client_pair"]["missing_checks"],
        )

    def test_release_gate_fails_when_machine_identity_query_failed(self) -> None:
        host_report = client_report("host")
        host_report["environment"]["checks"]["machine_identity_query_ok"] = False

        result = self.check_sample(
            host_client_report=host_report,
            joiner_client_report=client_report("joiner"),
            require_client_reports=True,
            require_distinct_client_machines=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("client_pair", result["failing_sections"])
        self.assertIn(
            "host_machine_identity_query_ok",
            result["client_pair"]["missing_checks"],
        )

    def test_release_gate_accepts_distinct_public_peer_hosts(self) -> None:
        result = self.check_sample(
            distinct_peer_hosts=2,
            host_client_report=client_report("host", lan_gateway="192.168.0.1"),
            joiner_client_report=client_report("joiner", lan_gateway="192.168.0.1"),
            require_client_reports=True,
            require_distinct_client_networks=True,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["client_network_pair"]["missing_checks"], [])

    def test_release_gate_can_require_bidirectional_mode1_members(self) -> None:
        result = self.check_sample(
            distinct_peer_hosts=2,
            min_distinct_peer_endpoints=2,
            min_bidirectional_mode1_members=2,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(
            result["server_summary"]["summary"]["distinct_peer_endpoints"], 2
        )
        self.assertEqual(
            result["server_summary"]["summary"]["bidirectional_mode1_members"], 2
        )

    def test_release_gate_fails_when_distinct_peer_endpoints_are_unproven(self) -> None:
        result = self.check_sample(min_distinct_peer_endpoints=2)

        self.assertFalse(result["ok"])
        self.assertIn("server_summary", result["failing_sections"])
        self.assertIn(
            "distinct_peer_endpoints_min",
            result["server_summary"]["missing_checks"],
        )

    def test_release_gate_fails_when_bidirectional_mode1_is_unproven(self) -> None:
        result = self.check_sample(min_bidirectional_mode1_members=2)

        self.assertFalse(result["ok"])
        self.assertIn("server_summary", result["failing_sections"])
        self.assertIn(
            "bidirectional_mode1_members_min",
            result["server_summary"]["missing_checks"],
        )

    def test_release_gate_accepts_distinct_lan_fingerprints(self) -> None:
        result = self.check_sample(
            distinct_peer_hosts=1,
            host_client_report=client_report("host", lan_gateway="192.168.0.1"),
            joiner_client_report=client_report("joiner", lan_gateway="192.168.1.1"),
            require_client_reports=True,
            require_distinct_client_networks=True,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["client_network_pair"]["missing_checks"], [])

    def test_release_gate_accepts_same_gateway_ip_with_distinct_gateway_macs(
        self,
    ) -> None:
        result = self.check_sample(
            distinct_peer_hosts=1,
            host_client_report=client_report(
                "host",
                lan_gateway="192.168.0.1",
                gateway_mac="00-11-22-33-44-55",
            ),
            joiner_client_report=client_report(
                "joiner",
                lan_gateway="192.168.0.1",
                gateway_mac="66-77-88-99-AA-BB",
            ),
            require_client_reports=True,
            require_distinct_client_networks=True,
        )

        self.assertTrue(result["ok"])
        self.assertEqual(result["client_network_pair"]["missing_checks"], [])

    def test_release_gate_rejects_same_gateway_ip_when_gateway_macs_missing(
        self,
    ) -> None:
        result = self.check_sample(
            distinct_peer_hosts=1,
            host_client_report=client_report(
                "host",
                lan_gateway="192.168.0.1",
                gateway_mac="",
            ),
            joiner_client_report=client_report(
                "joiner",
                lan_gateway="192.168.0.1",
                gateway_mac="",
            ),
            require_client_reports=True,
            require_distinct_client_networks=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("client_network_pair", result["failing_sections"])
        self.assertIn(
            "public_or_lan_networks_distinct",
            result["client_network_pair"]["missing_checks"],
        )

    def test_distinct_lan_check_ignores_shared_virtual_networks(self) -> None:
        host_report = client_report("host", lan_gateway="192.168.0.1")
        joiner_report = client_report("joiner", lan_gateway="192.168.1.1")
        host_report["environment"]["machine"]["local_ipv4_config"].append(
            {
                "IPv4Address": "172.26.240.1",
                "PrefixLength": 20,
                "IPv4DefaultGateway": None,
                "GatewayMac": None,
            }
        )
        joiner_report["environment"]["machine"]["local_ipv4_config"].append(
            {
                "IPv4Address": "172.26.240.1",
                "PrefixLength": 20,
                "IPv4DefaultGateway": None,
                "GatewayMac": None,
            }
        )

        result = self.check_sample(
            distinct_peer_hosts=1,
            host_client_report=host_report,
            joiner_client_report=joiner_report,
            require_client_reports=True,
            require_distinct_client_networks=True,
        )

        self.assertTrue(result["ok"])

    def test_release_gate_fails_when_network_separation_is_unproven(self) -> None:
        result = self.check_sample(
            distinct_peer_hosts=1,
            host_client_report=client_report("host", lan_gateway="192.168.0.1"),
            joiner_client_report=client_report("joiner", lan_gateway="192.168.0.1"),
            require_client_reports=True,
            require_distinct_client_networks=True,
        )

        self.assertFalse(result["ok"])
        self.assertIn("client_network_pair", result["failing_sections"])
        self.assertIn(
            "public_or_lan_networks_distinct",
            result["client_network_pair"]["missing_checks"],
        )


def client_report(
    role: str,
    hostname: str | None = None,
    machine_identity: str | None = None,
    lan_gateway: str = "192.168.0.1",
    gateway_mac: str = "00-11-22-33-44-55",
) -> dict[str, object]:
    return {
        "ok": True,
        "captured_utc": "2026-08-14T00:00:00+00:00",
        "role": role,
        "room": "RoomA",
        "game_id": None,
        "effective_game_id": 7,
        "server": "115.22.136.89:19777",
        "network": {
            "checks": {
                "live_process_present": True,
                "single_live_process": True,
                "server_tcp_connected": True,
                "one_server_tcp_per_process": True,
                "only_server_tcp_established": True,
                "no_tcp_listeners": True,
                "no_udp_endpoints": True,
            }
        },
        "environment": {
            "machine": {
                "hostname": hostname or f"{role}-pc",
                "identity": {
                    "fingerprint_sha256": machine_identity or f"{role.upper()}ID",
                    "source_count": 1,
                    "sources": [
                        {
                            "name": "machine_guid",
                            "present": True,
                            "sha256": machine_identity or f"{role.upper()}SOURCE",
                        }
                    ],
                },
                "local_ipv4_config": [
                    {
                        "IPv4Address": "192.168.0.20",
                        "PrefixLength": 24,
                        "IPv4DefaultGateway": lan_gateway,
                        "GatewayMac": gateway_mac,
                    }
                ],
            },
            "checks": {
                "environment_queries_ok": True,
                "machine_identity_query_ok": True,
                "machine_identity_present": True,
                "no_radmin_processes": True,
                "no_active_radmin_adapters": True,
                "no_running_radmin_services": True,
                "no_forbidden_default_gateway_interfaces": True,
            },
        },
        "deployment": {
            "ok": True,
            "exe": {
                "path": r"C:\Ranker\ranker_rebuild.exe",
                "exists": True,
                "sha256": "AAAA",
            },
            "ini": {
                "path": r"C:\Ranker\ranker_client.ini",
                "exists": True,
            },
            "ini_values": {
                "Address": "115.22.136.89",
                "Port": "19777",
                "PortNumber": 19777,
            },
            "expected_server": "115.22.136.89:19777",
            "checks": {
                "exe_exists": True,
                "exe_name_is_ranker_rebuild": True,
                "ini_exists": True,
                "ini_parse_ok": True,
                "ini_has_wizardnet_address": True,
                "ini_has_wizardnet_port": True,
                "ini_server_matches": True,
            },
            "missing_checks": [],
        },
    }


if __name__ == "__main__":
    unittest.main()
