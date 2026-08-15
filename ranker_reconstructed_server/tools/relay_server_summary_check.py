"""Check reconstructed WizardNet relay summary evidence in a server log."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


SUMMARY_PATTERN = re.compile(
    r"relay summary game=(?P<game_id>\d+) "
    r"name=(?P<room>.*?) "
    r"link_frames=(?P<link_frames>\d+) "
    r"link_bytes=(?P<link_bytes>\d+) "
    r"mode1_frames=(?P<mode1_frames>\d+) "
    r"mode1_bytes=(?P<mode1_bytes>\d+) "
    r"deliveries=(?P<deliveries>\d+) "
    r"no_target=(?P<no_target>\d+) "
    r"invalid_stream=(?P<invalid_stream>\d+)"
    r"(?: invalid_payload=(?P<invalid_payload>\d+))?"
    r"(?: members=(?P<members>\d+) "
    r"distinct_peer_hosts=(?P<distinct_peer_hosts>\d+) "
    r"member_peers=(?P<member_peers>\S*)"
    r"(?: member_frames=(?P<member_frames>\S*))?)?"
)


def parse_member_frames(text: str | None) -> dict[int, dict[str, int]]:
    if not text:
        return {}
    result: dict[int, dict[str, int]] = {}
    for member_text in text.split(";"):
        if not member_text:
            continue
        parts = member_text.split(":")
        try:
            member_id = int(parts[0])
        except ValueError:
            continue
        counters: dict[str, int] = {}
        for field in parts[1:]:
            if "=" not in field:
                continue
            name, value = field.split("=", 1)
            try:
                counters[name] = int(value)
            except ValueError:
                continue
        result[member_id] = counters
    return result


def parse_member_peers(text: str | None) -> dict[int, dict[str, object]]:
    if not text:
        return {}
    result: dict[int, dict[str, object]] = {}
    for peer_text in text.split(","):
        if "@" not in peer_text or ":" not in peer_text:
            continue
        member_text, endpoint = peer_text.split("@", 1)
        host, port_text = endpoint.rsplit(":", 1)
        try:
            member_id = int(member_text)
            port = int(port_text)
        except ValueError:
            continue
        result[member_id] = {
            "host": host,
            "port": port,
            "endpoint": f"{host}:{port}",
        }
    return result


def distinct_peer_endpoint_count(
    member_peers: dict[int, dict[str, object]]
) -> int:
    return len(
        {
            str(peer.get("endpoint", ""))
            for peer in member_peers.values()
            if peer.get("endpoint")
        }
    )


def bidirectional_mode1_member_count(
    member_frames: dict[int, dict[str, int]]
) -> int:
    return sum(
        1
        for counters in member_frames.values()
        if counters.get("mode1_tx", 0) > 0 and counters.get("mode1_rx", 0) > 0
    )


def normalize_int(value: object) -> int | None:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return None
    return None


def normalize_member_peers(value: object) -> dict[int, dict[str, object]]:
    if not isinstance(value, dict):
        return {}
    result: dict[int, dict[str, object]] = {}
    for member_key, peer_value in value.items():
        try:
            member_id = int(member_key)
        except (TypeError, ValueError):
            continue
        if not isinstance(peer_value, dict):
            continue
        host = str(peer_value.get("host", ""))
        port = normalize_int(peer_value.get("port")) or 0
        endpoint = str(peer_value.get("endpoint", ""))
        if not endpoint and host and port:
            endpoint = f"{host}:{port}"
        result[member_id] = {
            "host": host,
            "port": port,
            "endpoint": endpoint,
        }
    return result


def normalize_member_frames(value: object) -> dict[int, dict[str, int]]:
    if not isinstance(value, dict):
        return {}
    result: dict[int, dict[str, int]] = {}
    for member_key, counters_value in value.items():
        try:
            member_id = int(member_key)
        except (TypeError, ValueError):
            continue
        if not isinstance(counters_value, dict):
            continue
        counters: dict[str, int] = {}
        for name, counter_value in counters_value.items():
            normalized = normalize_int(counter_value)
            if normalized is not None:
                counters[str(name)] = normalized
        result[member_id] = counters
    return result


def normalize_summary_record(summary: object) -> dict[str, object] | None:
    if not isinstance(summary, dict):
        return None
    line = summary.get("line")
    if isinstance(line, str):
        parsed = parse_summaries(line)
        if parsed:
            return parsed[-1]

    normalized: dict[str, object] = dict(summary)
    for key in (
        "game_id",
        "link_frames",
        "link_bytes",
        "mode1_frames",
        "mode1_bytes",
        "deliveries",
        "no_target",
        "invalid_stream",
        "invalid_payload",
        "members",
        "distinct_peer_hosts",
    ):
        value = normalize_int(normalized.get(key))
        normalized[key] = value
    if normalized.get("invalid_payload") is None:
        normalized["invalid_payload"] = 0
    parsed_member_peers = normalize_member_peers(
        normalized.get("parsed_member_peers")
    )
    if not parsed_member_peers:
        parsed_member_peers = parse_member_peers(
            normalized.get("member_peers")
            if isinstance(normalized.get("member_peers"), str)
            else None
        )
    normalized["parsed_member_peers"] = parsed_member_peers
    normalized["distinct_peer_endpoints"] = distinct_peer_endpoint_count(
        parsed_member_peers
    )
    member_frames = normalize_member_frames(normalized.get("member_frames"))
    normalized["member_frames"] = member_frames
    normalized["bidirectional_mode1_members"] = bidirectional_mode1_member_count(
        member_frames
    )
    return normalized


def decode_log(data: bytes) -> str:
    for encoding in ("mbcs", "cp949", "utf-8"):
        try:
            return data.decode(encoding, errors="replace")
        except LookupError:
            continue
    return data.decode(errors="replace")


def read_log_tail(path: Path, tail_bytes: int) -> str:
    size = path.stat().st_size
    with path.open("rb") as file:
        if tail_bytes > 0 and size > tail_bytes:
            file.seek(size - tail_bytes)
        return decode_log(file.read())


def parse_summaries(log_text: str) -> list[dict[str, object]]:
    summaries: list[dict[str, object]] = []
    for line_number, line in enumerate(log_text.splitlines(), start=1):
        match = SUMMARY_PATTERN.search(line)
        if match is None:
            continue
        summary: dict[str, object] = {
            "line_number": line_number,
            "line": line,
            "room": match.group("room"),
        }
        for key in (
            "game_id",
            "link_frames",
            "link_bytes",
            "mode1_frames",
            "mode1_bytes",
            "deliveries",
            "no_target",
            "invalid_stream",
        ):
            value = match.group(key)
            summary[key] = int(value) if value is not None else 0
        summary["invalid_payload"] = int(match.group("invalid_payload") or 0)
        for key in ("members", "distinct_peer_hosts"):
            value = match.group(key)
            summary[key] = int(value) if value is not None else None
        summary["member_peers"] = match.group("member_peers")
        parsed_member_peers = parse_member_peers(match.group("member_peers"))
        summary["parsed_member_peers"] = parsed_member_peers
        summary["distinct_peer_endpoints"] = distinct_peer_endpoint_count(
            parsed_member_peers
        )
        member_frames = parse_member_frames(match.group("member_frames"))
        summary["member_frames"] = member_frames
        summary["bidirectional_mode1_members"] = bidirectional_mode1_member_count(
            member_frames
        )
        summaries.append(summary)
    return summaries


def matching_summaries(
    summaries: list[dict[str, object]], room: str | None, game_id: int | None
) -> list[dict[str, object]]:
    matches = summaries
    if room:
        matches = [summary for summary in matches if summary["room"] == room]
    if game_id is not None:
        matches = [summary for summary in matches if summary["game_id"] == game_id]
    return matches


def build_summary_result(
    args: argparse.Namespace,
    *,
    log: str | None,
    tail_bytes: int,
    summary: dict[str, object] | None,
    matched_summary_count: int,
    summary_lines: list[str],
    require_identity_match: bool = False,
) -> dict[str, object]:
    checks = {
        "summary_found": summary is not None,
        "link_frames_min": False,
        "mode1_frames_min": False,
        "deliveries_min": False,
        "no_target_max": False,
        "invalid_stream_max": False,
        "invalid_payload_max": False,
        "members_min": args.min_members <= 0,
        "distinct_peer_hosts_min": args.min_distinct_peer_hosts <= 0,
        "distinct_peer_endpoints_min": args.min_distinct_peer_endpoints <= 0,
        "bidirectional_mode1_members_min": (
            args.min_bidirectional_mode1_members <= 0
        ),
    }
    if require_identity_match:
        checks["room_matches"] = bool(
            summary is not None
            and (not args.room or summary.get("room") == args.room)
        )
        checks["game_id_matches"] = bool(
            summary is not None
            and (
                args.game_id is None
                or summary.get("game_id") == args.game_id
            )
        )
    if summary is not None:
        checks["link_frames_min"] = (
            (summary.get("link_frames") or 0) >= args.min_link_frames
        )
        checks["mode1_frames_min"] = (
            (summary.get("mode1_frames") or 0) >= args.min_mode1_frames
        )
        checks["deliveries_min"] = (
            (summary.get("deliveries") or 0) >= args.min_deliveries
        )
        checks["no_target_max"] = (
            (summary.get("no_target") or 0) <= args.max_no_target
        )
        checks["invalid_stream_max"] = (
            (summary.get("invalid_stream") or 0) <= args.max_invalid_stream
        )
        checks["invalid_payload_max"] = (
            (summary.get("invalid_payload") or 0) <= args.max_invalid_payload
        )
        checks["members_min"] = args.min_members <= 0 or (
            summary.get("members") is not None
            and summary.get("members") >= args.min_members
        )
        checks["distinct_peer_hosts_min"] = (
            args.min_distinct_peer_hosts <= 0
            or (
                summary.get("distinct_peer_hosts") is not None
                and summary.get("distinct_peer_hosts")
                >= args.min_distinct_peer_hosts
            )
        )
        checks["distinct_peer_endpoints_min"] = (
            args.min_distinct_peer_endpoints <= 0
            or (summary.get("distinct_peer_endpoints") or 0)
            >= args.min_distinct_peer_endpoints
        )
        checks["bidirectional_mode1_members_min"] = (
            args.min_bidirectional_mode1_members <= 0
            or (summary.get("bidirectional_mode1_members") or 0)
            >= args.min_bidirectional_mode1_members
        )

    missing = [name for name, ok in checks.items() if not ok]
    return {
        "ok": not missing,
        "log": log,
        "tail_bytes": tail_bytes,
        "room": args.room,
        "game_id": args.game_id,
        "criteria": {
            "min_link_frames": args.min_link_frames,
            "min_mode1_frames": args.min_mode1_frames,
            "min_deliveries": args.min_deliveries,
            "min_members": args.min_members,
            "min_distinct_peer_hosts": args.min_distinct_peer_hosts,
            "min_distinct_peer_endpoints": args.min_distinct_peer_endpoints,
            "min_bidirectional_mode1_members": (
                args.min_bidirectional_mode1_members
            ),
            "max_no_target": args.max_no_target,
            "max_invalid_stream": args.max_invalid_stream,
            "max_invalid_payload": args.max_invalid_payload,
        },
        "checks": checks,
        "missing_checks": missing,
        "summary": summary,
        "matched_summary_count": matched_summary_count,
        "summary_lines": summary_lines,
    }


def check_summary_record(
    summary: object,
    args: argparse.Namespace,
    *,
    log: str | None = None,
    tail_bytes: int = 0,
    matched_summary_count: int = 1,
    summary_lines: list[str] | None = None,
    require_identity_match: bool = True,
) -> dict[str, object]:
    normalized = normalize_summary_record(summary)
    if summary_lines is None:
        line = normalized.get("line") if isinstance(normalized, dict) else None
        summary_lines = [line] if isinstance(line, str) else []
    return build_summary_result(
        args,
        log=log,
        tail_bytes=tail_bytes,
        summary=normalized,
        matched_summary_count=matched_summary_count if normalized else 0,
        summary_lines=summary_lines,
        require_identity_match=require_identity_match,
    )


def check_summary(args: argparse.Namespace) -> dict[str, object]:
    path = args.log.resolve()
    log_text = read_log_tail(path, args.tail_bytes)
    summaries = parse_summaries(log_text)
    matches = matching_summaries(summaries, args.room, args.game_id)
    summary = matches[-1] if matches else None
    return build_summary_result(
        args,
        log=str(path),
        tail_bytes=args.tail_bytes,
        summary=summary,
        matched_summary_count=len(matches),
        summary_lines=[
            summary["line"] for summary in matches[-args.show_lines :]
        ],
    )


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(
        description="Validate WizardNet relay summary evidence in a server log."
    )
    parser.add_argument("log", type=Path, help="Path to the server stderr log.")
    parser.add_argument("--room", help="Expected room name.")
    parser.add_argument("--game-id", type=int, help="Expected server game ID.")
    parser.add_argument("--min-link-frames", type=int, default=1)
    parser.add_argument("--min-mode1-frames", type=int, default=1)
    parser.add_argument("--min-deliveries", type=int, default=1)
    parser.add_argument("--min-members", type=int, default=0)
    parser.add_argument("--min-distinct-peer-hosts", type=int, default=0)
    parser.add_argument("--min-distinct-peer-endpoints", type=int, default=0)
    parser.add_argument("--min-bidirectional-mode1-members", type=int, default=0)
    parser.add_argument("--max-no-target", type=int, default=0)
    parser.add_argument("--max-invalid-stream", type=int, default=0)
    parser.add_argument("--max-invalid-payload", type=int, default=0)
    parser.add_argument("--tail-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--show-lines", type=int, default=5)
    args = parser.parse_args()

    result = check_summary(args)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
