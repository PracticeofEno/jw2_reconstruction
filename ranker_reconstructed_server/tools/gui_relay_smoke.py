"""Drive two reconstructed clients through WizardNet relay create/join."""

from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
from typing import Callable

import relay_server_summary_check

try:
    import win32api
    import win32con
    import win32gui
    import win32process
except ImportError as exc:  # pragma: no cover - Windows-only diagnostic tool.
    raise SystemExit("pywin32 is required to drive the Ranker GUI") from exc


IDOK = 1
WM_CHAR = 0x0102
WM_COMMAND = 0x0111
WM_SETTEXT = 0x000C
LB_SETCURSEL = 0x0186
LB_GETTEXT = 0x0189
LB_GETTEXTLEN = 0x018A
LB_GETCOUNT = 0x018B
LBN_SELCHANGE = 1
CB_SETCURSEL = 0x014E
CBN_SELENDOK = 9

CONNECT_WIZARD_BUTTON_ID = 2000
LOBBY_CREATE_GAME_ID = 3015
LOBBY_JOIN_GAME_ID = 3017
LOGIN_ACCOUNT_EDIT_ID = 5002
LOGIN_PASSWORD_EDIT_ID = 5003
CREATE_GAME_NAME_EDIT_ID = 0x1770
CREATE_GAME_TYPE_COMBO_ID = 0x1772
CREATE_GAME_SCENARIO_LIST_ID = 0x1775
CREATE_GAME_CREATE_BUTTON_ID = 0x177C
JOIN_GAME_LIST_ID = 0x1B5C
JOIN_GAME_JOIN_BUTTON_ID = 0x1B60
LINK_LOBBY_START_BUTTON_ID = 0x09C6

USER32 = ctypes.windll.user32
USER32.SendMessageA.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint,
    ctypes.c_size_t,
    ctypes.c_void_p,
]
USER32.SendMessageA.restype = ctypes.c_ssize_t


@dataclass
class Client:
    label: str
    account: str
    password: str
    process: subprocess.Popen[bytes]
    main_window: int = 0
    windows: dict[str, int] = field(default_factory=dict)


@dataclass
class TcpConnection:
    pid: int
    local: str
    remote: str
    remote_address: str
    remote_port: int
    state: str


@dataclass
class UdpEndpoint:
    pid: int
    local: str


def make_wparam(low: int, high: int = 0) -> int:
    return (low & 0xFFFF) | ((high & 0xFFFF) << 16)


def send_command(hwnd: int, command_id: int, notify: int = 0, control: int = 0) -> None:
    # The rebuilt frontend can enter COM-backed modal/audio work from a
    # WM_COMMAND handler.  A cross-process SendMessage makes that work execute
    # inside an input-synchronous callback and can fail with
    # RPC_E_CANTCALLOUT_ININPUTSYNCCALL.  Real mouse/keyboard commands are
    # queued, so make the smoke driver follow that boundary too.
    if notify != 0:
        win32gui.PostMessage(hwnd, WM_COMMAND, make_wparam(command_id, notify), control)
        return
    win32gui.SendMessage(hwnd, WM_COMMAND, make_wparam(command_id, notify), control)


def all_top_windows_for_pid(pid: int) -> list[int]:
    found: list[int] = []

    def callback(hwnd: int, _: object) -> bool:
        _, window_pid = win32process.GetWindowThreadProcessId(hwnd)
        if window_pid == pid and win32gui.IsWindow(hwnd):
            found.append(hwnd)
        return True

    win32gui.EnumWindows(callback, None)
    return found


def find_main_window(pid: int) -> int:
    candidates = all_top_windows_for_pid(pid)
    visible = [hwnd for hwnd in candidates if win32gui.IsWindowVisible(hwnd)]
    for hwnd in visible:
        if (
            win32gui.GetClassName(hwnd) == "The Ranker"
            and win32gui.GetWindowText(hwnd) == "The Ranker"
        ):
            return hwnd
    return visible[0] if visible else 0


def descendants(root: int) -> list[int]:
    found: list[int] = []

    def callback(hwnd: int, _: object) -> bool:
        found.append(hwnd)
        return True

    if root:
        win32gui.EnumChildWindows(root, callback, None)
    return found


def process_windows(pid: int) -> list[int]:
    windows: list[int] = []
    for top in all_top_windows_for_pid(pid):
        windows.append(top)
        windows.extend(descendants(top))
    seen: set[int] = set()
    unique: list[int] = []
    for hwnd in windows:
        if hwnd not in seen and win32gui.IsWindow(hwnd):
            seen.add(hwnd)
            unique.append(hwnd)
    return unique


def find_descendant(
    root: int,
    *,
    text: str | None = None,
    class_name: str | None = None,
    control_id: int | None = None,
) -> int:
    for hwnd in descendants(root):
        if text is not None and win32gui.GetWindowText(hwnd) != text:
            continue
        if class_name is not None and win32gui.GetClassName(hwnd) != class_name:
            continue
        if control_id is not None and win32gui.GetDlgCtrlID(hwnd) != control_id:
            continue
        return hwnd
    return 0


def find_process_window(
    pid: int,
    *,
    text: str | None = None,
    class_name: str | None = None,
    control_id: int | None = None,
) -> int:
    for hwnd in process_windows(pid):
        if text is not None and win32gui.GetWindowText(hwnd) != text:
            continue
        if class_name is not None and win32gui.GetClassName(hwnd) != class_name:
            continue
        if control_id is not None and win32gui.GetDlgCtrlID(hwnd) != control_id:
            continue
        return hwnd
    return 0


def wait_for(
    description: str,
    condition: Callable[[], object],
    timeout: float,
    interval: float = 0.1,
) -> object:
    deadline = time.monotonic() + timeout
    last_value: object = None
    while time.monotonic() < deadline:
        last_value = condition()
        if last_value:
            return last_value
        time.sleep(interval)
    raise TimeoutError(f"timed out waiting for {description}; last={last_value!r}")


def wait_main(client: Client, timeout: float) -> int:
    hwnd = wait_for(
        f"{client.label} main window",
        lambda: find_main_window(client.process.pid),
        timeout,
    )
    client.main_window = int(hwnd)
    return client.main_window


def wait_child(client: Client, name: str, text: str, timeout: float) -> int:
    hwnd = wait_for(
        f"{client.label} {text} window",
        lambda: find_process_window(client.process.pid, text=text),
        timeout,
    )
    client.windows[name] = int(hwnd)
    return int(hwnd)


def wait_connect_window(client: Client, timeout: float) -> int:
    deadline = time.monotonic() + timeout
    last_hwnd = 0
    while time.monotonic() < deadline:
        last_hwnd = find_process_window(client.process.pid, text="Connect")
        if last_hwnd:
            client.windows["connect"] = last_hwnd
            return last_hwnd
        # Deliver this as ordinary keyboard input.  The title transition does
        # DirectShow/COM work and must not run inside an inter-thread
        # input-synchronous SendMessage callback.
        try:
            win32gui.SetForegroundWindow(client.main_window)
        except win32gui.error:
            pass
        win32api.keybd_event(ord("M"), 0, 0, 0)
        win32api.keybd_event(ord("M"), 0, win32con.KEYEVENTF_KEYUP, 0)
        time.sleep(0.5)
    raise TimeoutError(
        f"timed out waiting for {client.label} Connect window; last={last_hwnd!r}"
    )


def child_by_id(parent: int, control_id: int) -> int:
    hwnd = find_descendant(parent, control_id=control_id)
    if not hwnd:
        raise RuntimeError(f"control id 0x{control_id:x} not found")
    return hwnd


def set_text(parent: int, control_id: int, value: str) -> None:
    hwnd = child_by_id(parent, control_id)
    win32gui.SendMessage(hwnd, WM_SETTEXT, 0, value)


def listbox_count(hwnd: int) -> int:
    return int(win32gui.SendMessage(hwnd, LB_GETCOUNT, 0, 0))


def listbox_text(hwnd: int, index: int) -> str:
    length = int(win32gui.SendMessage(hwnd, LB_GETTEXTLEN, index, 0))
    if length < 0:
        return ""
    buffer = ctypes.create_string_buffer(length + 2)
    USER32.SendMessageA(
        ctypes.c_void_p(hwnd),
        LB_GETTEXT,
        index,
        ctypes.c_void_p(ctypes.addressof(buffer)),
    )
    return buffer.value.decode("mbcs", errors="replace")


def listbox_items(hwnd: int) -> list[str]:
    return [listbox_text(hwnd, index) for index in range(listbox_count(hwnd))]


def scenario_player_count(name: str) -> int:
    match = re.match(r"\((\d+)\)", name.strip())
    if match is None:
        return 0
    return int(match.group(1))


def select_scenario_index(items: list[str], min_players: int) -> int:
    fallback = 0
    for index, item in enumerate(items):
        if item.strip().lower().endswith(".trk"):
            fallback = index
            break
    for index, item in enumerate(items):
        if (
            item.strip().lower().endswith(".trk")
            and scenario_player_count(item) >= min_players
        ):
            return index
    return fallback


def select_listbox(parent: int, listbox: int, control_id: int, index: int) -> None:
    win32gui.PostMessage(listbox, LB_SETCURSEL, index, 0)
    send_command(parent, control_id, LBN_SELCHANGE, listbox)


def dump_window_tree(root: int) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for hwnd in [root, *descendants(root)]:
        try:
            rows.append(
                {
                    "hwnd": f"0x{hwnd:08x}",
                    "class": win32gui.GetClassName(hwnd),
                    "text": win32gui.GetWindowText(hwnd),
                    "id": win32gui.GetDlgCtrlID(hwnd),
                    "visible": bool(win32gui.IsWindowVisible(hwnd)),
                }
            )
        except win32gui.error:
            continue
    return rows


def dump_process_windows(pid: int) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for hwnd in process_windows(pid):
        try:
            parent = win32gui.GetParent(hwnd)
            rows.append(
                {
                    "hwnd": f"0x{hwnd:08x}",
                    "class": win32gui.GetClassName(hwnd),
                    "text": win32gui.GetWindowText(hwnd),
                    "id": win32gui.GetDlgCtrlID(hwnd),
                    "visible": bool(win32gui.IsWindowVisible(hwnd)),
                    "parent": f"0x{parent:08x}" if parent else None,
                }
            )
        except win32gui.error:
            continue
    return rows


def launch_client(
    label: str,
    account: str,
    password: str,
    exe: Path,
    workdir: Path,
    server_host: str,
    server_port: int,
) -> Client:
    env = os.environ.copy()
    env["RANKER_REBUILD_BACKGROUND_TEST"] = "1"
    env["RANKER_RECONSTRUCTED_SERVER_ADDRESS"] = server_host
    env["RANKER_RECONSTRUCTED_SERVER_PORT"] = str(server_port)
    process = subprocess.Popen(
        [str(exe)],
        cwd=str(workdir),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return Client(label=label, account=account, password=password, process=process)


def open_wizard_lobby(client: Client, timeout: float) -> int:
    wait_main(client, timeout)
    connect = wait_connect_window(client, timeout)
    send_command(connect, CONNECT_WIZARD_BUTTON_ID)
    send_command(connect, IDOK)

    login = wait_child(client, "login", "Light", timeout)
    set_text(login, LOGIN_ACCOUNT_EDIT_ID, client.account)
    set_text(login, LOGIN_PASSWORD_EDIT_ID, client.password)
    send_command(login, IDOK)

    lobby = wait_child(client, "lobby", "Lobby", timeout)
    return lobby


def create_room(
    client: Client,
    room_name: str,
    timeout: float,
    min_players: int,
    game_type_index: int | None = None,
    scenario_index_override: int | None = None,
) -> dict[str, object]:
    lobby = client.windows["lobby"]
    send_command(lobby, LOBBY_CREATE_GAME_ID)
    create = wait_child(client, "create_game", "Create Game", timeout)
    set_text(create, CREATE_GAME_NAME_EDIT_ID, room_name)
    if game_type_index is not None:
        game_type_combo = child_by_id(create, CREATE_GAME_TYPE_COMBO_ID)
        win32gui.PostMessage(game_type_combo, CB_SETCURSEL, game_type_index, 0)
        send_command(
            create,
            CREATE_GAME_TYPE_COMBO_ID,
            CBN_SELENDOK,
            game_type_combo,
        )
        time.sleep(0.5)
    scenario_list = child_by_id(create, CREATE_GAME_SCENARIO_LIST_ID)
    wait_for(
        f"{client.label} scenario list",
        lambda: listbox_count(scenario_list) > 0,
        timeout,
    )
    scenario_items = listbox_items(scenario_list)
    scenario_index = (
        scenario_index_override
        if scenario_index_override is not None
        else select_scenario_index(scenario_items, min_players)
    )
    if scenario_index < 0 or scenario_index >= len(scenario_items):
        raise ValueError(
            f"scenario index {scenario_index} is outside {len(scenario_items)} items"
        )
    select_listbox(create, scenario_list, CREATE_GAME_SCENARIO_LIST_ID, scenario_index)
    send_command(create, CREATE_GAME_CREATE_BUTTON_ID)
    link = wait_child(client, "link", "Link", timeout)
    return {
        "scenario_index": scenario_index,
        "scenario": scenario_items[scenario_index] if scenario_items else "",
        "min_players": min_players,
        "game_type_index": game_type_index,
        "scenario_index_override": scenario_index_override,
        "link_hwnd": f"0x{link:08x}",
    }


def join_room(client: Client, room_name: str, timeout: float) -> dict[str, object]:
    lobby = client.windows["lobby"]
    send_command(lobby, LOBBY_JOIN_GAME_ID)
    join = wait_child(client, "join_game", "Join Game", timeout)
    game_list = child_by_id(join, JOIN_GAME_LIST_ID)

    def matching_index() -> int:
        for index, item in enumerate(listbox_items(game_list)):
            if item == room_name:
                return index + 1
        return 0

    match = int(wait_for(
        f"{client.label} room {room_name}",
        matching_index,
        timeout,
        interval=0.25,
    )) - 1
    game_items = listbox_items(game_list)
    select_listbox(join, game_list, JOIN_GAME_LIST_ID, match)
    send_command(join, JOIN_GAME_JOIN_BUTTON_ID)
    link = wait_child(client, "link", "Link", timeout)
    return {
        "join_index": match,
        "visible_games": game_items,
        "link_hwnd": f"0x{link:08x}",
    }


def read_log_suffix(path: Path, start_size: int) -> str:
    if not path.exists():
        return ""
    with path.open("rb") as file:
        file.seek(min(start_size, path.stat().st_size))
        data = file.read()
    return data.decode("mbcs", errors="replace")


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


def parse_tcp_connections(output: str, pids: set[int]) -> list[TcpConnection]:
    rows: list[TcpConnection] = []
    for line in output.splitlines():
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
            TcpConnection(
                pid=pid,
                local=parts[1],
                remote=parts[2],
                remote_address=remote_address,
                remote_port=remote_port,
                state=parts[3].upper(),
            )
        )
    return rows


def parse_udp_endpoints(output: str, pids: set[int]) -> list[UdpEndpoint]:
    rows: list[UdpEndpoint] = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 4 or parts[0].upper() != "UDP":
            continue
        try:
            pid = int(parts[-1])
        except ValueError:
            continue
        if pid in pids:
            rows.append(UdpEndpoint(pid=pid, local=parts[1]))
    return rows


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


def relay_only_network_summary(
    clients: list[Client], server_host: str, server_port: int
) -> dict[str, object]:
    live_pids = {
        client.process.pid
        for client in clients
        if client.process.poll() is None
    }
    tcp_connections = parse_tcp_connections(netstat_output("TCP"), live_pids)
    udp_endpoints = parse_udp_endpoints(netstat_output("UDP"), live_pids)
    allowed_addresses = resolve_server_addresses(server_host)
    established = [
        connection
        for connection in tcp_connections
        if connection.state == "ESTABLISHED"
    ]
    listening = [
        connection
        for connection in tcp_connections
        if connection.state == "LISTENING"
    ]
    unexpected = [
        connection
        for connection in established
        if connection.remote_port != server_port
        or connection.remote_address not in allowed_addresses
    ]
    server_connections = [
        connection
        for connection in established
        if connection.remote_port == server_port
        and connection.remote_address in allowed_addresses
    ]
    server_connection_counts = {
        pid: sum(1 for connection in server_connections if connection.pid == pid)
        for pid in live_pids
    }
    return {
        "allowed_server_addresses": sorted(allowed_addresses),
        "tcp_established": [connection.__dict__ for connection in established],
        "tcp_listening": [connection.__dict__ for connection in listening],
        "tcp_unexpected": [connection.__dict__ for connection in unexpected],
        "udp_endpoints": [endpoint.__dict__ for endpoint in udp_endpoints],
        "server_tcp_connection_counts": server_connection_counts,
        "checks": {
            "one_server_tcp_per_process": bool(live_pids)
            and all(count == 1 for count in server_connection_counts.values()),
            "only_server_tcp_established": not unexpected,
            "no_tcp_listeners": not listening,
            "no_udp_endpoints": not udp_endpoints,
        },
    }


def relay_member_ids(lines: list[str], marker: str) -> list[int]:
    member_ids: set[int] = set()
    pattern = re.compile(r"\bmember=(\d+)\b")
    for line in lines:
        if marker not in line:
            continue
        match = pattern.search(line)
        if match is not None:
            member_ids.add(int(match.group(1)))
    return sorted(member_ids)


def relay_target_ids(lines: list[str], marker: str) -> list[int]:
    target_ids: set[int] = set()
    pattern = re.compile(r"\btarget=(\d+)\b")
    for line in lines:
        if "wizardnet relay frame queued" not in line or marker not in line:
            continue
        match = pattern.search(line)
        if match is not None and int(match.group(1)) != 0:
            target_ids.add(int(match.group(1)))
    return sorted(target_ids)


def relay_log_summary(
    log_text: str, room_name: str, expected_players: int
) -> dict[str, object]:
    relay_lines = [
        line
        for line in log_text.splitlines()
        if "wizardnet relay" in line or "link relay" in line or "link countdown" in line
    ]
    configured_members = relay_member_ids(relay_lines, "wizardnet relay configured")
    joined_members = relay_member_ids(relay_lines, "wizardnet relay browser join accepted")
    link_received_members = relay_member_ids(
        relay_lines, "wizardnet relay link received"
    )
    mode1_received_members = relay_member_ids(
        relay_lines, "wizardnet relay mode1 received"
    )
    mode1_target_members = relay_target_ids(relay_lines, "stream=1")
    direct_transport_lines = [
        line
        for line in log_text.splitlines()
        if any(
            marker in line
            for marker in (
                "link udp local route",
                "link udp route fallback",
                "link udp probe",
                "link submit blocked missing udp route",
            )
        )
    ]
    problem_lines = [
        line
        for line in relay_lines
        if any(
            marker in line
            for marker in (
                " blocked",
                " rejected",
                " dropped",
                " ignored",
                "auth-failed",
            )
        )
    ]
    checks = {
        "host_configured": "wizardnet relay configured" in log_text and "host=yes" in log_text,
        "join_queued": "wizardnet relay browser join queued" in log_text,
        "join_accepted": "wizardnet relay browser join accepted" in log_text,
        "joiner_configured": "wizardnet relay configured" in log_text and "host=no" in log_text,
        "link_frame_queued": "wizardnet relay frame queued" in log_text and "stream=0" in log_text,
        "relay_payload_encrypted": (
            "wizardnet relay frame queued" in log_text
            and "wire_bytes=" in log_text
            and "crypto=yes" in log_text
            and "crypto_key=room" in log_text
        ),
        "link_frame_received": "wizardnet relay link received" in log_text,
        "link_countdown": "link countdown timer set" in log_text,
        "start_game_called": "link countdown complete calling start_game" in log_text,
        "mode1_frame_queued": "wizardnet relay frame queued" in log_text and "stream=1" in log_text,
        "mode1_received": "wizardnet relay mode1 received" in log_text,
        "expected_configured_members": len(configured_members) >= expected_players,
        "expected_joined_members": len(joined_members) >= max(0, expected_players - 1),
        "expected_link_received_members": len(link_received_members) >= min(2, expected_players),
        "expected_mode1_members": len(mode1_received_members) >= expected_players,
        "expected_mode1_targets": len(mode1_target_members) >= expected_players,
        "no_relay_problem_lines": not problem_lines,
        "no_direct_transport_logs": not direct_transport_lines,
    }
    return {
        "room_seen": room_name in log_text,
        "expected_players": expected_players,
        "configured_members": configured_members,
        "joined_members": joined_members,
        "link_received_members": link_received_members,
        "mode1_received_members": mode1_received_members,
        "mode1_target_members": mode1_target_members,
        "checks": checks,
        "problem_lines": problem_lines[-20:],
        "direct_transport_lines": direct_transport_lines[-20:],
        "relay_lines": relay_lines[-80:],
    }


def require_relay_checks(
    result: dict[str, object], *, start_game: bool, assert_relay_only: bool
) -> None:
    log_summary = result.get("log")
    if not isinstance(log_summary, dict):
        raise RuntimeError("relay log summary is missing")
    checks = log_summary.get("checks")
    if not isinstance(checks, dict):
        raise RuntimeError("relay log checks are missing")

    required = [
        "host_configured",
        "join_queued",
        "join_accepted",
        "joiner_configured",
        "link_frame_queued",
        "relay_payload_encrypted",
        "link_frame_received",
        "no_relay_problem_lines",
    ]
    if assert_relay_only:
        required.append("no_direct_transport_logs")
    if start_game:
        required.extend([
            "link_countdown",
        "start_game_called",
        "mode1_frame_queued",
        "mode1_received",
        "expected_configured_members",
        "expected_joined_members",
        "expected_link_received_members",
        "expected_mode1_members",
        "expected_mode1_targets",
        ])

    missing = [name for name in required if not checks.get(name)]
    if missing:
        result["missing_checks"] = missing
        raise RuntimeError(json.dumps(result, ensure_ascii=False, indent=2))
    if assert_relay_only:
        network_summary = result.get("network")
        if not isinstance(network_summary, dict):
            raise RuntimeError("relay-only network summary is missing")
        network_checks = network_summary.get("checks")
        if not isinstance(network_checks, dict):
            raise RuntimeError("relay-only network checks are missing")
        if (
            not network_checks.get("one_server_tcp_per_process")
            or not network_checks.get("only_server_tcp_established")
            or not network_checks.get("no_tcp_listeners")
            or not network_checks.get("no_udp_endpoints")
        ):
            raise RuntimeError(json.dumps(result, ensure_ascii=False, indent=2))


def stop_clients(clients: list[Client]) -> None:
    for client in reversed(clients):
        if client.process.poll() is None:
            client.process.terminate()
            try:
                client.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                client.process.kill()
                client.process.wait(timeout=3)


def server_summary_args(args: argparse.Namespace, room_name: str) -> argparse.Namespace:
    min_members = (
        args.server_min_members
        if args.server_min_members is not None
        else args.joiner_count + 1
    )
    min_bidirectional_mode1_members = (
        args.server_min_bidirectional_mode1_members
        if args.server_min_bidirectional_mode1_members is not None
        else (args.joiner_count + 1 if args.start_game else 0)
    )
    min_distinct_peer_endpoints = (
        args.server_min_distinct_peer_endpoints
        if args.server_min_distinct_peer_endpoints is not None
        else args.joiner_count + 1
    )
    return argparse.Namespace(
        log=args.server_log,
        room=room_name,
        game_id=None,
        min_link_frames=args.server_min_link_frames,
        min_mode1_frames=args.server_min_mode1_frames,
        min_deliveries=args.server_min_deliveries,
        min_members=min_members,
        min_distinct_peer_hosts=args.server_min_distinct_peer_hosts,
        min_distinct_peer_endpoints=min_distinct_peer_endpoints,
        min_bidirectional_mode1_members=min_bidirectional_mode1_members,
        max_no_target=args.server_max_no_target,
        max_invalid_stream=args.server_max_invalid_stream,
        max_invalid_payload=args.server_max_invalid_payload,
        tail_bytes=args.server_summary_tail_bytes,
        show_lines=5,
    )


def wait_for_server_summary(
    args: argparse.Namespace, room_name: str
) -> dict[str, object]:
    deadline = time.time() + args.server_summary_timeout
    last_result: dict[str, object] | None = None
    while True:
        try:
            last_result = relay_server_summary_check.check_summary(
                server_summary_args(args, room_name)
            )
        except FileNotFoundError as exc:
            last_result = {
                "ok": False,
                "error": str(exc),
                "checks": {"summary_found": False},
                "missing_checks": ["summary_found"],
            }
        if last_result.get("checks", {}).get("summary_found"):
            return last_result
        if time.time() >= deadline:
            return last_result
        time.sleep(0.25)


def run(args: argparse.Namespace) -> dict[str, object]:
    exe = args.exe.resolve()
    workdir = args.workdir.resolve()
    log_path = workdir / "Jw2.log"
    log_start = log_path.stat().st_size if log_path.exists() else 0

    suffix = str(int(time.time() * 1000))[-6:]
    if args.joiner_count < 1 or args.joiner_count > 7:
        raise ValueError("--joiner-count must be between 1 and 7")
    room_name = args.room_name or f"GR{suffix}"
    host = launch_client(
        "host",
        args.host_account or f"GH{suffix}",
        args.password,
        exe,
        workdir,
        args.server_host,
        args.server_port,
    )
    joiner: Client | None = None
    clients = [host]
    joiner_results: list[dict[str, object]] = []
    result: dict[str, object] = {
        "room_name": room_name,
        "server": f"{args.server_host}:{args.server_port}",
        "joiner_count": args.joiner_count,
        "host_pid": host.process.pid,
        "joiners": joiner_results,
    }
    try:
        open_wizard_lobby(host, args.timeout)
        result["host_create"] = create_room(
            host,
            room_name,
            args.timeout,
            args.joiner_count + 1,
            args.game_type_index,
            args.scenario_index,
        )

        for index in range(args.joiner_count):
            account = (
                args.join_account
                if args.join_account is not None and args.joiner_count == 1
                else f"GJ{index + 1}{suffix}"
            )
            joiner = launch_client(
                f"joiner{index + 1}",
                account,
                args.password,
                exe,
                workdir,
                args.server_host,
                args.server_port,
            )
            clients.append(joiner)
            open_wizard_lobby(joiner, args.timeout)
            join_result = join_room(joiner, room_name, args.timeout)
            joiner_entry = {
                "index": index + 1,
                "pid": joiner.process.pid,
                "account": account,
                **join_result,
            }
            joiner_results.append(joiner_entry)
            if index == 0:
                result["joiner_pid"] = joiner.process.pid
                result["joiner_join"] = join_result

        if args.start_game:
            time.sleep(args.start_delay)
            send_command(host.windows["link"], LINK_LOBBY_START_BUTTON_ID)
            wait_for(
                "relay start log",
                lambda: "link relay submit posted start decision"
                in read_log_suffix(log_path, log_start),
                args.timeout,
                interval=0.25,
            )
            result["start_clicked"] = True

        time.sleep(args.post_wait)
        log_text = read_log_suffix(log_path, log_start)
        result["log"] = relay_log_summary(
            log_text, room_name, args.joiner_count + 1
        )
        if args.assert_relay_only:
            result["network"] = relay_only_network_summary(
                clients, args.server_host, args.server_port
            )
        require_relay_checks(
            result,
            start_game=args.start_game,
            assert_relay_only=args.assert_relay_only,
        )
        if args.server_log is not None:
            if args.keep_open:
                raise RuntimeError("--server-log cannot be used with --keep-open")
            stop_clients(clients)
            result["server_summary"] = wait_for_server_summary(args, room_name)
            if not result["server_summary"].get("ok"):
                raise RuntimeError(json.dumps(result, ensure_ascii=False, indent=2))
        return result
    except Exception as exc:
        log_text = read_log_suffix(log_path, log_start)
        result["error"] = str(exc)
        result["log"] = relay_log_summary(
            log_text, room_name, args.joiner_count + 1
        )
        if args.assert_relay_only:
            try:
                result["network"] = relay_only_network_summary(
                    clients, args.server_host, args.server_port
                )
            except Exception as network_exc:
                result["network_error"] = str(network_exc)
        result["windows"] = {
            client.label: dump_process_windows(client.process.pid)
            for client in clients
            if client.main_window
        }
        raise RuntimeError(json.dumps(result, ensure_ascii=False, indent=2)) from exc
    finally:
        if not args.keep_open:
            stop_clients(clients)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--exe",
        type=Path,
        default=Path(r"C:\Users\eno\Desktop\jw_resversing\RankerOCPV_Win\ranker_rebuild.exe"),
    )
    parser.add_argument(
        "--workdir",
        type=Path,
        default=Path(r"C:\Users\eno\Desktop\jw_resversing\RankerOCPV_Win"),
    )
    parser.add_argument("--server-host", default="127.0.0.1")
    parser.add_argument("--server-port", type=int, default=19777)
    parser.add_argument("--room-name")
    parser.add_argument("--host-account")
    parser.add_argument("--join-account")
    parser.add_argument("--joiner-count", type=int, default=1)
    parser.add_argument("--password", default="pw")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--post-wait", type=float, default=1.0)
    parser.add_argument("--game-type-index", type=int)
    parser.add_argument("--scenario-index", type=int)
    parser.add_argument("--start-game", action="store_true")
    parser.add_argument("--start-delay", type=float, default=1.0)
    parser.add_argument("--assert-relay-only", action="store_true")
    parser.add_argument("--server-log", type=Path)
    parser.add_argument("--server-summary-timeout", type=float, default=10.0)
    parser.add_argument("--server-summary-tail-bytes", type=int, default=1024 * 1024)
    parser.add_argument("--server-min-link-frames", type=int, default=1)
    parser.add_argument("--server-min-mode1-frames", type=int, default=1)
    parser.add_argument("--server-min-deliveries", type=int, default=1)
    parser.add_argument("--server-min-members", type=int)
    parser.add_argument("--server-min-distinct-peer-hosts", type=int, default=0)
    parser.add_argument("--server-min-distinct-peer-endpoints", type=int)
    parser.add_argument("--server-min-bidirectional-mode1-members", type=int)
    parser.add_argument("--server-max-no-target", type=int, default=0)
    parser.add_argument("--server-max-invalid-stream", type=int, default=0)
    parser.add_argument("--server-max-invalid-payload", type=int, default=0)
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()

    result = run(args)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
