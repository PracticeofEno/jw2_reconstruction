from __future__ import annotations

import argparse
import tempfile
from pathlib import Path
import sys
import unittest


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import relay_client_check  # noqa: E402


def tcp_row(pid: int, remote_address: str = "203.0.113.7") -> dict[str, object]:
    return {
        "pid": pid,
        "local": f"10.0.0.{pid}:50000",
        "remote": f"{remote_address}:19777",
        "remote_address": remote_address,
        "remote_port": 19777,
        "state": "ESTABLISHED",
    }


class RelayClientCheckTests(unittest.TestCase):
    def test_log_args_include_game_id(self) -> None:
        args = argparse.Namespace(
            log=Path("Jw2.log"),
            role="host",
            room="RoomA",
            game_id=7,
            require_mode1=True,
            forbid_direct_transport=True,
            tail_bytes=1024,
            show_lines=20,
        )

        log_args = relay_client_check.build_log_args(args)

        self.assertEqual(log_args.game_id, 7)

    def test_network_check_requires_one_server_tcp_per_pid(self) -> None:
        result = self.check_network([tcp_row(100)], [100, 101])

        self.assertFalse(result["checks"]["one_server_tcp_per_process"])
        self.assertFalse(result["checks"]["single_live_process"])

    def test_network_check_records_multiple_live_processes(self) -> None:
        result = self.check_network([tcp_row(100), tcp_row(101)], [100, 101])

        self.assertTrue(result["checks"]["one_server_tcp_per_process"])
        self.assertFalse(result["checks"]["single_live_process"])
        self.assertTrue(result["checks"]["only_server_tcp_established"])

    def test_network_check_passes_for_one_live_process(self) -> None:
        result = self.check_network([tcp_row(100)], [100])

        self.assertTrue(result["checks"]["single_live_process"])
        self.assertTrue(result["checks"]["one_server_tcp_per_process"])
        self.assertTrue(result["checks"]["only_server_tcp_established"])

    def test_file_evidence_records_hash_and_ini_text(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            exe = base / "ranker_rebuild.exe"
            ini = base / "ranker_client.ini"
            exe.write_bytes(b"relay-exe")
            ini.write_text("[WizardNet]\nAddress=203.0.113.7\n", encoding="utf-8")

            exe_result = relay_client_check.file_evidence(exe)
            ini_result = relay_client_check.text_file_evidence(ini)

            self.assertTrue(exe_result["exists"])
            self.assertEqual(
                exe_result["sha256"],
                "3DA100F71440F571A4DEC87E4467A381F7AC747CA0121F28FD2D39CC7D0A3509",
            )
            self.assertIn("Address=203.0.113.7", ini_result["text"])

    def test_deployment_check_passes_for_matching_rebuild_exe_and_ini(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            exe = base / "ranker_rebuild.exe"
            ini = base / "ranker_client.ini"
            exe.write_bytes(b"relay-exe")
            ini.write_text(
                "[WizardNet]\nAddress=203.0.113.7\nPort=19777\n",
                encoding="utf-8",
            )

            result = relay_client_check.build_deployment_result(
                argparse.Namespace(
                    exe=exe,
                    ini=ini,
                    server_host="203.0.113.7",
                    server_port=19777,
                )
            )

            self.assertTrue(result["ok"])
            self.assertEqual(result["missing_checks"], [])
            self.assertEqual(result["ini_values"]["PortNumber"], 19777)
            self.assertNotIn("text", result["ini"])

    def test_deployment_check_fails_for_alternate_exe_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            exe = base / "ranker_rebuild_latest.exe"
            ini = base / "ranker_client.ini"
            exe.write_bytes(b"relay-exe")
            ini.write_text(
                "[WizardNet]\nAddress=203.0.113.7\nPort=19777\n",
                encoding="utf-8",
            )

            result = relay_client_check.build_deployment_result(
                argparse.Namespace(
                    exe=exe,
                    ini=ini,
                    server_host="203.0.113.7",
                    server_port=19777,
                )
            )

            self.assertFalse(result["ok"])
            self.assertIn("exe_name_is_ranker_rebuild", result["missing_checks"])

    def test_deployment_check_fails_for_wrong_ini_server(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            exe = base / "ranker_rebuild.exe"
            ini = base / "ranker_client.ini"
            exe.write_bytes(b"relay-exe")
            ini.write_text(
                "[WizardNet]\nAddress=198.51.100.4\nPort=19777\n",
                encoding="utf-8",
            )

            result = relay_client_check.build_deployment_result(
                argparse.Namespace(
                    exe=exe,
                    ini=ini,
                    server_host="203.0.113.7",
                    server_port=19777,
                )
            )

            self.assertFalse(result["ok"])
            self.assertIn("ini_server_matches", result["missing_checks"])

    def test_environment_check_flags_running_radmin_process(self) -> None:
        result = self.check_environment(
            processes=[{"image_name": "RvRvpnGui.exe", "pid": 33}],
            adapters=[],
            services=[],
        )

        self.assertFalse(result["checks"]["no_radmin_processes"])
        self.assertTrue(result["checks"]["no_active_radmin_adapters"])
        self.assertTrue(result["checks"]["no_running_radmin_services"])

    def test_environment_check_allows_inactive_radmin_install(self) -> None:
        result = self.check_environment(
            processes=[],
            adapters=[
                {
                    "Name": "Radmin VPN",
                    "InterfaceDescription": "Famatech Radmin VPN Ethernet Adapter",
                    "Status": "Disconnected",
                }
            ],
            services=[
                {
                    "Name": "RvControlSvc",
                    "DisplayName": "Radmin VPN Control Service",
                    "Status": "Stopped",
                }
            ],
        )

        self.assertTrue(result["checks"]["no_radmin_processes"])
        self.assertTrue(result["checks"]["no_active_radmin_adapters"])
        self.assertTrue(result["checks"]["no_running_radmin_services"])
        self.assertTrue(result["checks"]["machine_identity_present"])
        self.assertNotIn("machine-guid-a", json_dump(result))

    def test_environment_check_flags_active_radmin_adapter(self) -> None:
        result = self.check_environment(
            processes=[],
            adapters=[
                {
                    "Name": "Radmin VPN",
                    "InterfaceDescription": "Famatech Radmin VPN Ethernet Adapter",
                    "Status": "Up",
                }
            ],
            services=[],
        )

        self.assertFalse(result["checks"]["no_active_radmin_adapters"])

    def test_environment_check_flags_forbidden_default_gateway_interface(self) -> None:
        result = self.check_environment(
            processes=[],
            adapters=[],
            services=[],
            ip_configs=[
                {
                    "InterfaceAlias": "WireGuard Tunnel",
                    "InterfaceDescription": "WireGuard Tunnel Adapter",
                    "IPv4Address": "10.8.0.2",
                    "PrefixLength": 24,
                    "IPv4DefaultGateway": "10.8.0.1",
                    "GatewayMac": "00-11-22-33-44-55",
                }
            ],
        )

        self.assertFalse(
            result["checks"]["no_forbidden_default_gateway_interfaces"]
        )
        self.assertEqual(
            result["forbidden_default_gateway_interfaces"][0]["InterfaceAlias"],
            "WireGuard Tunnel",
        )

    def test_environment_check_flags_virtual_default_gateway_interface(self) -> None:
        result = self.check_environment(
            processes=[],
            adapters=[],
            services=[],
            ip_configs=[
                {
                    "InterfaceAlias": "vEthernet (Default Switch)",
                    "InterfaceDescription": "Hyper-V Virtual Ethernet Adapter",
                    "IPv4Address": "192.168.144.10",
                    "PrefixLength": 20,
                    "IPv4DefaultGateway": "192.168.144.1",
                    "GatewayMac": "00-15-5D-01-02-03",
                }
            ],
        )

        self.assertFalse(
            result["checks"]["no_forbidden_default_gateway_interfaces"]
        )
        self.assertEqual(
            result["forbidden_default_gateway_interfaces"][0][
                "InterfaceDescription"
            ],
            "Hyper-V Virtual Ethernet Adapter",
        )

    def test_machine_identity_ignores_generic_values(self) -> None:
        identity = relay_client_check.build_machine_identity(
            {
                "MachineGuid": "00000000-0000-0000-0000-000000000000",
                "ComputerSystemUuid": "To Be Filled By O.E.M.",
                "BiosSerialNumber": "System Serial Number",
            }
        )

        self.assertEqual(identity["fingerprint_sha256"], "")
        self.assertEqual(identity["source_count"], 0)

    def check_network(
        self, tcp_connections: list[dict[str, object]], pids: list[int]
    ) -> dict[str, object]:
        original_resolve = relay_client_check.resolve_server_addresses
        original_tcp = relay_client_check.tcp_connections_for_pids
        original_udp = relay_client_check.udp_endpoints_for_pids
        try:
            relay_client_check.resolve_server_addresses = lambda _: {"203.0.113.7"}
            relay_client_check.tcp_connections_for_pids = lambda _: tcp_connections
            relay_client_check.udp_endpoints_for_pids = lambda _: []
            args = argparse.Namespace(
                pid=pids,
                process_name="ranker_rebuild.exe",
                server_host="203.0.113.7",
                server_port=19777,
            )
            return relay_client_check.build_network_result(args)
        finally:
            relay_client_check.resolve_server_addresses = original_resolve
            relay_client_check.tcp_connections_for_pids = original_tcp
            relay_client_check.udp_endpoints_for_pids = original_udp

    def check_environment(
        self,
        *,
        processes: list[dict[str, object]],
        adapters: list[dict[str, object]],
        services: list[dict[str, object]],
        ip_configs: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        original_tasklist = relay_client_check.tasklist_rows
        original_adapters = relay_client_check.net_adapter_rows
        original_services = relay_client_check.service_rows
        original_ip_config = relay_client_check.ip_configuration_rows
        original_identity = relay_client_check.machine_identity_rows
        original_ipv4 = relay_client_check.local_ipv4_addresses
        try:
            relay_client_check.tasklist_rows = lambda: processes
            relay_client_check.net_adapter_rows = lambda: adapters
            relay_client_check.service_rows = lambda: services
            relay_client_check.ip_configuration_rows = lambda: ip_configs or []
            relay_client_check.machine_identity_rows = lambda: [
                {
                    "MachineGuid": "machine-guid-a",
                    "ComputerSystemUuid": "uuid-a",
                    "BiosSerialNumber": "serial-a",
                }
            ]
            relay_client_check.local_ipv4_addresses = lambda: ["10.0.0.5"]
            args = argparse.Namespace(
                radmin_marker=["radmin", "rvrvpn", "rvcontrol", "famatech"],
                forbidden_route_marker=relay_client_check.DEFAULT_FORBIDDEN_ROUTE_MARKERS,
            )
            return relay_client_check.build_environment_result(args)
        finally:
            relay_client_check.tasklist_rows = original_tasklist
            relay_client_check.net_adapter_rows = original_adapters
            relay_client_check.service_rows = original_services
            relay_client_check.ip_configuration_rows = original_ip_config
            relay_client_check.machine_identity_rows = original_identity
            relay_client_check.local_ipv4_addresses = original_ipv4


def json_dump(value: object) -> str:
    import json

    return json.dumps(value, sort_keys=True)


if __name__ == "__main__":
    unittest.main()
