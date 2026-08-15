"""Check reconstructed WizardNet relay evidence in a Ranker Jw2.log file."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Callable


PROBLEM_MARKERS = (
    " blocked",
    " rejected",
    " dropped",
    " ignored",
    "auth-failed",
)

DIRECT_TRANSPORT_MARKERS = (
    "link udp local route",
    "link udp route fallback",
    "link udp probe",
    "link submit blocked missing udp route",
)

GAME_ID_PATTERN = re.compile(r"\bgame=(\d+)\b")

COMMON_RELAY_NEEDLES = {
    "link_frame_queued": ("wizardnet relay frame queued", "stream=0"),
    "relay_payload_encrypted": (
        "wizardnet relay frame queued",
        "wire_bytes=",
        "crypto=yes",
        "crypto_key=room",
    ),
    "link_countdown": ("link countdown timer set",),
    "start_game_called": ("link countdown complete calling start_game",),
}

ROLE_NEEDLES = {
    "host": {
        "host_configured": ("wizardnet relay configured", "host=yes"),
        "host_link_route": ("link relay route initialized", "host=yes"),
        "link_frame_received": ("wizardnet relay link received", "member=2"),
        "mode1_frame_queued": ("wizardnet relay frame queued", "target=2", "stream=1"),
        "mode1_received": ("wizardnet relay mode1 received", "member=2"),
    },
    "joiner": {
        "join_queued": ("wizardnet relay browser join queued",),
        "join_accepted": ("wizardnet relay browser join accepted",),
        "joiner_configured": ("wizardnet relay configured", "host=no"),
        "joiner_link_route": ("link relay route initialized", "host=no"),
        "link_join_accepted": ("link relay join accepted",),
        "link_frame_received": ("wizardnet relay link received", "member=1"),
        "mode1_frame_queued": ("wizardnet relay frame queued", "target=1", "stream=1"),
        "mode1_received": ("wizardnet relay mode1 received", "member=1"),
    },
}


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
        data = file.read()
    return decode_log(data)


def line_matches(line: str, needles: tuple[str, ...]) -> bool:
    return all(needle in line for needle in needles)


def line_game_id(line: str) -> int | None:
    match = GAME_ID_PATTERN.search(line)
    if match is None:
        return None
    return int(match.group(1))


def line_has_game_id(line: str, game_id: int) -> bool:
    return line_game_id(line) == game_id


def has_match(lines: list[str], needles: tuple[str, ...]) -> bool:
    return any(line_matches(line, needles) for line in lines)


def line_allowed_for_game(line: str, game_id: int | None) -> bool:
    if game_id is None:
        return True
    line_id = line_game_id(line)
    return line_id is None or line_id == game_id


def relay_lines(log_text: str, game_id: int | None = None) -> list[str]:
    return [
        line
        for line in log_text.splitlines()
        if (
            "wizardnet relay" in line
            or "link relay" in line
            or "link countdown" in line
        )
        and line_allowed_for_game(line, game_id)
    ]


def problem_lines(lines: list[str]) -> list[str]:
    return [
        line
        for line in lines
        if any(marker in line for marker in PROBLEM_MARKERS)
    ]


def direct_transport_lines(log_text: str, game_id: int | None = None) -> list[str]:
    return [
        line
        for line in log_text.splitlines()
        if any(marker in line for marker in DIRECT_TRANSPORT_MARKERS)
        and line_allowed_for_game(line, game_id)
    ]


def observed_game_ids(lines: list[str]) -> list[int]:
    game_ids = {
        line_id
        for line in lines
        if (line_id := line_game_id(line)) is not None
    }
    return sorted(game_ids)


def text_from_last_matching_line(
    log_text: str, predicate: Callable[[str], bool]
) -> tuple[str, bool]:
    offset = 0
    matched_offset = 0
    matched = False
    for line in log_text.splitlines(keepends=True):
        if predicate(line):
            matched_offset = offset
            matched = True
        offset += len(line)
    return log_text[matched_offset:], matched


def text_from_room_session(log_text: str, room: str) -> tuple[str, str]:
    lines = log_text.splitlines(keepends=True)
    offsets: list[int] = []
    offset = 0
    for line in lines:
        offsets.append(offset)
        offset += len(line)

    room_index = -1
    for index, line in enumerate(lines):
        if room in line:
            room_index = index
    if room_index < 0:
        return log_text, "tail"

    game_id = 0
    match = GAME_ID_PATTERN.search(lines[room_index])
    if match is not None:
        game_id = int(match.group(1))

    if game_id != 0:
        for index in range(room_index, -1, -1):
            line = lines[index]
            if (
                line_has_game_id(line, game_id)
                and "wizardnet relay configured" in line
                and "host=yes" in line
            ):
                return log_text[offsets[index] :], f"room_game_{game_id}"

    return log_text[offsets[room_index] :], "room"


def text_from_game_session(
    log_text: str, game_id: int, role: str
) -> tuple[str, str]:
    role_marker = ""
    if role == "host":
        role_marker = "host=yes"
    elif role == "joiner":
        role_marker = "host=no"

    def matches_configured(line: str) -> bool:
        if (
            "wizardnet relay configured" not in line
            or not line_has_game_id(line, game_id)
        ):
            return False
        return not role_marker or role_marker in line

    lines = log_text.splitlines(keepends=True)
    offsets: list[int] = []
    offset = 0
    for line in lines:
        offsets.append(offset)
        offset += len(line)

    configured_indices: list[int] = []
    for index, line in enumerate(lines):
        if matches_configured(line):
            configured_indices.append(index)

    if configured_indices:
        configured_index = (
            configured_indices[0]
            if role == "combined"
            else configured_indices[-1]
        )
        start_index = configured_index
        if role == "joiner":
            for index in range(configured_index, -1, -1):
                line = lines[index]
                if (
                    line_has_game_id(line, game_id)
                    and "wizardnet relay browser join queued" in line
                ):
                    start_index = index
                    break
                if index != configured_index and "wizardnet relay configured" in line:
                    break
        end_offset = len(log_text)
        end_scan_index = configured_indices[-1]
        for index in range(end_scan_index + 1, len(lines)):
            line = lines[index]
            if (
                "wizardnet relay configured" in line
                and not line_has_game_id(line, game_id)
            ):
                end_offset = offsets[index]
                break
            if (
                role == "joiner"
                and "wizardnet relay browser join queued" in line
                and not line_has_game_id(line, game_id)
            ):
                end_offset = offsets[index]
                break
        scope = (
            f"game_{game_id}_{role}_configured"
            if role in ("host", "joiner")
            else f"game_{game_id}_relay_configured"
        )
        return log_text[offsets[start_index] : end_offset], scope
    return "", f"game_{game_id}_missing"


def text_from_game_room_session(
    log_text: str, game_id: int, room: str, role: str
) -> tuple[str, str]:
    lines = log_text.splitlines(keepends=True)
    offsets: list[int] = []
    offset = 0
    for line in lines:
        offsets.append(offset)
        offset += len(line)

    room_index = -1
    for index, line in enumerate(lines):
        if room in line and line_has_game_id(line, game_id):
            room_index = index
    if room_index < 0:
        return text_from_game_session(log_text, game_id, role)

    def configured_for_role(line: str) -> bool:
        if "wizardnet relay configured" not in line or not line_has_game_id(line, game_id):
            return False
        if role == "host":
            return "host=yes" in line
        if role == "joiner":
            return "host=no" in line
        return True

    start_index = room_index
    if role == "joiner":
        for index in range(room_index, -1, -1):
            line = lines[index]
            if (
                room in line
                and line_has_game_id(line, game_id)
                and "wizardnet relay browser join queued" in line
            ):
                start_index = index
                break
            if "wizardnet relay configured" in line and not line_has_game_id(line, game_id):
                break
    else:
        for index in range(room_index, -1, -1):
            line = lines[index]
            if configured_for_role(line):
                start_index = index
                if role == "host" or "host=yes" in line:
                    break
            if "wizardnet relay configured" in line and not line_has_game_id(line, game_id):
                break

    end_offset = len(log_text)
    for index in range(room_index + 1, len(lines)):
        line = lines[index]
        if " room=" in line and room not in line:
            end_offset = offsets[index]
            break
        if "wizardnet relay configured" in line and not line_has_game_id(line, game_id):
            end_offset = offsets[index]
            break

    scope = (
        f"room_{room}_game_{game_id}_{role}"
        if role in ("host", "joiner")
        else f"room_{room}_game_{game_id}"
    )
    return log_text[offsets[start_index] : end_offset], scope


def session_scope(log_text: str, args: argparse.Namespace) -> tuple[str, str]:
    game_id = getattr(args, "game_id", None)
    if game_id is not None:
        if args.room:
            return text_from_game_room_session(log_text, game_id, args.room, args.role)
        return text_from_game_session(log_text, game_id, args.role)
    if args.room and args.room in log_text:
        return text_from_room_session(log_text, args.room)
    if args.role == "host":
        scoped, matched = text_from_last_matching_line(
            log_text,
            lambda line: "wizardnet relay configured" in line and "host=yes" in line,
        )
        return scoped, "host_configured" if matched else "tail"
    if args.role == "joiner":
        scoped, matched = text_from_last_matching_line(
            log_text,
            lambda line: "wizardnet relay configured" in line and "host=no" in line,
        )
        return scoped, "joiner_configured" if matched else "tail"
    scoped, matched = text_from_last_matching_line(
        log_text, lambda line: "wizardnet relay configured" in line
    )
    return scoped, "relay_configured" if matched else "tail"


def build_checks(
    lines: list[str],
    role: str,
    require_mode1: bool,
    forbid_direct_transport: bool,
    direct_lines: list[str],
) -> dict[str, bool]:
    checks: dict[str, bool] = {
        name: has_match(lines, needles)
        for name, needles in COMMON_RELAY_NEEDLES.items()
    }
    if role == "combined":
        for role_name, role_checks in ROLE_NEEDLES.items():
            for name, needles in role_checks.items():
                checks[f"{role_name}_{name}"] = has_match(lines, needles)
    else:
        for name, needles in ROLE_NEEDLES[role].items():
            checks[name] = has_match(lines, needles)
    if not require_mode1:
        for name in list(checks):
            if "mode1" in name:
                checks.pop(name)
    checks["no_relay_problem_lines"] = not problem_lines(lines)
    if forbid_direct_transport:
        checks["no_direct_transport_lines"] = not direct_lines
    return checks


def required_check_names(
    role: str, require_mode1: bool, forbid_direct_transport: bool
) -> list[str]:
    required = [
        "link_frame_queued",
        "relay_payload_encrypted",
        "link_countdown",
        "start_game_called",
        "no_relay_problem_lines",
    ]
    if role == "combined":
        for role_name, role_checks in ROLE_NEEDLES.items():
            for name in role_checks:
                if require_mode1 or "mode1" not in name:
                    required.append(f"{role_name}_{name}")
    else:
        for name in ROLE_NEEDLES[role]:
            if require_mode1 or "mode1" not in name:
                required.append(name)
    if forbid_direct_transport:
        required.append("no_direct_transport_lines")
    return required


def check_log(args: argparse.Namespace) -> dict[str, object]:
    path = args.log.resolve()
    log_text = read_log_tail(path, args.tail_bytes)
    game_id = getattr(args, "game_id", None)
    room_seen = bool(args.room and args.room in log_text)
    scoped_text, scope = session_scope(log_text, args)
    lines = relay_lines(scoped_text, game_id)
    line_game_ids = observed_game_ids(lines)
    effective_game_id = game_id
    if effective_game_id is None and len(line_game_ids) == 1:
        effective_game_id = line_game_ids[0]
    direct_lines = direct_transport_lines(scoped_text, game_id)
    checks = build_checks(
        lines,
        args.role,
        args.require_mode1,
        args.forbid_direct_transport,
        direct_lines,
    )
    if args.room:
        checks["room_seen"] = room_seen
    missing = [
        name
        for name in required_check_names(
            args.role, args.require_mode1, args.forbid_direct_transport
        )
        if not checks.get(name)
    ]
    if args.room and args.role in ("joiner", "combined") and not checks["room_seen"]:
        missing.append("room_seen")

    problems = problem_lines(lines)
    result: dict[str, object] = {
        "ok": not missing and not problems,
        "log": str(path),
        "role": args.role,
        "game_id": game_id,
        "effective_game_id": effective_game_id,
        "observed_game_ids": line_game_ids,
        "tail_bytes": args.tail_bytes,
        "scope": scope,
        "checks": checks,
        "missing_checks": missing,
        "problem_lines": problems[-20:],
        "direct_transport_lines": direct_lines[-20:],
        "relay_lines": lines[-args.show_lines :],
    }
    return result


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(
        description="Validate WizardNet relay evidence in a reconstructed client log."
    )
    parser.add_argument("log", type=Path, help="Path to Jw2.log")
    parser.add_argument(
        "--role",
        choices=("host", "joiner", "combined"),
        required=True,
        help="Which client role this log should prove.",
    )
    parser.add_argument(
        "--room",
        help="Expected room name. This is required only for joiner or combined evidence.",
    )
    parser.add_argument(
        "--game-id",
        type=int,
        help="Expected server game ID. When set, this scopes checks to that relay session.",
    )
    parser.add_argument(
        "--require-mode1",
        action="store_true",
        help="Require gameplay Mode1 relay send and receive evidence.",
    )
    parser.add_argument(
        "--forbid-direct-transport",
        action="store_true",
        help="Fail if the log shows the Link lobby using direct UDP route/probe paths.",
    )
    parser.add_argument(
        "--tail-bytes",
        type=int,
        default=1024 * 1024,
        help="Only inspect the last N bytes of the log; use 0 for the full file.",
    )
    parser.add_argument(
        "--show-lines",
        type=int,
        default=80,
        help="Number of trailing relay lines to include in JSON output.",
    )
    args = parser.parse_args()

    result = check_log(args)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
