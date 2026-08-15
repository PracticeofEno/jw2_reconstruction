"""Run the offline WizardNet relay release gate over both client logs."""

from __future__ import annotations

import argparse
import ipaddress
import json
from pathlib import Path
import sys

import relay_log_check
import relay_server_summary_check


def client_log_args(
    log: Path,
    role: str,
    args: argparse.Namespace,
    game_id: int | None,
) -> argparse.Namespace:
    return argparse.Namespace(
        log=log,
        role=role,
        room=args.room,
        game_id=game_id,
        require_mode1=args.require_mode1,
        forbid_direct_transport=args.forbid_direct_transport,
        tail_bytes=args.tail_bytes,
        show_lines=args.show_lines,
    )


def server_summary_args(args: argparse.Namespace) -> argparse.Namespace:
    return argparse.Namespace(
        log=args.server_log,
        room=args.room,
        game_id=args.game_id,
        min_link_frames=args.min_link_frames,
        min_mode1_frames=args.min_mode1_frames,
        min_deliveries=args.min_deliveries,
        min_members=args.min_members,
        min_distinct_peer_hosts=args.min_distinct_peer_hosts,
        min_distinct_peer_endpoints=args.min_distinct_peer_endpoints,
        min_bidirectional_mode1_members=args.min_bidirectional_mode1_members,
        max_no_target=args.max_no_target,
        max_invalid_stream=args.max_invalid_stream,
        max_invalid_payload=args.max_invalid_payload,
        tail_bytes=args.tail_bytes,
        show_lines=args.server_show_lines,
    )


def load_server_summary(args: argparse.Namespace) -> dict[str, object]:
    if args.server_evidence is not None:
        return load_server_summary_evidence(args.server_evidence, args)
    if args.server_evidence_dir is not None:
        return load_latest_server_summary_evidence(args.server_evidence_dir, args)
    return relay_server_summary_check.check_summary(server_summary_args(args))


def load_server_summary_evidence(
    evidence_path: Path, args: argparse.Namespace
) -> dict[str, object]:
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    exported_summary = evidence.get("server_summary")
    summary_record = (
        exported_summary.get("summary")
        if isinstance(exported_summary, dict)
        else None
    )
    summary_lines = (
        exported_summary.get("summary_lines")
        if isinstance(exported_summary, dict)
        else None
    )
    matched_summary_count = (
        exported_summary.get("matched_summary_count")
        if isinstance(exported_summary, dict)
        else 0
    )
    if not isinstance(summary_lines, list):
        summary_lines = []
    if not isinstance(matched_summary_count, int):
        matched_summary_count = 0
    result = relay_server_summary_check.check_summary_record(
        summary_record,
        server_summary_args(args),
        log=str(evidence_path.resolve()),
        tail_bytes=args.tail_bytes,
        matched_summary_count=matched_summary_count,
        summary_lines=[str(line) for line in summary_lines],
        require_identity_match=True,
    )
    checks = result["checks"]
    checks["server_evidence_has_summary"] = isinstance(exported_summary, dict)
    checks["server_evidence_ok"] = bool(
        evidence.get("ok") is True
        and isinstance(exported_summary, dict)
        and exported_summary.get("ok") is True
    )
    checks["server_evidence_summary_line_present"] = bool(
        isinstance(summary_record, dict)
        and isinstance(summary_record.get("line"), str)
        and summary_record.get("line")
    )
    result["ok"] = all(checks.values())
    result["missing_checks"] = [name for name, ok in checks.items() if not ok]
    result["evidence_path"] = str(evidence_path.resolve())
    result["source_log"] = evidence.get("source_log")
    result["exported_utc"] = evidence.get("exported_utc")
    return result


def evidence_identity(evidence_path: Path) -> tuple[str, int | None]:
    try:
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "", None
    exported_summary = evidence.get("server_summary")
    if not isinstance(exported_summary, dict):
        return "", None
    summary = exported_summary.get("summary")
    if not isinstance(summary, dict):
        return "", None
    room = evidence.get("room")
    if not isinstance(room, str):
        room = summary.get("room")
    game_id = evidence.get("game_id")
    if not isinstance(game_id, int):
        game_id = summary.get("game_id")
    return (
        str(room) if isinstance(room, str) else "",
        game_id if isinstance(game_id, int) else None,
    )


def matching_evidence_paths(
    evidence_dir: Path, args: argparse.Namespace
) -> list[Path]:
    if not evidence_dir.exists():
        return []
    matches: list[Path] = []
    for path in evidence_dir.rglob("*.json"):
        room, game_id = evidence_identity(path)
        if room != args.room:
            continue
        if args.game_id is not None and game_id != args.game_id:
            continue
        matches.append(path)
    return sorted(
        matches,
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )


def load_latest_server_summary_evidence(
    evidence_dir: Path, args: argparse.Namespace
) -> dict[str, object]:
    matches = matching_evidence_paths(evidence_dir, args)
    if not matches:
        summary_args = server_summary_args(args)
        result = relay_server_summary_check.check_summary_record(
            None,
            summary_args,
            log=str(evidence_dir.resolve()),
            tail_bytes=0,
            matched_summary_count=0,
            summary_lines=[],
            require_identity_match=True,
        )
        checks = result["checks"]
        checks["server_evidence_dir_exists"] = evidence_dir.exists()
        checks["server_evidence_dir_has_matching_room"] = False
        result["ok"] = False
        result["missing_checks"] = [
            name for name, ok in checks.items() if not ok
        ]
        result["evidence_dir"] = str(evidence_dir.resolve())
        result["candidate_count"] = 0
        return result
    result = load_server_summary_evidence(matches[0], args)
    result["evidence_dir"] = str(evidence_dir.resolve())
    result["candidate_count"] = len(matches)
    return result


def effective_game_id(
    args: argparse.Namespace, server: dict[str, object]
) -> int | None:
    if args.game_id is not None:
        return args.game_id
    summary = server.get("summary")
    if isinstance(summary, dict):
        game_id = summary.get("game_id")
        if isinstance(game_id, int):
            return game_id
    return None


def load_client_report(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def client_report_matches(
    path: Path | None,
    role: str,
    args: argparse.Namespace,
    game_id: int | None,
) -> dict[str, object]:
    if path is None:
        checks = {"report_provided": not args.require_client_reports}
        return {
            "ok": checks["report_provided"],
            "path": None,
            "role": role,
            "checks": checks,
            "missing_checks": [
                name for name, ok in checks.items() if not ok
            ],
        }

    report = load_client_report(path)
    network = report.get("network")
    network_checks = network.get("checks") if isinstance(network, dict) else None
    environment = report.get("environment")
    environment_checks = (
        environment.get("checks") if isinstance(environment, dict) else None
    )
    deployment = report.get("deployment")
    deployment_checks = (
        deployment.get("checks") if isinstance(deployment, dict) else None
    )
    expected_server = f"{args.server_host}:{args.server_port}"
    checks = {
        "report_provided": True,
        "report_ok": report.get("ok") is True,
        "role_matches": report.get("role") == role,
        "room_matches": report.get("room") == args.room,
        "server_matches": report.get("server") == expected_server,
        "network_present": isinstance(network_checks, dict),
        "live_process_present": bool(
            network_checks is not None
            and network_checks.get("live_process_present")
        ),
        "single_live_process": bool(
            network_checks is not None
            and network_checks.get("single_live_process")
        ),
        "server_tcp_connected": bool(
            network_checks is not None
            and network_checks.get("server_tcp_connected")
        ),
        "one_server_tcp_per_process": bool(
            network_checks is not None
            and network_checks.get("one_server_tcp_per_process")
        ),
        "only_server_tcp_established": bool(
            network_checks is not None
            and network_checks.get("only_server_tcp_established")
        ),
        "no_tcp_listeners": bool(
            network_checks is not None
            and network_checks.get("no_tcp_listeners")
        ),
        "no_udp_endpoints": bool(
            network_checks is not None
            and network_checks.get("no_udp_endpoints")
        ),
        "environment_present": isinstance(environment_checks, dict),
        "deployment_present": isinstance(deployment_checks, dict),
        "deployment_ok": bool(
            deployment is not None and deployment.get("ok") is True
        ),
        "deployment_exe_exists": bool(
            deployment_checks is not None and deployment_checks.get("exe_exists")
        ),
        "deployment_exe_name_is_ranker_rebuild": bool(
            deployment_checks is not None
            and deployment_checks.get("exe_name_is_ranker_rebuild")
        ),
        "deployment_ini_exists": bool(
            deployment_checks is not None and deployment_checks.get("ini_exists")
        ),
        "deployment_ini_parse_ok": bool(
            deployment_checks is not None and deployment_checks.get("ini_parse_ok")
        ),
        "deployment_ini_server_matches": bool(
            deployment_checks is not None
            and deployment_checks.get("ini_server_matches")
        ),
    }
    report_game_id = report.get("effective_game_id", report.get("game_id"))
    checks["game_id_matches"] = game_id is None or report_game_id == game_id
    if args.require_no_radmin:
        checks["environment_queries_ok"] = bool(
            environment_checks is not None
            and environment_checks.get("environment_queries_ok")
        )
        checks["no_radmin_processes"] = bool(
            environment_checks is not None
            and environment_checks.get("no_radmin_processes")
        )
        checks["no_active_radmin_adapters"] = bool(
            environment_checks is not None
            and environment_checks.get("no_active_radmin_adapters")
        )
        checks["no_running_radmin_services"] = bool(
            environment_checks is not None
            and environment_checks.get("no_running_radmin_services")
        )
        checks["no_forbidden_default_gateway_interfaces"] = bool(
            environment_checks is not None
            and environment_checks.get("no_forbidden_default_gateway_interfaces")
        )
    missing = [name for name, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "path": str(path.resolve()),
        "role": role,
        "checks": checks,
        "missing_checks": missing,
        "captured_utc": report.get("captured_utc"),
        "effective_game_id": report_game_id,
        "machine": (
            environment.get("machine")
            if isinstance(environment, dict)
            else None
        ),
        "deployment": report.get("deployment"),
    }


def load_optional_report(path: Path | None) -> dict[str, object] | None:
    if path is None:
        return None
    return load_client_report(path)


def report_hostname(report: dict[str, object] | None) -> str:
    if report is None:
        return ""
    environment = report.get("environment")
    if not isinstance(environment, dict):
        return ""
    machine = environment.get("machine")
    if not isinstance(machine, dict):
        return ""
    return str(machine.get("hostname", ""))


def report_machine_identity(report: dict[str, object] | None) -> str:
    machine = report_machine(report)
    identity = machine.get("identity")
    if not isinstance(identity, dict):
        return ""
    return str(identity.get("fingerprint_sha256", ""))


def report_machine(report: dict[str, object] | None) -> dict[str, object]:
    if report is None:
        return {}
    environment = report.get("environment")
    if not isinstance(environment, dict):
        return {}
    machine = environment.get("machine")
    return machine if isinstance(machine, dict) else {}


def report_environment_checks(
    report: dict[str, object] | None,
) -> dict[str, object]:
    if report is None:
        return {}
    environment = report.get("environment")
    if not isinstance(environment, dict):
        return {}
    checks = environment.get("checks")
    return checks if isinstance(checks, dict) else {}


def report_exe_hash(report: dict[str, object] | None) -> str:
    if report is None:
        return ""
    deployment = report.get("deployment")
    if not isinstance(deployment, dict):
        return ""
    exe = deployment.get("exe")
    if not isinstance(exe, dict):
        return ""
    return str(exe.get("sha256", ""))


def ipv4_network_fingerprint(row: dict[str, object]) -> str:
    address = str(row.get("IPv4Address", ""))
    if not address or address.startswith("127.") or address.startswith("169.254."):
        return ""
    prefix = row.get("PrefixLength")
    network = ""
    try:
        if prefix is not None:
            network = str(ipaddress.ip_network(f"{address}/{prefix}", strict=False))
    except ValueError:
        network = ""
    gateway = str(row.get("IPv4DefaultGateway", ""))
    gateway_mac = str(row.get("GatewayMac", ""))
    if gateway or gateway_mac:
        return f"gw={gateway}|mac={gateway_mac}".casefold()
    return network.casefold()


def report_lan_fingerprints(report: dict[str, object] | None) -> list[str]:
    machine = report_machine(report)
    configs = machine.get("local_ipv4_config")
    if isinstance(configs, dict):
        config_rows = [configs]
    elif isinstance(configs, list):
        config_rows = [row for row in configs if isinstance(row, dict)]
    else:
        config_rows = []
    gateway_rows = [
        row
        for row in config_rows
        if row.get("IPv4DefaultGateway") or row.get("GatewayMac")
    ]
    if gateway_rows:
        config_rows = gateway_rows
    fingerprints = {
        fingerprint
        for row in config_rows
        if (fingerprint := ipv4_network_fingerprint(row))
    }
    return sorted(fingerprints)


def client_pair_matches(args: argparse.Namespace) -> dict[str, object]:
    host_report = load_optional_report(args.host_client_report)
    joiner_report = load_optional_report(args.joiner_client_report)
    host_hostname = report_hostname(host_report)
    joiner_hostname = report_hostname(joiner_report)
    host_identity = report_machine_identity(host_report)
    joiner_identity = report_machine_identity(joiner_report)
    host_environment_checks = report_environment_checks(host_report)
    joiner_environment_checks = report_environment_checks(joiner_report)
    checks = {
        "host_report_provided": host_report is not None,
        "joiner_report_provided": joiner_report is not None,
        "host_machine_identity_query_ok": bool(
            host_environment_checks.get("machine_identity_query_ok")
        ),
        "joiner_machine_identity_query_ok": bool(
            joiner_environment_checks.get("machine_identity_query_ok")
        ),
        "host_machine_identity_present": bool(host_identity),
        "joiner_machine_identity_present": bool(joiner_identity),
        "machine_identities_distinct": bool(host_identity)
        and bool(joiner_identity)
        and host_identity.casefold() != joiner_identity.casefold(),
    }
    missing = [name for name, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "checks": checks,
        "missing_checks": missing,
        "host_hostname": host_hostname,
        "joiner_hostname": joiner_hostname,
        "host_machine_identity": host_identity,
        "joiner_machine_identity": joiner_identity,
    }


def client_deployment_pair_matches(args: argparse.Namespace) -> dict[str, object]:
    host_report = load_optional_report(args.host_client_report)
    joiner_report = load_optional_report(args.joiner_client_report)
    host_hash = report_exe_hash(host_report)
    joiner_hash = report_exe_hash(joiner_report)
    hashes_match = (
        args.allow_different_exe_hash
        or (
            bool(host_hash)
            and bool(joiner_hash)
            and host_hash.casefold() == joiner_hash.casefold()
        )
    )
    checks = {
        "host_report_provided": host_report is not None,
        "joiner_report_provided": joiner_report is not None,
        "host_exe_hash_present": bool(host_hash),
        "joiner_exe_hash_present": bool(joiner_hash),
        "exe_hashes_match": hashes_match,
    }
    missing = [name for name, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "checks": checks,
        "missing_checks": missing,
        "host_exe_hash": host_hash,
        "joiner_exe_hash": joiner_hash,
    }


def client_network_pair_matches(
    args: argparse.Namespace, server: dict[str, object]
) -> dict[str, object]:
    host_report = load_optional_report(args.host_client_report)
    joiner_report = load_optional_report(args.joiner_client_report)
    host_lans = report_lan_fingerprints(host_report)
    joiner_lans = report_lan_fingerprints(joiner_report)
    summary = server.get("summary")
    distinct_peer_hosts = 0
    if isinstance(summary, dict):
        value = summary.get("distinct_peer_hosts")
        if isinstance(value, int):
            distinct_peer_hosts = value
    public_peer_hosts_distinct = distinct_peer_hosts >= 2
    local_lans_distinct = (
        bool(host_lans)
        and bool(joiner_lans)
        and set(host_lans).isdisjoint(joiner_lans)
    )
    checks = {
        "host_report_provided": host_report is not None,
        "joiner_report_provided": joiner_report is not None,
        "server_summary_has_peer_hosts": distinct_peer_hosts > 0,
        "host_lan_fingerprint_present": bool(host_lans),
        "joiner_lan_fingerprint_present": bool(joiner_lans),
        "public_or_lan_networks_distinct": (
            public_peer_hosts_distinct or local_lans_distinct
        ),
    }
    missing = [name for name, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "checks": checks,
        "missing_checks": missing,
        "server_distinct_peer_hosts": distinct_peer_hosts,
        "host_lan_fingerprints": host_lans,
        "joiner_lan_fingerprints": joiner_lans,
    }


def check_release_gate(args: argparse.Namespace) -> dict[str, object]:
    server = load_server_summary(args)
    game_id = effective_game_id(args, server)
    host = relay_log_check.check_log(
        client_log_args(args.host_log, "host", args, game_id)
    )
    joiner = relay_log_check.check_log(
        client_log_args(args.joiner_log, "joiner", args, game_id)
    )
    sections = {
        "host": host,
        "joiner": joiner,
        "server_summary": server,
    }
    host_client_report = client_report_matches(
        args.host_client_report, "host", args, game_id
    )
    joiner_client_report = client_report_matches(
        args.joiner_client_report, "joiner", args, game_id
    )
    if args.require_client_reports or args.host_client_report is not None:
        sections["host_client_report"] = host_client_report
    if args.require_client_reports or args.joiner_client_report is not None:
        sections["joiner_client_report"] = joiner_client_report
    if (
        args.require_client_reports
        or (
            args.host_client_report is not None
            and args.joiner_client_report is not None
        )
    ):
        sections["client_deployment_pair"] = client_deployment_pair_matches(args)
    if args.require_distinct_client_machines:
        sections["client_pair"] = client_pair_matches(args)
    if args.require_distinct_client_networks:
        sections["client_network_pair"] = client_network_pair_matches(args, server)
    failing_sections = [name for name, result in sections.items() if not result["ok"]]
    return {
        "ok": not failing_sections,
        "room": args.room,
        "game_id": args.game_id,
        "effective_game_id": game_id,
        "server": f"{args.server_host}:{args.server_port}",
        "require_mode1": args.require_mode1,
        "forbid_direct_transport": args.forbid_direct_transport,
        "require_client_reports": args.require_client_reports,
        "require_no_radmin": args.require_no_radmin,
        "require_distinct_client_machines": args.require_distinct_client_machines,
        "require_distinct_client_networks": args.require_distinct_client_networks,
        "failing_sections": failing_sections,
        **sections,
    }


def short_hash(value: object) -> str:
    text = str(value or "")
    return text[:12] if text else ""


def format_release_gate_summary(result: dict[str, object]) -> str:
    lines = [
        f"WizardNet relay final gate: {'PASS' if result.get('ok') else 'FAIL'}",
        (
            f"room={result.get('room')} "
            f"game_id={result.get('effective_game_id')} "
            f"server={result.get('server')}"
        ),
    ]
    failing_sections = result.get("failing_sections")
    if isinstance(failing_sections, list) and failing_sections:
        lines.append("failing_sections=" + ", ".join(str(item) for item in failing_sections))
    else:
        lines.append("failing_sections=none")

    for section_name in (
        "host",
        "joiner",
        "server_summary",
        "host_client_report",
        "joiner_client_report",
        "client_deployment_pair",
        "client_pair",
        "client_network_pair",
    ):
        section = result.get(section_name)
        if not isinstance(section, dict):
            continue
        status = "OK" if section.get("ok") else "FAIL"
        missing = section.get("missing_checks")
        missing_text = ""
        if isinstance(missing, list) and missing:
            missing_text = " missing=" + ",".join(str(item) for item in missing)
        lines.append(f"- {section_name}: {status}{missing_text}")
        if section_name == "server_summary":
            if evidence_path := section.get("evidence_path"):
                lines.append(f"  evidence_path={evidence_path}")
            if evidence_dir := section.get("evidence_dir"):
                lines.append(
                    f"  evidence_dir={evidence_dir} "
                    f"candidate_count={section.get('candidate_count')}"
                )
            summary = section.get("summary")
            if isinstance(summary, dict):
                lines.append(
                    "  "
                    f"members={summary.get('members')} "
                    f"endpoints={summary.get('distinct_peer_endpoints')} "
                    f"bidirectional_mode1={summary.get('bidirectional_mode1_members')} "
                    f"no_target={summary.get('no_target')} "
                    f"invalid_stream={summary.get('invalid_stream')} "
                    f"invalid_payload={summary.get('invalid_payload')}"
                )
        elif section_name == "client_pair":
            lines.append(
                "  "
                f"host={section.get('host_hostname')} "
                f"joiner={section.get('joiner_hostname')} "
                f"host_id={short_hash(section.get('host_machine_identity'))} "
                f"joiner_id={short_hash(section.get('joiner_machine_identity'))}"
            )
        elif section_name == "client_network_pair":
            lines.append(
                "  "
                f"server_distinct_peer_hosts={section.get('server_distinct_peer_hosts')} "
                f"host_lan={section.get('host_lan_fingerprints')} "
                f"joiner_lan={section.get('joiner_lan_fingerprints')}"
            )
        elif section_name == "client_deployment_pair":
            lines.append(
                "  "
                f"host_exe={short_hash(section.get('host_exe_hash'))} "
                f"joiner_exe={short_hash(section.get('joiner_exe_hash'))}"
            )
    return "\n".join(lines) + "\n"


def write_text_output(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Validate both reconstructed client logs plus the server relay "
            "summary for a two-PC WizardNet relay release gate."
        )
    )
    parser.add_argument("--host-log", type=Path, required=True)
    parser.add_argument("--joiner-log", type=Path, required=True)
    parser.add_argument("--server-log", type=Path)
    parser.add_argument("--server-evidence", type=Path)
    parser.add_argument(
        "--server-evidence-dir",
        type=Path,
        help=(
            "Directory containing automatic server evidence JSON files. The "
            "latest file matching --room and optional --game-id is used."
        ),
    )
    parser.add_argument("--host-client-report", type=Path)
    parser.add_argument("--joiner-client-report", type=Path)
    parser.add_argument("--room", required=True)
    parser.add_argument("--server-host", default="115.22.136.89")
    parser.add_argument("--server-port", type=int, default=19777)
    parser.add_argument("--game-id", type=int)
    report_group = parser.add_mutually_exclusive_group()
    report_group.add_argument(
        "--require-client-reports",
        dest="require_client_reports",
        action="store_true",
        help="Require host and joiner relay_client_check JSON reports.",
    )
    report_group.add_argument(
        "--allow-missing-client-reports",
        dest="require_client_reports",
        action="store_false",
        help="Allow log-only lab checks without live client JSON reports.",
    )
    radmin_group = parser.add_mutually_exclusive_group()
    radmin_group.add_argument(
        "--require-no-radmin",
        dest="require_no_radmin",
        action="store_true",
        help="Require client reports to prove no active Radmin/Famatech VPN state.",
    )
    radmin_group.add_argument(
        "--allow-radmin",
        dest="require_no_radmin",
        action="store_false",
        help="Allow active Radmin/Famatech VPN state in lab checks.",
    )
    machine_group = parser.add_mutually_exclusive_group()
    machine_group.add_argument(
        "--require-distinct-client-machines",
        dest="require_distinct_client_machines",
        action="store_true",
        help=(
            "Require host and joiner client reports to contain different hashed "
            "machine identity fingerprints."
        ),
    )
    machine_group.add_argument(
        "--allow-same-machine",
        dest="require_distinct_client_machines",
        action="store_false",
        help="Allow host and joiner evidence from the same machine in lab checks.",
    )
    network_group = parser.add_mutually_exclusive_group()
    network_group.add_argument(
        "--require-distinct-client-networks",
        dest="require_distinct_client_networks",
        action="store_true",
        help=(
            "Require evidence that the clients are not on the same local network. "
            "This passes with either two server-observed peer IPs or distinct "
            "client LAN fingerprints from the JSON reports."
        ),
    )
    network_group.add_argument(
        "--allow-same-network",
        dest="require_distinct_client_networks",
        action="store_false",
        help="Allow same-network evidence in lab checks.",
    )
    parser.add_argument(
        "--allow-different-exe-hash",
        action="store_true",
        help="Do not require host and joiner reports to contain the same exe SHA256.",
    )
    parser.add_argument(
        "--no-require-mode1",
        action="store_false",
        dest="require_mode1",
        help="Do not require gameplay Mode1 relay send/receive evidence.",
    )
    parser.set_defaults(require_mode1=True)
    parser.add_argument(
        "--allow-direct-transport",
        action="store_false",
        dest="forbid_direct_transport",
        help="Do not fail on legacy direct UDP route/probe log lines.",
    )
    parser.set_defaults(forbid_direct_transport=True)
    parser.add_argument("--min-link-frames", type=int, default=1)
    parser.add_argument("--min-mode1-frames", type=int, default=1)
    parser.add_argument("--min-deliveries", type=int, default=1)
    parser.add_argument("--min-members", type=int, default=2)
    parser.add_argument("--min-distinct-peer-hosts", type=int, default=0)
    parser.add_argument("--min-distinct-peer-endpoints", type=int, default=2)
    parser.add_argument("--min-bidirectional-mode1-members", type=int, default=2)
    parser.add_argument("--max-no-target", type=int, default=0)
    parser.add_argument("--max-invalid-stream", type=int, default=0)
    parser.add_argument("--max-invalid-payload", type=int, default=0)
    parser.add_argument("--tail-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--show-lines", type=int, default=80)
    parser.add_argument("--server-show-lines", type=int, default=5)
    parser.add_argument(
        "--output",
        type=Path,
        help="Optional path to write the full JSON gate result.",
    )
    parser.add_argument(
        "--summary-output",
        type=Path,
        help="Optional path to write a compact human-readable gate summary.",
    )
    parser.set_defaults(
        require_client_reports=True,
        require_no_radmin=True,
        require_distinct_client_machines=True,
        require_distinct_client_networks=True,
    )
    return parser


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = build_arg_parser()
    args = parser.parse_args()
    if (
        args.server_log is None
        and args.server_evidence is None
        and args.server_evidence_dir is None
    ):
        parser.error(
            "one of --server-log, --server-evidence, or "
            "--server-evidence-dir is required"
        )

    result = check_release_gate(args)
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output is not None:
        write_text_output(args.output, output + "\n")
    if args.summary_output is not None:
        write_text_output(args.summary_output, format_release_gate_summary(result))
    print(output)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
