"""Check one live reconstructed client during a two-PC WizardNet relay test."""

from __future__ import annotations

import argparse
import configparser
import csv
import hashlib
import io
import json
import platform
import re
from pathlib import Path
import socket
import subprocess
import sys
from datetime import datetime, timezone
from typing import Callable

import relay_log_check


def split_netstat_endpoint(endpoint: str) -> tuple[str, int]:
    if endpoint.startswith("[") and "]:" in endpoint:
        address, port = endpoint.rsplit("]:", 1)
        return address[1:], int(port)
    if ":" not in endpoint:
        return endpoint, 0
    address, port = endpoint.rsplit(":", 1)
    try:
        return address, int(port)
    except ValueError:
        return address, 0


def netstat_output(protocol: str) -> str:
    completed = subprocess.run(
        ["netstat", "-ano", "-p", protocol],
        check=True,
        capture_output=True,
        text=True,
        encoding="mbcs",
        errors="replace",
    )
    return completed.stdout


def process_ids_by_name(process_name: str) -> list[int]:
    completed = subprocess.run(
        ["tasklist", "/FI", f"IMAGENAME eq {process_name}", "/FO", "CSV", "/NH"],
        check=True,
        capture_output=True,
        text=True,
        encoding="mbcs",
        errors="replace",
    )
    pids: list[int] = []
    for row in csv.reader(io.StringIO(completed.stdout)):
        if len(row) < 2 or row[0].upper() == "INFO:":
            continue
        if row[0].casefold() != process_name.casefold():
            continue
        try:
            pids.append(int(row[1]))
        except ValueError:
            continue
    return sorted(set(pids))


def tasklist_rows() -> list[dict[str, object]]:
    completed = subprocess.run(
        ["tasklist", "/FO", "CSV", "/NH"],
        check=True,
        capture_output=True,
        text=True,
        encoding="mbcs",
        errors="replace",
    )
    rows: list[dict[str, object]] = []
    for row in csv.reader(io.StringIO(completed.stdout)):
        if len(row) < 2 or row[0].upper() == "INFO:":
            continue
        try:
            pid: int | None = int(row[1])
        except ValueError:
            pid = None
        rows.append(
            {
                "image_name": row[0],
                "pid": pid,
                "session_name": row[2] if len(row) > 2 else "",
                "session": row[3] if len(row) > 3 else "",
            }
        )
    return rows


def powershell_json(command: str) -> list[dict[str, object]]:
    completed = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        check=True,
        capture_output=True,
        text=True,
        encoding="mbcs",
        errors="replace",
    )
    output = completed.stdout.strip()
    if not output:
        return []
    data = json.loads(output)
    if isinstance(data, dict):
        return [data]
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]
    return []


def net_adapter_rows() -> list[dict[str, object]]:
    return powershell_json(
        "Get-NetAdapter | "
        "Select-Object Name,InterfaceDescription,Status,MacAddress,LinkSpeed | "
        "ConvertTo-Json -Compress"
    )


def service_rows() -> list[dict[str, object]]:
    return powershell_json(
        "Get-Service | "
        "Select-Object Name,DisplayName,Status | "
        "ConvertTo-Json -Compress"
    )


def ip_configuration_rows() -> list[dict[str, object]]:
    return powershell_json(
        "Get-NetIPConfiguration | ForEach-Object { "
        "$ipv4 = $_.IPv4Address | Select-Object -First 1; "
        "$gw = $_.IPv4DefaultGateway | Select-Object -First 1; "
        "$gwmac = $null; "
        "if ($gw) { "
        "$null = Test-Connection -ComputerName $gw.NextHop -Count 1 -Quiet "
        "-ErrorAction SilentlyContinue; "
        "$neighbor = Get-NetNeighbor -InterfaceIndex $_.InterfaceIndex "
        "-IPAddress $gw.NextHop -AddressFamily IPv4 -ErrorAction SilentlyContinue | "
        "Where-Object { $_.LinkLayerAddress -and $_.LinkLayerAddress -ne '00-00-00-00-00-00' } | "
        "Select-Object -First 1; "
        "if ($neighbor) { $gwmac = $neighbor.LinkLayerAddress } "
        "} "
        "[pscustomobject]@{"
        "InterfaceAlias=$_.InterfaceAlias;"
        "InterfaceIndex=$_.InterfaceIndex;"
        "InterfaceDescription=$_.InterfaceDescription;"
        "IPv4Address=$ipv4.IPAddress;"
        "PrefixLength=$ipv4.PrefixLength;"
        "IPv4DefaultGateway=$gw.NextHop;"
        "GatewayMac=$gwmac"
        "} "
        "} | ConvertTo-Json -Compress"
    )


def machine_identity_rows() -> list[dict[str, object]]:
    return powershell_json(
        "$machineGuid = (Get-ItemProperty "
        "-Path 'HKLM:\\SOFTWARE\\Microsoft\\Cryptography' "
        "-Name MachineGuid -ErrorAction SilentlyContinue).MachineGuid; "
        "$product = Get-CimInstance -ClassName Win32_ComputerSystemProduct "
        "-ErrorAction SilentlyContinue; "
        "$bios = Get-CimInstance -ClassName Win32_BIOS "
        "-ErrorAction SilentlyContinue; "
        "[pscustomobject]@{"
        "MachineGuid=$machineGuid;"
        "ComputerSystemUuid=$product.UUID;"
        "BiosSerialNumber=$bios.SerialNumber"
        "} | ConvertTo-Json -Compress"
    )


def row_matches_markers(
    row: dict[str, object], fields: tuple[str, ...], markers: tuple[str, ...]
) -> bool:
    for field in fields:
        value = row.get(field)
        if value is None:
            continue
        text = str(value).casefold()
        if any(marker in text for marker in markers):
            return True
    return False


def active_adapter(row: dict[str, object]) -> bool:
    return str(row.get("Status", "")).casefold() in {"up", "connected"}


def running_service(row: dict[str, object]) -> bool:
    return str(row.get("Status", "")).casefold() == "running"


def local_ipv4_addresses() -> list[str]:
    addresses: set[str] = set()
    try:
        for family, _, _, _, sockaddr in socket.getaddrinfo(
            socket.gethostname(), None, socket.AF_INET
        ):
            if family == socket.AF_INET and sockaddr:
                address = str(sockaddr[0])
                if not address.startswith("127."):
                    addresses.add(address)
    except OSError:
        pass
    return sorted(addresses)


def collect_environment_rows(
    collector: Callable[[], list[dict[str, object]]]
) -> tuple[list[dict[str, object]], str | None]:
    try:
        return collector(), None
    except (
        FileNotFoundError,
        OSError,
        subprocess.CalledProcessError,
        json.JSONDecodeError,
    ) as exc:
        return [], str(exc)


GENERIC_MACHINE_ID_VALUES = {
    "00000000-0000-0000-0000-000000000000",
    "03000200-0400-0500-0006-000700080009",
    "default string",
    "none",
    "not applicable",
    "not available",
    "not specified",
    "system serial number",
    "to be filled by o.e.m.",
    "to be filled by oem",
    "unknown",
}

DEFAULT_FORBIDDEN_ROUTE_MARKERS = [
    "radmin",
    "famatech",
    "vpn",
    "wireguard",
    "wintun",
    "tap-windows",
    "openvpn",
    "zerotier",
    "hamachi",
    "tailscale",
    "netbird",
    "softether",
    "tunnel",
    "teredo",
    "tap",
    "tun",
    "virtual",
    "vmware",
    "virtualbox",
    "hyper-v",
    "vethernet",
]


def normalize_machine_identity_value(value: object) -> str:
    text = str(value or "").strip()
    if not text:
        return ""
    lowered = text.casefold()
    if lowered in GENERIC_MACHINE_ID_VALUES:
        return ""
    if re.fullmatch(r"[0\-]+", text):
        return ""
    return lowered


def build_machine_identity(raw: dict[str, object]) -> dict[str, object]:
    source_fields = (
        ("machine_guid", "MachineGuid"),
        ("computer_system_uuid", "ComputerSystemUuid"),
        ("bios_serial_number", "BiosSerialNumber"),
    )
    components: list[str] = []
    sources: list[dict[str, object]] = []
    for source_name, field in source_fields:
        normalized = normalize_machine_identity_value(raw.get(field))
        if not normalized:
            sources.append({"name": source_name, "present": False})
            continue
        component = f"{source_name}={normalized}"
        components.append(component)
        sources.append(
            {
                "name": source_name,
                "present": True,
                "sha256": hashlib.sha256(component.encode("utf-8")).hexdigest().upper(),
            }
        )
    fingerprint = ""
    if components:
        fingerprint = hashlib.sha256(
            "\n".join(sorted(components)).encode("utf-8")
        ).hexdigest().upper()
    return {
        "fingerprint_sha256": fingerprint,
        "source_count": len(components),
        "sources": sources,
    }


def build_environment_result(args: argparse.Namespace) -> dict[str, object]:
    markers = tuple(marker.casefold() for marker in args.radmin_marker)
    route_markers = tuple(
        marker.casefold() for marker in args.forbidden_route_marker
    )
    all_processes, process_error = collect_environment_rows(tasklist_rows)
    all_adapters, adapter_error = collect_environment_rows(net_adapter_rows)
    all_services, service_error = collect_environment_rows(service_rows)
    ip_configs, ip_config_error = collect_environment_rows(ip_configuration_rows)
    identity_rows, identity_error = collect_environment_rows(machine_identity_rows)
    identity = build_machine_identity(identity_rows[0] if identity_rows else {})
    processes = [
        row
        for row in all_processes
        if row_matches_markers(row, ("image_name",), markers)
    ]
    adapters = [
        row
        for row in all_adapters
        if row_matches_markers(row, ("Name", "InterfaceDescription"), markers)
    ]
    services = [
        row
        for row in all_services
        if row_matches_markers(row, ("Name", "DisplayName"), markers)
    ]
    active_adapters = [row for row in adapters if active_adapter(row)]
    running_services = [row for row in services if running_service(row)]
    forbidden_default_gateway_interfaces = [
        row
        for row in ip_configs
        if row.get("IPv4DefaultGateway")
        and row_matches_markers(
            row,
            ("InterfaceAlias", "InterfaceDescription"),
            route_markers,
        )
    ]
    checks = {
        "environment_queries_ok": not (
            process_error or adapter_error or service_error or ip_config_error
        ),
        "machine_identity_query_ok": identity_error is None,
        "machine_identity_present": bool(identity["fingerprint_sha256"]),
        "no_radmin_processes": not processes,
        "no_active_radmin_adapters": not active_adapters,
        "no_running_radmin_services": not running_services,
        "no_forbidden_default_gateway_interfaces": (
            not forbidden_default_gateway_interfaces
        ),
    }
    return {
        "machine": {
            "hostname": socket.gethostname(),
            "platform": platform.platform(),
            "local_ipv4": local_ipv4_addresses(),
            "local_ipv4_config": ip_configs,
            "identity": identity,
        },
        "radmin_markers": list(markers),
        "radmin_processes": processes,
        "radmin_adapters": adapters,
        "active_radmin_adapters": active_adapters,
        "radmin_services": services,
        "running_radmin_services": running_services,
        "forbidden_route_markers": list(route_markers),
        "forbidden_default_gateway_interfaces": (
            forbidden_default_gateway_interfaces
        ),
        "errors": {
            "processes": process_error,
            "adapters": adapter_error,
            "services": service_error,
            "ip_config": ip_config_error,
            "machine_identity": identity_error,
        },
        "checks": checks,
    }


def resolve_server_addresses(server_host: str) -> set[str]:
    addresses = {server_host}
    try:
        for family, _, _, _, sockaddr in socket.getaddrinfo(server_host, None):
            if family == socket.AF_INET and sockaddr:
                addresses.add(str(sockaddr[0]))
    except OSError:
        pass
    if server_host in {"localhost", "127.0.0.1"}:
        addresses.update({"localhost", "127.0.0.1"})
    return addresses


def tcp_connections_for_pids(pids: set[int]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in netstat_output("TCP").splitlines():
        parts = line.split()
        if len(parts) < 5 or parts[0].upper() != "TCP":
            continue
        try:
            pid = int(parts[4])
        except ValueError:
            continue
        if pid not in pids:
            continue
        remote_address, remote_port = split_netstat_endpoint(parts[2])
        rows.append(
            {
                "pid": pid,
                "local": parts[1],
                "remote": parts[2],
                "remote_address": remote_address,
                "remote_port": remote_port,
                "state": parts[3].upper(),
            }
        )
    return rows


def udp_endpoints_for_pids(pids: set[int]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in netstat_output("UDP").splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[0].upper() != "UDP":
            continue
        try:
            pid = int(parts[-1])
        except ValueError:
            continue
        if pid in pids:
            rows.append({"pid": pid, "local": parts[1]})
    return rows


def file_evidence(path: Path | None) -> dict[str, object]:
    if path is None:
        return {"path": None, "exists": False}
    resolved = path.resolve()
    if not resolved.exists():
        return {"path": str(resolved), "exists": False}
    digest = hashlib.sha256()
    with resolved.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    stat = resolved.stat()
    return {
        "path": str(resolved),
        "exists": True,
        "size": stat.st_size,
        "modified_utc": datetime.fromtimestamp(
            stat.st_mtime, timezone.utc
        ).isoformat(),
        "sha256": digest.hexdigest().upper(),
    }


def text_file_evidence(path: Path | None) -> dict[str, object]:
    evidence = file_evidence(path)
    if evidence.get("exists"):
        resolved = Path(str(evidence["path"]))
        evidence["text"] = resolved.read_text(encoding="mbcs", errors="replace")
    return evidence


def server_hosts_match(expected: str, actual: str) -> bool:
    if expected.casefold() == actual.casefold():
        return True
    return bool(
        resolve_server_addresses(expected).intersection(resolve_server_addresses(actual))
    )


def parse_wizardnet_ini(text: str) -> tuple[dict[str, object], str | None]:
    parser = configparser.ConfigParser()
    try:
        parser.read_string(text)
    except configparser.Error as exc:
        return {}, str(exc)
    if not parser.has_section("WizardNet"):
        return {}, "missing [WizardNet] section"

    address = parser.get("WizardNet", "Address", fallback="").strip()
    port_text = parser.get("WizardNet", "Port", fallback="").strip()
    port: int | None = None
    port_error: str | None = None
    if port_text:
        try:
            port = int(port_text)
        except ValueError as exc:
            port_error = str(exc)
    return {
        "Address": address,
        "Port": port_text,
        "PortNumber": port,
    }, port_error


def build_deployment_result(args: argparse.Namespace) -> dict[str, object]:
    exe = file_evidence(args.exe)
    ini = text_file_evidence(args.ini)
    ini_values: dict[str, object] = {}
    ini_error: str | None = None
    if ini.get("exists"):
        ini_values, ini_error = parse_wizardnet_ini(str(ini.get("text", "")))
        # ranker_client.ini also contains the remembered account and display
        # preferences.  They are not release evidence, so never publish the
        # combined file's raw text in a client report.
        ini.pop("text", None)
    address = str(ini_values.get("Address", ""))
    port = ini_values.get("PortNumber")
    address_matches = bool(address) and server_hosts_match(args.server_host, address)
    port_matches = isinstance(port, int) and port == args.server_port
    checks = {
        "exe_exists": bool(exe.get("exists")),
        "exe_name_is_ranker_rebuild": (
            args.exe is not None and args.exe.name.casefold() == "ranker_rebuild.exe"
        ),
        "ini_exists": bool(ini.get("exists")),
        "ini_parse_ok": bool(ini.get("exists")) and ini_error is None,
        "ini_has_wizardnet_address": bool(address),
        "ini_has_wizardnet_port": isinstance(port, int),
        "ini_server_matches": address_matches and port_matches,
    }
    missing = [name for name, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "exe": exe,
        "ini": ini,
        "ini_values": ini_values,
        "ini_error": ini_error,
        "expected_server": f"{args.server_host}:{args.server_port}",
        "checks": checks,
        "missing_checks": missing,
    }


def build_log_args(args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        log=args.log,
        role=args.role,
        room=args.room,
        game_id=args.game_id,
        require_mode1=args.require_mode1,
        forbid_direct_transport=args.forbid_direct_transport,
        tail_bytes=args.tail_bytes,
        show_lines=args.show_lines,
    )


def build_network_result(args: argparse.Namespace) -> dict[str, object]:
    pids = set(args.pid)
    if not pids:
        pids.update(process_ids_by_name(args.process_name))
    allowed_addresses = resolve_server_addresses(args.server_host)
    tcp_connections = tcp_connections_for_pids(pids) if pids else []
    udp_endpoints = udp_endpoints_for_pids(pids) if pids else []
    established = [
        connection
        for connection in tcp_connections
        if connection["state"] == "ESTABLISHED"
    ]
    listening = [
        connection
        for connection in tcp_connections
        if connection["state"] == "LISTENING"
    ]
    server_connections = [
        connection
        for connection in established
        if connection["remote_port"] == args.server_port
        and connection["remote_address"] in allowed_addresses
    ]
    unexpected = [
        connection
        for connection in established
        if connection not in server_connections
    ]
    server_connection_counts = {
        pid: sum(1 for connection in server_connections if connection["pid"] == pid)
        for pid in pids
    }
    checks = {
        "live_process_present": bool(pids),
        "single_live_process": len(pids) == 1,
        "server_tcp_connected": bool(server_connections),
        "one_server_tcp_per_process": bool(pids)
        and all(count == 1 for count in server_connection_counts.values()),
        "only_server_tcp_established": not unexpected,
        "no_tcp_listeners": not listening,
        "no_udp_endpoints": not udp_endpoints,
    }
    return {
        "process_name": args.process_name,
        "pids": sorted(pids),
        "allowed_server_addresses": sorted(allowed_addresses),
        "tcp_established": established,
        "tcp_listening": listening,
        "tcp_unexpected": unexpected,
        "udp_endpoints": udp_endpoints,
        "server_tcp_connection_counts": server_connection_counts,
        "checks": checks,
    }


def build_result(args: argparse.Namespace) -> dict[str, object]:
    log_result = relay_log_check.check_log(build_log_args(args))
    deployment = build_deployment_result(args)
    result: dict[str, object] = {
        "ok": bool(log_result["ok"]),
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "role": args.role,
        "room": args.room,
        "game_id": args.game_id,
        "effective_game_id": log_result.get("effective_game_id"),
        "server": f"{args.server_host}:{args.server_port}",
        "deployment": deployment,
        "log": log_result,
    }
    if args.require_deployment:
        result["ok"] = bool(result["ok"] and deployment["ok"])

    if not args.skip_network:
        network = build_network_result(args)
        checks = network["checks"]
        network_ok = (
            (checks["live_process_present"] or not args.require_live_process)
            and checks["single_live_process"]
            and checks["server_tcp_connected"]
            and checks["one_server_tcp_per_process"]
            and checks["only_server_tcp_established"]
            and checks["no_tcp_listeners"]
            and checks["no_udp_endpoints"]
        )
        result["network"] = network
        result["ok"] = bool(result["ok"] and network_ok)
    if not args.skip_environment:
        environment = build_environment_result(args)
        environment_ok = (
            not args.require_no_radmin
            or (
                environment["checks"]["environment_queries_ok"]
                and environment["checks"]["no_radmin_processes"]
                and environment["checks"]["no_active_radmin_adapters"]
                and environment["checks"]["no_running_radmin_services"]
                and environment["checks"]["no_forbidden_default_gateway_interfaces"]
            )
        )
        result["environment"] = environment
        result["ok"] = bool(result["ok"] and environment_ok)
    return result


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(
        description=(
            "Validate one reconstructed client during a live two-PC WizardNet "
            "relay test."
        )
    )
    parser.add_argument("--log", type=Path, required=True, help="Path to Jw2.log")
    parser.add_argument(
        "--role",
        choices=("host", "joiner", "combined"),
        required=True,
        help="Client role this log should prove.",
    )
    parser.add_argument("--room", help="Expected room name.")
    parser.add_argument("--server-host", default="115.22.136.89")
    parser.add_argument("--server-port", type=int, default=19777)
    parser.add_argument(
        "--game-id",
        type=int,
        help="Expected server game ID. When set, this scopes log checks exactly.",
    )
    parser.add_argument("--process-name", default="ranker_rebuild.exe")
    parser.add_argument(
        "--exe",
        type=Path,
        help="Path to ranker_rebuild.exe; defaults to the log directory.",
    )
    parser.add_argument(
        "--ini",
        type=Path,
        help="Path to ranker_client.ini; defaults to the log directory.",
    )
    parser.add_argument("--output", type=Path, help="Optional JSON output path.")
    parser.add_argument("--pid", type=int, action="append", default=[])
    parser.add_argument("--require-mode1", action="store_true")
    parser.add_argument("--forbid-direct-transport", action="store_true")
    parser.add_argument(
        "--require-deployment",
        action="store_true",
        help=(
            "Fail unless ranker_rebuild.exe exists and ranker_client.ini "
            "points at the expected relay server."
        ),
    )
    parser.add_argument("--require-live-process", action="store_true")
    parser.add_argument(
        "--require-no-radmin",
        action="store_true",
        help=(
            "Fail if Radmin/Famatech processes, active adapters, or running "
            "services are detected."
        ),
    )
    parser.add_argument(
        "--radmin-marker",
        action="append",
        default=["radmin", "rvrvpn", "rvcontrol", "famatech"],
        help="Case-insensitive process/adapter/service marker for VPN checks.",
    )
    parser.add_argument(
        "--forbidden-route-marker",
        action="append",
        default=DEFAULT_FORBIDDEN_ROUTE_MARKERS,
        help=(
            "Case-insensitive marker that must not appear on the interface "
            "providing an IPv4 default gateway in strict release evidence."
        ),
    )
    parser.add_argument("--skip-network", action="store_true")
    parser.add_argument("--skip-environment", action="store_true")
    parser.add_argument("--tail-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--show-lines", type=int, default=80)
    args = parser.parse_args()
    if args.exe is None:
        args.exe = args.log.resolve().parent / "ranker_rebuild.exe"
    if args.ini is None:
        args.ini = args.log.resolve().parent / "ranker_client.ini"

    result = build_result(args)
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
