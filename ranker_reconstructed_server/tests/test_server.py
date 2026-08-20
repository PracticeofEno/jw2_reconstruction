from __future__ import annotations

import asyncio
import json
from pathlib import Path
import socket
import struct
import tempfile
import time
import unittest

from ranker_server.app import RankerServer
from ranker_server.config import ServerConfig
from ranker_server.protocol import (
    HEADER_BYTES,
    StreamDecoder,
    build_colored_text_packet,
    build_packet,
    read_c_string,
    read_i32,
    read_u32,
)
from ranker_server.replays import fnv1a64
from ranker_server.state import (
    ClientSession,
    PRESENCE_STATUS_HOSTING,
    PRESENCE_STATUS_LOBBY,
    PRESENCE_STATUS_PLAYING,
    PRESENCE_STATUS_ROOM_MEMBER,
)

RELAY_SECRET_BYTES = 32
RELAY_CIPHER_HEADER_BYTES = 28


def login_packet(account: str, password: str) -> bytes:
    payload = bytearray(0x11D - HEADER_BYTES)
    payload[0 : len(account)] = account.encode("ascii")
    payload[0x80 : 0x80 + len(password)] = password.encode("ascii")
    return build_packet(1, payload)


def create_account_packet(account: str, password: str) -> bytes:
    payload = bytearray(0x9D - HEADER_BYTES)
    payload[0 : len(account)] = account.encode("ascii")
    payload[0x20 : 0x20 + len(password)] = password.encode("ascii")
    payload[0x50 : 0x50 + 12] = b"Local player"
    struct.pack_into("<IIII", payload, 0x70, 1, 0, 1980, 0)
    return build_packet(3, payload)


def lobby_mark_packet(mark_index: int) -> bytes:
    return build_packet(0x96, struct.pack("<I", mark_index))


def relay_secret(packet: bytes) -> bytes:
    return packet[0x19 : 0x19 + RELAY_SECRET_BYTES]


def relay_wire_payload(payload: bytes) -> bytes:
    return b"WRL1" + struct.pack("<I", len(payload)) + b"\0" * 20 + payload


def relay_plain_payload(payload: bytes) -> bytes:
    assert payload[:4] == b"WRL1"
    plain_bytes = read_u32(payload, 4)
    assert plain_bytes == len(payload) - RELAY_CIPHER_HEADER_BYTES
    return payload[RELAY_CIPHER_HEADER_BYTES:]


async def read_packet(reader: asyncio.StreamReader) -> bytes:
    header = await reader.readexactly(12)
    packet_bytes = read_u32(header, 8)
    return header + await reader.readexactly(packet_bytes - 12)


async def read_until_opcode(
    reader: asyncio.StreamReader, opcode: int, *, limit: int = 20
) -> bytes:
    for _ in range(limit):
        packet = await asyncio.wait_for(read_packet(reader), timeout=2.0)
        if read_u32(packet, 4) == opcode:
            return packet
    raise AssertionError(f"opcode 0x{opcode:02x} was not received")


async def read_until_presence(
    reader: asyncio.StreamReader, account: str, *, limit: int = 40
) -> bytes:
    for _ in range(limit):
        packet = await asyncio.wait_for(read_packet(reader), timeout=2.0)
        if (
            read_u32(packet, 4) == 7
            and read_c_string(packet, 0x0D, 0x20) == account
        ):
            return packet
    raise AssertionError(f"presence for {account!r} was not received")


async def wait_until(predicate, *, timeout: float = 1.0) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    while not predicate():
        if asyncio.get_running_loop().time() >= deadline:
            raise AssertionError("condition was not reached before timeout")
        await asyncio.sleep(0.01)


def host_game_packet(
    name: str = "RelayRoom",
    *,
    port: int = 23010,
    map_name: str = "RelayMap",
    game_type: int = 0,
) -> bytes:
    request = bytearray(0x409 - HEADER_BYTES)
    request[0 : len(name)] = name.encode("ascii")[:0x80]
    sockaddr = (
        struct.pack("<H", socket.AF_INET)
        + struct.pack(">H", port)
        + socket.inet_aton("127.0.0.1")
        + b"\0" * 8
    )
    request[0x10D - HEADER_BYTES : 0x11D - HEADER_BYTES] = sockaddr
    struct.pack_into("<I", request, 0x11D - HEADER_BYTES, game_type)
    encoded_map = map_name.encode("ascii")[:0x20]
    map_offset = 0x12D - HEADER_BYTES + 8
    request[map_offset : map_offset + len(encoded_map)] = encoded_map
    return build_packet(0x19, request)


class SlowDrainWriter:
    def __init__(self) -> None:
        self.closed = False
        self.written = bytearray()

    def is_closing(self) -> bool:
        return self.closed

    def write(self, data: bytes) -> None:
        self.written.extend(data)

    async def drain(self) -> None:
        await asyncio.sleep(60)

    def close(self) -> None:
        self.closed = True


class ServerIntegrationTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        config = ServerConfig(host="127.0.0.1", port=0, room_ttl_seconds=60)
        self.server = RankerServer(config)
        await self.server.start()
        self.connections: list[tuple[asyncio.StreamReader, asyncio.StreamWriter]] = []

    async def asyncTearDown(self) -> None:
        for _, writer in self.connections:
            writer.close()
            await writer.wait_closed()
        await self.server.close()

    async def connect_and_login(
        self, account: str, password: str = "pw"
    ) -> tuple[asyncio.StreamReader, asyncio.StreamWriter]:
        reader, writer = await asyncio.open_connection(
            "127.0.0.1", self.server.bound_port
        )
        self.connections.append((reader, writer))
        writer.write(login_packet(account, password))
        await writer.drain()
        response = await read_packet(reader)
        self.assertEqual(read_u32(response, 4), 2)
        self.assertEqual(read_u32(response, 0x0D), 0)
        return reader, writer

    async def test_slow_client_send_is_closed_after_timeout(self) -> None:
        self.server.config.send_timeout_seconds = 0.01
        writer = SlowDrainWriter()
        session = ClientSession(999, None, writer, "127.0.0.1", 12345)

        await self.server._send(session, b"payload")

        self.assertEqual(bytes(writer.written), b"payload")
        self.assertTrue(writer.closed)

    async def advertise_relay_game(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        name: str = "RelayRoom",
        game_type: int = 0,
    ) -> int:
        writer.write(host_game_packet(name, game_type=game_type))
        await writer.drain()
        hosted = await read_until_opcode(reader, 0x1A)
        self.assertEqual(read_u32(hosted, 0x0D), 1)
        game_id = read_u32(hosted, 0x11)
        self.assertNotEqual(game_id, 0)
        self.assertEqual(read_u32(hosted, 0x15), 1)
        secret = relay_secret(hosted)
        self.assertEqual(len(secret), RELAY_SECRET_BYTES)
        self.assertNotEqual(secret, b"\0" * RELAY_SECRET_BYTES)
        return game_id

    async def test_login_and_online_user_paging(self) -> None:
        alice_reader, alice_writer = await self.connect_and_login("Alice")
        _, bob_writer = await self.connect_and_login("Bob")

        # Alice receives Bob's real-time presence record first.
        presence = await read_packet(alice_reader)
        self.assertEqual(read_u32(presence, 4), 7)
        self.assertEqual(read_c_string(presence, 0x0D, 0x20), "Bob")

        alice_writer.write(build_packet(0x12, struct.pack("<I", 0)))
        await alice_writer.drain()
        page = await read_packet(alice_reader)
        self.assertEqual(read_u32(page, 4), 0x13)
        self.assertEqual(read_c_string(page, 0x15, 0x20), "Alice")
        bob_writer.close()

    async def test_lobby_mark_is_saved_and_broadcast_in_presence(self) -> None:
        alice_reader, alice_writer = await self.connect_and_login("Alice")
        bob_reader, bob_writer = await self.connect_and_login("Bob")

        initial_presence = await read_packet(alice_reader)
        self.assertEqual(read_u32(initial_presence, 4), 7)
        self.assertEqual(read_u32(initial_presence, 0x59), 0)

        bob_writer.write(lobby_mark_packet(4))
        await bob_writer.drain()
        acknowledgement = await read_until_opcode(bob_reader, 0x97)
        self.assertEqual(read_u32(acknowledgement, 0x0D), 0)
        self.assertEqual(read_u32(acknowledgement, 0x11), 4)
        self.assertEqual(self.server.accounts.profile_value("Bob", "lobby_mark"), 4)

        alice_presence = await read_until_opcode(alice_reader, 7)
        self.assertEqual(read_c_string(alice_presence, 0x0D, 0x20), "Bob")
        self.assertEqual(read_u32(alice_presence, 0x59), 4)

        alice_writer.write(build_packet(0x12, struct.pack("<I", 1)))
        await alice_writer.drain()
        bob_page = await read_until_opcode(alice_reader, 0x13)
        self.assertEqual(read_c_string(bob_page, 0x15, 0x20), "Bob")
        self.assertEqual(read_u32(bob_page, 0x5D), 4)

        bob_writer.write(lobby_mark_packet(5))
        await bob_writer.drain()
        rejected = await read_until_opcode(bob_reader, 0x97)
        self.assertEqual(read_u32(rejected, 0x0D), 1)
        self.assertEqual(read_u32(rejected, 0x11), 4)
        self.assertEqual(self.server.accounts.profile_value("Bob", "lobby_mark"), 4)

        bob_writer.close()
        await bob_writer.wait_closed()
        await wait_until(lambda: self.server.state.find_client_by_account("Bob") is None)
        _, _ = await self.connect_and_login("Bob")
        restored_presence = await read_until_opcode(alice_reader, 7)
        self.assertEqual(read_c_string(restored_presence, 0x0D, 0x20), "Bob")
        self.assertEqual(read_u32(restored_presence, 0x59), 4)

    async def test_client_socket_uses_disconnect_keepalive(self) -> None:
        _, _ = await self.connect_and_login("Keepalive")
        session = next(iter(self.server.state.clients.values()))
        server_socket = session.writer.get_extra_info("socket")
        self.assertIsNotNone(server_socket)
        self.assertEqual(
            server_socket.getsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE), 1
        )
        for option_name, expected in (
            ("TCP_KEEPIDLE", self.server.config.client_keepalive_idle_seconds),
            ("TCP_KEEPINTVL", self.server.config.client_keepalive_interval_seconds),
            ("TCP_KEEPCNT", self.server.config.client_keepalive_probe_count),
        ):
            if hasattr(socket, option_name):
                self.assertEqual(
                    server_socket.getsockopt(
                        socket.IPPROTO_TCP, getattr(socket, option_name)
                    ),
                    expected,
                )

    async def test_lobby_reconnect_resynchronizes_all_member_presence(self) -> None:
        alice_reader, _ = await self.connect_and_login("Alice")
        bob_reader, bob_writer = await self.connect_and_login("Bob")

        # Consume Bob's initial real-time login notification on Alice.
        initial_presence = await read_packet(alice_reader)
        self.assertEqual(read_u32(initial_presence, 4), 7)
        self.assertEqual(read_c_string(initial_presence, 0x0D, 0x20), "Bob")

        # Returning from a P2P game must produce a complete snapshot even when
        # the other client returned while this client was outside the lobby UI.
        bob_writer.write(build_packet(0x0E, b"\0" * 8))
        await bob_writer.drain()
        returned_names = {
            read_c_string(await read_packet(bob_reader), 0x0D, 0x20),
            read_c_string(await read_packet(bob_reader), 0x0D, 0x20),
        }
        self.assertEqual(returned_names, {"Alice", "Bob"})

        repeated_presence = await read_packet(alice_reader)
        self.assertEqual(read_u32(repeated_presence, 4), 7)
        self.assertEqual(read_c_string(repeated_presence, 0x0D, 0x20), "Bob")

    async def test_online_presence_tracks_room_and_gameplay_status(self) -> None:
        host_reader, host_writer = await self.connect_and_login("StatusHost")
        join_reader, join_writer = await self.connect_and_login("StatusJoin")
        observer_reader, observer_writer = await self.connect_and_login(
            "StatusObserver"
        )

        # Drain the initial login notifications from clients that connected
        # before the observer. The observer remains in the online-lobby view.
        await read_until_presence(host_reader, "StatusJoin")
        await read_until_presence(host_reader, "StatusObserver")
        await read_until_presence(join_reader, "StatusObserver")

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "StatusRoom"
        )
        hosting = await read_until_presence(observer_reader, "StatusHost")
        self.assertEqual(read_u32(hosting, 0x61), PRESENCE_STATUS_HOSTING)
        self.assertEqual(read_c_string(hosting, 0x65, 0x20), "StatusRoom")

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        await read_until_opcode(join_reader, 0x93, limit=40)
        await read_until_opcode(host_reader, 0x94, limit=40)
        room_member = await read_until_presence(observer_reader, "StatusJoin")
        self.assertEqual(
            read_u32(room_member, 0x61), PRESENCE_STATUS_ROOM_MEMBER
        )
        self.assertEqual(read_c_string(room_member, 0x65, 0x20), "StatusRoom")

        host_writer.write(build_packet(0x28, b"\0" * 0x20))
        await host_writer.drain()
        playing = {
            read_c_string(packet, 0x0D, 0x20): read_u32(packet, 0x61)
            for packet in (
                await read_until_opcode(observer_reader, 7, limit=40),
                await read_until_opcode(observer_reader, 7, limit=40),
            )
        }
        self.assertEqual(
            playing,
            {
                "StatusHost": PRESENCE_STATUS_PLAYING,
                "StatusJoin": PRESENCE_STATUS_PLAYING,
            },
        )

        join_writer.write(build_packet(0x0E, b"\0" * 8))
        await join_writer.drain()
        returned = await read_until_presence(observer_reader, "StatusJoin")
        self.assertEqual(read_u32(returned, 0x61), PRESENCE_STATUS_LOBBY)
        self.assertEqual(read_c_string(returned, 0x65, 0x20), "")

        observer_writer.write(build_packet(0x12, struct.pack("<I", 1)))
        await observer_writer.drain()
        paged = await read_until_opcode(observer_reader, 0x13, limit=40)
        self.assertEqual(read_c_string(paged, 0x15, 0x20), "StatusJoin")
        self.assertEqual(read_u32(paged, 0x65), PRESENCE_STATUS_LOBBY)
        self.assertEqual(read_c_string(paged, 0x69, 0x20), "")

    async def test_host_advertisement_is_returned_to_join_browser(self) -> None:
        host_reader, host_writer = await self.connect_and_login("Host")
        join_reader, join_writer = await self.connect_and_login("Joiner")

        # The host receives Joiner's real-time lobby presence notification.
        presence = await read_packet(host_reader)
        self.assertEqual(read_u32(presence, 4), 7)

        request = bytearray(0x409 - HEADER_BYTES)
        request[0:8] = b"TestRoom"
        sockaddr = (
            struct.pack("<H", socket.AF_INET)
            + struct.pack(">H", 23000)
            + socket.inet_aton("127.0.0.1")
            + b"\0" * 8
        )
        request[0x10D - HEADER_BYTES : 0x11D - HEADER_BYTES] = sockaddr
        struct.pack_into("<I", request, 0x11D - HEADER_BYTES, 1)
        descriptor = bytearray(0x2DC)
        descriptor[8:16] = b"TestMap\0"
        request[0x12D - HEADER_BYTES :] = descriptor
        host_writer.write(build_packet(0x19, request))
        await host_writer.drain()

        hosted = await read_packet(host_reader)
        self.assertEqual(read_u32(hosted, 4), 0x1A)
        self.assertEqual(read_u32(hosted, 0x0D), 1)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        listed = await read_packet(join_reader)
        self.assertEqual(read_u32(listed, 4), 0x1E)
        self.assertEqual(read_c_string(listed, 0x0D, 0x80), "TestRoom")
        self.assertEqual(read_i32(listed, 0xAD), 0)
        self.assertEqual(listed[0x8D:0x9D], sockaddr)
        self.assertEqual(listed[0xB9 + 8 : 0xB9 + 16], b"TestMap\0")

        # A joining client only selected this advertisement; it does not own
        # it and must not be able to retire the host's room.
        join_writer.write(build_packet(0x1B, struct.pack("<I", 1)))
        await join_writer.drain()
        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        still_listed = await read_packet(join_reader)
        self.assertEqual(read_u32(still_listed, 4), 0x1E)
        self.assertEqual(read_c_string(still_listed, 0x0D, 0x80), "TestRoom")

        # A host request can race the preceding lobby-reconnect packet when a
        # player leaves a room and immediately creates another one.  Reusing
        # the same room name must replace that session's own advertisement,
        # not fail the duplicate-name check.
        host_writer.write(build_packet(0x19, request))
        await host_writer.drain()
        rehosted = await read_packet(host_reader)
        self.assertEqual(read_u32(rehosted, 4), 0x1A)
        self.assertEqual(read_u32(rehosted, 0x0D), 1)
        replaced = await read_packet(join_reader)
        readded = await read_packet(join_reader)
        self.assertEqual(read_u32(replaced, 4), 0x26)
        self.assertEqual(read_u32(readded, 4), 0x27)

        # Leaving the hosted Link room reconnects the same authenticated
        # session to WizardNet.  The advertisement must disappear at that
        # transition rather than remaining as a stale, unjoinable room.
        host_writer.write(build_packet(0x0E, b"\0" * 8))
        await host_writer.drain()
        removed = await read_packet(join_reader)
        self.assertEqual(read_u32(removed, 4), 0x26)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        empty = await read_packet(join_reader)
        self.assertEqual(read_u32(empty, 4), 0x1E)
        self.assertEqual(read_i32(empty, 0xAD), -1)

    async def test_abrupt_host_disconnect_removes_advertised_game(self) -> None:
        host_reader, host_writer = await self.connect_and_login("ForceHost")
        join_reader, join_writer = await self.connect_and_login("ForceJoin")

        # Consume ForceJoin's presence notification on the host.
        await read_packet(host_reader)

        request = bytearray(0x409 - HEADER_BYTES)
        request[0:10] = b"ForceRoom\0"
        sockaddr = (
            struct.pack("<H", socket.AF_INET)
            + struct.pack(">H", 23055)
            + socket.inet_aton("127.0.0.1")
            + b"\0" * 8
        )
        request[0x10D - HEADER_BYTES : 0x11D - HEADER_BYTES] = sockaddr
        request[0x12D - HEADER_BYTES + 8 : 0x12D - HEADER_BYTES + 17] = (
            b"ForceMap\0"
        )
        host_writer.write(build_packet(0x19, request))
        await host_writer.drain()
        hosted = await read_packet(host_reader)
        self.assertEqual(read_u32(hosted, 4), 0x1A)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        listed = await read_packet(join_reader)
        self.assertEqual(read_u32(listed, 4), 0x1E)

        # Abort instead of sending a lobby-reconnect/remove-game request. The
        # server must retire the host's room and notify active join browsers.
        host_writer.transport.abort()
        removed = await asyncio.wait_for(read_packet(join_reader), timeout=2.0)
        self.assertEqual(read_u32(removed, 4), 0x26)
        self.assertEqual(read_u32(removed, 0x0D), 1)

    async def test_public_address_rewrites_local_host_advertisement(self) -> None:
        self.server.config.public_address = "8.8.8.8"
        host_reader, host_writer = await self.connect_and_login("LocalHost")
        join_reader, join_writer = await self.connect_and_login("RemoteJoiner")

        # LocalHost receives RemoteJoiner's lobby presence first.
        await read_packet(host_reader)

        request = bytearray(0x409 - HEADER_BYTES)
        request[0:9] = b"LocalRoom"
        local_sockaddr = (
            struct.pack("<H", socket.AF_INET)
            + struct.pack(">H", 23010)
            + socket.inet_aton("127.0.0.1")
            + b"\0" * 8
        )
        request[0x10D - HEADER_BYTES : 0x11D - HEADER_BYTES] = local_sockaddr
        request[0x12D - HEADER_BYTES + 8 : 0x12D - HEADER_BYTES + 17] = b"LocalMap\0"
        host_writer.write(build_packet(0x19, request))
        await host_writer.drain()
        hosted = await read_packet(host_reader)
        self.assertEqual(read_u32(hosted, 4), 0x1A)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        listed = await read_packet(join_reader)
        self.assertEqual(read_c_string(listed, 0x0D, 0x80), "LocalRoom")
        self.assertEqual(socket.inet_ntoa(listed[0x91:0x95]), "8.8.8.8")
        self.assertEqual(struct.unpack(">H", listed[0x8F:0x91])[0], 23010)

    async def test_game_relay_join_and_frames_are_forwarded_by_member_id(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayHost")
        join_reader, join_writer = await self.connect_and_login("RelayJoin")
        third_reader, third_writer = await self.connect_and_login("RelayThird")

        # RelayHost receives the joiners' real-time lobby presence first.
        await read_packet(host_reader)
        await read_packet(host_reader)
        await read_packet(join_reader)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        empty_list = await read_packet(join_reader)
        self.assertEqual(read_u32(empty_list, 4), 0x1E)
        self.assertEqual(read_i32(empty_list, 0xAD), -1)

        request = bytearray(0x409 - HEADER_BYTES)
        request[0:9] = b"RelayRoom"
        sockaddr = (
            struct.pack("<H", socket.AF_INET)
            + struct.pack(">H", 23010)
            + socket.inet_aton("127.0.0.1")
            + b"\0" * 8
        )
        request[0x10D - HEADER_BYTES : 0x11D - HEADER_BYTES] = sockaddr
        request[0x12D - HEADER_BYTES + 8 : 0x12D - HEADER_BYTES + 17] = b"RelayMap\0"
        host_writer.write(build_packet(0x19, request))
        await host_writer.drain()

        hosted = await read_packet(host_reader)
        self.assertEqual(read_u32(hosted, 4), 0x1A)
        self.assertEqual(read_u32(hosted, 0x0D), 1)
        game_id = read_u32(hosted, 0x11)
        self.assertEqual(game_id, 1)
        self.assertEqual(read_u32(hosted, 0x15), 1)
        host_relay_secret = relay_secret(hosted)
        self.assertEqual(len(host_relay_secret), RELAY_SECRET_BYTES)
        self.assertNotEqual(host_relay_secret, b"\0" * RELAY_SECRET_BYTES)

        advertised_update = await read_packet(join_reader)
        self.assertEqual(read_u32(advertised_update, 4), 0x27)

        join_payload = b"link-join"
        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(join_payload)
            )
        )
        await join_writer.drain()
        joined = await read_packet(join_reader)
        self.assertEqual(read_u32(joined, 4), 0x93)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        self.assertEqual(read_u32(joined, 0x11), game_id)
        join_member_id = read_u32(joined, 0x15)
        self.assertEqual(join_member_id, 2)
        self.assertEqual(relay_secret(joined), host_relay_secret)
        join_session = next(
            client
            for client in self.server.state.clients.values()
            if client.account == "RelayJoin"
        )
        self.assertEqual(join_session.view, "link")

        forwarded_join = await read_packet(host_reader)
        self.assertEqual(read_u32(forwarded_join, 4), 0x94)
        self.assertEqual(read_u32(forwarded_join, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded_join, 0x11), join_member_id)
        self.assertEqual(read_u32(forwarded_join, 0x15), 0)
        self.assertEqual(relay_plain_payload(forwarded_join[0x19:]), join_payload)

        reply_payload = b"host-reply"
        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, join_member_id, 0)
                + relay_wire_payload(reply_payload),
            )
        )
        await host_writer.drain()
        forwarded_reply = await read_packet(join_reader)
        self.assertEqual(read_u32(forwarded_reply, 4), 0x94)
        self.assertEqual(read_u32(forwarded_reply, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded_reply, 0x11), 1)
        self.assertEqual(read_u32(forwarded_reply, 0x15), 0)
        self.assertEqual(relay_plain_payload(forwarded_reply[0x19:]), reply_payload)

        large_payload = b"R" * 0x5000
        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, join_member_id, 1)
                + relay_wire_payload(large_payload),
            )
        )
        await host_writer.drain()
        forwarded_large = await read_packet(join_reader)
        self.assertEqual(read_u32(forwarded_large, 4), 0x94)
        self.assertEqual(read_u32(forwarded_large, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded_large, 0x11), 1)
        self.assertEqual(read_u32(forwarded_large, 0x15), 1)
        self.assertEqual(relay_plain_payload(forwarded_large[0x19:]), large_payload)

        for index in range(16):
            ordered_payload = f"ordered-gameplay-{index:02d}".encode("ascii")
            host_writer.write(
                build_packet(
                    0x92,
                    struct.pack("<III", game_id, join_member_id, 1)
                    + relay_wire_payload(ordered_payload),
                )
            )
        await host_writer.drain()
        for index in range(16):
            ordered = await read_packet(join_reader)
            self.assertEqual(read_u32(ordered, 4), 0x94)
            self.assertEqual(read_u32(ordered, 0x0D), game_id)
            self.assertEqual(read_u32(ordered, 0x11), 1)
            self.assertEqual(read_u32(ordered, 0x15), 1)
            self.assertEqual(
                relay_plain_payload(ordered[0x19:]),
                f"ordered-gameplay-{index:02d}".encode("ascii"),
            )

        third_writer.write(
            build_packet(0x90, struct.pack("<I", game_id) + relay_wire_payload(b"third"))
        )
        await third_writer.drain()
        third_joined = await read_packet(third_reader)
        self.assertEqual(read_u32(third_joined, 4), 0x93)
        third_member_id = read_u32(third_joined, 0x15)
        self.assertEqual(third_member_id, 3)
        await read_packet(host_reader)

        host_only_payload = b"joiner-to-host-only"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 1)
                + relay_wire_payload(host_only_payload),
            )
        )
        await join_writer.drain()
        host_only = await read_packet(host_reader)
        self.assertEqual(read_u32(host_only, 4), 0x94)
        self.assertEqual(read_u32(host_only, 0x0D), game_id)
        self.assertEqual(read_u32(host_only, 0x11), join_member_id)
        self.assertEqual(read_u32(host_only, 0x15), 1)
        self.assertEqual(relay_plain_payload(host_only[0x19:]), host_only_payload)
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(third_reader), timeout=0.05)

        missing_target_payload = b"missing-target"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 99, 1)
                + relay_wire_payload(missing_target_payload),
            )
        )
        await join_writer.drain()
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(host_reader), timeout=0.05)
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(third_reader), timeout=0.05)
        self.assertEqual(
            self.server.state.games[game_id].relay_no_target_frames,
            1,
        )

        invalid_stream_payload = b"invalid-stream"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 99)
                + relay_wire_payload(invalid_stream_payload),
            )
        )
        await join_writer.drain()
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(host_reader), timeout=0.05)
        self.assertEqual(
            self.server.state.games[game_id].relay_invalid_stream_frames,
            1,
        )
        self.assertGreaterEqual(
            self.server.state.games[game_id].relay_mode1_frames,
            18,
        )
        self.assertGreater(self.server.state.games[game_id].relay_mode1_bytes, 0)

        broadcast_payload = b"host-broadcast"
        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 0, 1)
                + relay_wire_payload(broadcast_payload),
            )
        )
        await host_writer.drain()
        join_broadcast = await read_packet(join_reader)
        third_broadcast = await read_packet(third_reader)
        self.assertEqual(read_u32(join_broadcast, 4), 0x94)
        self.assertEqual(read_u32(third_broadcast, 4), 0x94)
        self.assertEqual(read_u32(join_broadcast, 0x15), 1)
        self.assertEqual(read_u32(third_broadcast, 0x15), 1)
        self.assertEqual(relay_plain_payload(join_broadcast[0x19:]), broadcast_payload)
        self.assertEqual(relay_plain_payload(third_broadcast[0x19:]), broadcast_payload)
        game = self.server.state.games[game_id]
        self.assertGreaterEqual(game.relay_member_link_tx[1], 1)
        self.assertGreaterEqual(game.relay_member_link_rx[1], 2)
        self.assertGreaterEqual(game.relay_member_link_tx[join_member_id], 1)
        self.assertGreaterEqual(game.relay_member_link_rx[join_member_id], 1)
        self.assertGreaterEqual(game.relay_member_mode1_tx[1], 18)
        self.assertGreaterEqual(game.relay_member_mode1_rx[1], 1)
        self.assertGreaterEqual(game.relay_member_mode1_tx[join_member_id], 1)
        self.assertGreaterEqual(game.relay_member_mode1_rx[join_member_id], 18)
        self.assertGreaterEqual(game.relay_member_mode1_rx[third_member_id], 1)

        join_writer.write(build_packet(0x0E, b"\0" * 8))
        await join_writer.drain()
        host_member_left = await read_packet(host_reader)
        third_member_left = await read_packet(third_reader)
        self.assertEqual(read_u32(host_member_left, 4), 0x95)
        self.assertEqual(read_u32(third_member_left, 4), 0x95)
        self.assertEqual(read_u32(host_member_left, 0x0D), game_id)
        self.assertEqual(read_u32(third_member_left, 0x0D), game_id)
        self.assertEqual(read_u32(host_member_left, 0x11), join_member_id)
        self.assertEqual(read_u32(third_member_left, 0x11), join_member_id)

        replacement_reader, replacement_writer = await self.connect_and_login(
            "RelayReplacement"
        )
        replacement_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"replacement")
            )
        )
        await replacement_writer.drain()
        replacement_joined = await read_until_opcode(replacement_reader, 0x93)
        self.assertEqual(read_u32(replacement_joined, 0x0D), 0)
        replacement_member_id = read_u32(replacement_joined, 0x15)
        self.assertEqual(replacement_member_id, join_member_id)
        replacement_forwarded = await read_until_opcode(host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(replacement_forwarded, 0x0D), game_id)
        self.assertEqual(read_u32(replacement_forwarded, 0x11), replacement_member_id)
        self.assertEqual(
            relay_plain_payload(replacement_forwarded[0x19:]), b"replacement"
        )

        replacement_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await replacement_writer.drain()
        replacement_left = await read_until_opcode(host_reader, 0x95, limit=40)
        self.assertEqual(read_u32(replacement_left, 0x0D), game_id)
        self.assertEqual(read_u32(replacement_left, 0x11), replacement_member_id)

        third_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await third_writer.drain()
        host_third_left = await read_packet(host_reader)
        self.assertEqual(read_u32(host_third_left, 4), 0x95)
        self.assertEqual(read_u32(host_third_left, 0x0D), game_id)
        self.assertEqual(read_u32(host_third_left, 0x11), third_member_id)

    async def test_removing_advertisement_preserves_active_relay_members(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayHideHost")
        join_reader, join_writer = await self.connect_and_login("RelayHideJoin")
        browser_reader, browser_writer = await self.connect_and_login("RelayHideBrowse")

        # Consume lobby presence notifications that are unrelated to relay framing.
        await read_packet(host_reader)
        await read_packet(host_reader)
        await read_packet(join_reader)

        browser_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await browser_writer.drain()
        initial_empty = await read_until_opcode(browser_reader, 0x1E, limit=40)
        self.assertEqual(read_i32(initial_empty, 0xAD), -1)

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayHiddenRoom"
        )
        listed = await read_until_opcode(browser_reader, 0x27, limit=40)
        self.assertEqual(read_u32(listed, 0xB1), game_id)

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        join_member_id = read_u32(joined, 0x15)
        self.assertEqual(join_member_id, 2)
        await read_until_opcode(host_reader, 0x94, limit=40)

        host_writer.write(build_packet(0x1B, struct.pack("<I", game_id)))
        await host_writer.drain()
        hidden = await read_until_opcode(browser_reader, 0x26, limit=40)
        self.assertEqual(read_u32(hidden, 0x0D), game_id)

        game = self.server.state.games.get(game_id)
        self.assertIsNotNone(game)
        self.assertFalse(game.advertised)
        self.assertEqual(game.relay_members, {1: 1, 2: 2})

        browser_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await browser_writer.drain()
        empty = await read_until_opcode(browser_reader, 0x1E, limit=40)
        self.assertEqual(read_i32(empty, 0xAD), -1)

        late_reader, late_writer = await self.connect_and_login("RelayHideLate")
        late_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"late")
            )
        )
        await late_writer.drain()
        rejected = await read_until_opcode(late_reader, 0x93, limit=40)
        self.assertEqual(read_u32(rejected, 0x0D), 1)
        self.assertEqual(read_u32(rejected, 0x11), game_id)

        probe_payload = b"still-relayed-after-hide"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 1)
                + relay_wire_payload(probe_payload),
            )
        )
        await join_writer.drain()
        forwarded_probe = await read_until_opcode(host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(forwarded_probe, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded_probe, 0x11), join_member_id)
        self.assertEqual(relay_plain_payload(forwarded_probe[0x19:]), probe_payload)

    async def test_start_game_hides_advertisement_and_preserves_relay_members(
        self,
    ) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayStartHost")
        join_reader, join_writer = await self.connect_and_login("RelayStartJoin")
        browser_reader, browser_writer = await self.connect_and_login("RelayStartBrowse")

        await read_packet(host_reader)
        await read_packet(host_reader)
        await read_packet(join_reader)

        browser_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await browser_writer.drain()
        initial_empty = await read_until_opcode(browser_reader, 0x1E, limit=40)
        self.assertEqual(read_i32(initial_empty, 0xAD), -1)

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayStartRoom"
        )
        listed = await read_until_opcode(browser_reader, 0x27, limit=40)
        self.assertEqual(read_u32(listed, 0xB1), game_id)

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        join_member_id = read_u32(joined, 0x15)
        self.assertEqual(join_member_id, 2)
        await read_until_opcode(host_reader, 0x94, limit=40)

        host_writer.write(build_packet(0x28, b"\0" * 0x20))
        await host_writer.drain()
        hidden = await read_until_opcode(browser_reader, 0x26, limit=40)
        self.assertEqual(read_u32(hidden, 0x0D), game_id)

        game = self.server.state.games.get(game_id)
        self.assertIsNotNone(game)
        self.assertFalse(game.advertised)
        self.assertEqual(game.relay_members, {1: 1, 2: 2})

        browser_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await browser_writer.drain()
        empty = await read_until_opcode(browser_reader, 0x1E, limit=40)
        self.assertEqual(read_i32(empty, 0xAD), -1)

        probe_payload = b"still-relayed-after-start"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 1)
                + relay_wire_payload(probe_payload),
            )
        )
        await join_writer.drain()
        forwarded_probe = await read_until_opcode(host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(forwarded_probe, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded_probe, 0x11), join_member_id)
        self.assertEqual(relay_plain_payload(forwarded_probe[0x19:]), probe_payload)

    async def test_online_chat_does_not_pollute_active_relay_stream(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayChatHost")
        join_reader, join_writer = await self.connect_and_login("RelayChatJoin")
        _, observer_writer = await self.connect_and_login("RelayChatOnline")

        await read_packet(host_reader)
        await read_packet(host_reader)
        await read_packet(join_reader)

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayChatRoom"
        )
        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        join_member_id = read_u32(joined, 0x15)
        await read_until_opcode(host_reader, 0x94, limit=40)

        raw_chat = build_colored_text_packet("Online> ", "hello lobby")
        observer_writer.write(build_packet(0x2A, raw_chat[4:]))
        await observer_writer.drain()
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(join_reader.readexactly(1), timeout=0.05)

        relay_payload = b"relay-after-online-chat"
        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, join_member_id, 1)
                + relay_wire_payload(relay_payload),
            )
        )
        await host_writer.drain()
        forwarded = await read_until_opcode(join_reader, 0x94, limit=40)
        self.assertEqual(read_u32(forwarded, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded, 0x11), 1)
        self.assertEqual(relay_plain_payload(forwarded[0x19:]), relay_payload)

    async def test_expired_active_relay_room_is_hidden_not_disconnected(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayExpireHost")
        join_reader, join_writer = await self.connect_and_login("RelayExpireJoin")

        await read_packet(host_reader)
        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayExpireRoom"
        )

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        join_member_id = read_u32(joined, 0x15)
        await read_until_opcode(host_reader, 0x94, limit=40)

        game = self.server.state.games[game_id]
        game.created_at = time.monotonic() - self.server.config.room_ttl_seconds - 1
        await self.server._cleanup_expired_games_once()
        self.assertIn(game_id, self.server.state.games)
        self.assertFalse(game.advertised)
        self.assertEqual(game.relay_members, {1: 1, 2: 2})

        probe_payload = b"still-relayed-after-expire"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 1)
                + relay_wire_payload(probe_payload),
            )
        )
        await join_writer.drain()
        forwarded_probe = await read_until_opcode(host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(forwarded_probe, 0x0D), game_id)
        self.assertEqual(read_u32(forwarded_probe, 0x11), join_member_id)
        self.assertEqual(relay_plain_payload(forwarded_probe[0x19:]), probe_payload)

    async def test_hidden_relay_host_disconnect_notifies_members_and_removes_room(
        self,
    ) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayHiddenDropHost")
        join_reader, join_writer = await self.connect_and_login("RelayHiddenDropJoin")

        await read_packet(host_reader)
        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayHiddenDropRoom"
        )
        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        await read_until_opcode(host_reader, 0x94, limit=40)

        host_writer.write(build_packet(0x1B, struct.pack("<I", game_id)))
        await host_writer.drain()
        await wait_until(lambda: not self.server.state.games[game_id].advertised)
        self.assertFalse(self.server.state.games[game_id].advertised)

        host_writer.transport.abort()
        host_left = await read_until_opcode(join_reader, 0x95, limit=40)
        self.assertEqual(read_u32(host_left, 0x0D), game_id)
        self.assertEqual(read_u32(host_left, 0x11), 1)
        self.assertNotIn(game_id, self.server.state.games)
        join_session = next(
            client
            for client in self.server.state.clients.values()
            if client.account == "RelayHiddenDropJoin"
        )
        self.assertIsNone(join_session.relay_game_id)
        self.assertEqual(join_session.relay_member_id, 0)

    async def test_hidden_relay_joiner_leave_keeps_host_until_lobby_return(
        self,
    ) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayHiddenSoloHost")
        join_reader, join_writer = await self.connect_and_login("RelayHiddenSoloJoin")

        await read_packet(host_reader)
        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayHiddenSoloRoom"
        )
        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        join_member_id = read_u32(joined, 0x15)
        await read_until_opcode(host_reader, 0x94, limit=40)

        host_writer.write(build_packet(0x1B, struct.pack("<I", game_id)))
        await host_writer.drain()
        await wait_until(lambda: not self.server.state.games[game_id].advertised)
        self.assertFalse(self.server.state.games[game_id].advertised)

        join_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await join_writer.drain()
        left = await read_until_opcode(host_reader, 0x95, limit=40)
        self.assertEqual(read_u32(left, 0x0D), game_id)
        self.assertEqual(read_u32(left, 0x11), join_member_id)
        game = self.server.state.games[game_id]
        self.assertFalse(game.advertised)
        self.assertEqual(game.relay_members, {1: 1})

        host_writer.write(build_packet(0x0E, b"\0" * 8))
        await host_writer.drain()
        await wait_until(lambda: game_id not in self.server.state.games)
        self.assertNotIn(game_id, self.server.state.games)

    async def test_hidden_relay_room_name_reuse_is_isolated_by_game_id(self) -> None:
        first_host_reader, first_host_writer = await self.connect_and_login(
            "RelayReuseHostA"
        )
        first_join_reader, first_join_writer = await self.connect_and_login(
            "RelayReuseJoinA"
        )
        second_host_reader, second_host_writer = await self.connect_and_login(
            "RelayReuseHostB"
        )
        second_join_reader, second_join_writer = await self.connect_and_login(
            "RelayReuseJoinB"
        )
        browser_reader, browser_writer = await self.connect_and_login(
            "RelayReuseBrowser"
        )

        # Consume presence notifications that are unrelated to game relay packets.
        for _ in range(4):
            await read_packet(first_host_reader)
        for _ in range(3):
            await read_packet(first_join_reader)
        for _ in range(2):
            await read_packet(second_host_reader)
        await read_packet(second_join_reader)

        room_name = "RelaySameName"
        first_game_id = await self.advertise_relay_game(
            first_host_reader, first_host_writer, room_name
        )
        first_join_writer.write(
            build_packet(
                0x90,
                struct.pack("<I", first_game_id)
                + relay_wire_payload(b"first-join"),
            )
        )
        await first_join_writer.drain()
        first_joined = await read_until_opcode(first_join_reader, 0x93, limit=40)
        first_join_member = read_u32(first_joined, 0x15)
        self.assertEqual(first_join_member, 2)
        await read_until_opcode(first_host_reader, 0x94, limit=40)

        first_host_writer.write(build_packet(0x1B, struct.pack("<I", first_game_id)))
        await first_host_writer.drain()
        await wait_until(lambda: not self.server.state.games[first_game_id].advertised)

        browser_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await browser_writer.drain()
        empty = await read_until_opcode(browser_reader, 0x1E, limit=40)
        self.assertEqual(read_i32(empty, 0xAD), -1)

        second_game_id = await self.advertise_relay_game(
            second_host_reader, second_host_writer, room_name
        )
        self.assertNotEqual(first_game_id, second_game_id)
        listed = await read_until_opcode(browser_reader, 0x27, limit=40)
        self.assertEqual(read_c_string(listed, 0x0D, 0x80), room_name)
        self.assertEqual(read_u32(listed, 0xB1), second_game_id)

        second_join_writer.write(
            build_packet(
                0x90,
                struct.pack("<I", second_game_id)
                + relay_wire_payload(b"second-join"),
            )
        )
        await second_join_writer.drain()
        second_joined = await read_until_opcode(second_join_reader, 0x93, limit=40)
        second_join_member = read_u32(second_joined, 0x15)
        self.assertEqual(second_join_member, 2)
        forwarded_second_join = await read_until_opcode(
            second_host_reader, 0x94, limit=40
        )
        self.assertEqual(read_u32(forwarded_second_join, 0x0D), second_game_id)
        self.assertEqual(
            relay_plain_payload(forwarded_second_join[0x19:]), b"second-join"
        )

        old_payload = b"old-hidden-room"
        first_join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", first_game_id, 1, 1)
                + relay_wire_payload(old_payload),
            )
        )
        await first_join_writer.drain()
        old_forwarded = await read_until_opcode(first_host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(old_forwarded, 0x0D), first_game_id)
        self.assertEqual(read_u32(old_forwarded, 0x11), first_join_member)
        self.assertEqual(relay_plain_payload(old_forwarded[0x19:]), old_payload)
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(second_host_reader), timeout=0.05)

        new_payload = b"new-visible-room"
        second_join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", second_game_id, 1, 1)
                + relay_wire_payload(new_payload),
            )
        )
        await second_join_writer.drain()
        new_forwarded = await read_until_opcode(second_host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(new_forwarded, 0x0D), second_game_id)
        self.assertEqual(read_u32(new_forwarded, 0x11), second_join_member)
        self.assertEqual(relay_plain_payload(new_forwarded[0x19:]), new_payload)

    async def test_stale_relay_leave_does_not_remove_new_membership(self) -> None:
        first_host_reader, first_host_writer = await self.connect_and_login(
            "RelayStaleHostA"
        )
        second_host_reader, second_host_writer = await self.connect_and_login(
            "RelayStaleHostB"
        )
        join_reader, join_writer = await self.connect_and_login("RelayStaleJoin")

        first_game_id = await self.advertise_relay_game(
            first_host_reader, first_host_writer, "RelayStaleRoomA"
        )
        second_game_id = await self.advertise_relay_game(
            second_host_reader, second_host_writer, "RelayStaleRoomB"
        )

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", first_game_id) + relay_wire_payload(b"a")
            )
        )
        await join_writer.drain()
        first_joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(first_joined, 0x0D), 0)
        first_member_id = read_u32(first_joined, 0x15)
        self.assertEqual(first_member_id, 2)
        await read_until_opcode(first_host_reader, 0x94, limit=40)

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", second_game_id) + relay_wire_payload(b"b")
            )
        )
        await join_writer.drain()
        second_joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(second_joined, 0x0D), 0)
        second_member_id = read_u32(second_joined, 0x15)
        self.assertEqual(second_member_id, 2)
        first_left = await read_until_opcode(first_host_reader, 0x95, limit=40)
        self.assertEqual(read_u32(first_left, 0x0D), first_game_id)
        self.assertEqual(read_u32(first_left, 0x11), first_member_id)
        await read_until_opcode(second_host_reader, 0x94, limit=40)

        join_writer.write(build_packet(0x91, struct.pack("<I", first_game_id)))
        await join_writer.drain()
        probe_payload = b"still-in-second-room"
        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", second_game_id, 1, 1)
                + relay_wire_payload(probe_payload),
            )
        )
        await join_writer.drain()
        forwarded_probe = await read_until_opcode(second_host_reader, 0x94, limit=40)
        self.assertEqual(read_u32(forwarded_probe, 0x0D), second_game_id)
        self.assertEqual(read_u32(forwarded_probe, 0x11), second_member_id)
        self.assertEqual(read_u32(forwarded_probe, 0x15), 1)
        self.assertEqual(relay_plain_payload(forwarded_probe[0x19:]), probe_payload)
        join_session = next(
            client
            for client in self.server.state.clients.values()
            if client.account == "RelayStaleJoin"
        )
        self.assertEqual(join_session.relay_game_id, second_game_id)
        self.assertEqual(join_session.relay_member_id, second_member_id)

    async def test_relay_rejects_missing_game_and_non_member_frame(self) -> None:
        reader, writer = await self.connect_and_login("RelayStray")
        missing_game_id = 404

        writer.write(build_packet(0x90, struct.pack("<I", missing_game_id) + b"join"))
        await writer.drain()
        missing = await read_packet(reader)
        self.assertEqual(read_u32(missing, 4), 0x93)
        self.assertEqual(read_u32(missing, 0x0D), 1)
        self.assertEqual(read_u32(missing, 0x11), missing_game_id)
        self.assertEqual(read_u32(missing, 0x15), 0)

        writer.write(
            build_packet(0x92, struct.pack("<III", missing_game_id, 0, 0) + b"data")
        )
        await writer.drain()
        rejected = await read_packet(reader)
        self.assertEqual(read_u32(rejected, 4), 0x93)
        self.assertEqual(read_u32(rejected, 0x0D), 3)
        self.assertEqual(read_u32(rejected, 0x11), missing_game_id)
        self.assertEqual(read_u32(rejected, 0x15), 0)

    async def test_relay_rejects_unwrapped_join_and_gameplay_payloads(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayCipherHost")
        join_reader, join_writer = await self.connect_and_login("RelayCipherJoin")

        await read_packet(host_reader)
        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayCipherRoom"
        )

        join_writer.write(
            build_packet(0x90, struct.pack("<I", game_id) + b"plaintext-join")
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        self.assertEqual(read_u32(joined, 0x15), 2)
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(host_reader), timeout=0.05)

        join_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 1, 1) + b"plaintext-gameplay",
            )
        )
        await join_writer.drain()
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(read_packet(host_reader), timeout=0.05)

        game = self.server.state.games[game_id]
        self.assertEqual(game.relay_invalid_payload_frames, 2)
        self.assertEqual(game.relay_link_frames, 0)
        self.assertEqual(game.relay_mode1_frames, 0)

    async def test_malformed_relay_packets_do_not_poison_client_session(self) -> None:
        reader, writer = await self.connect_and_login("RelayMalformed")

        writer.write(build_packet(0x90, b""))
        await writer.drain()
        rejected_join = await read_packet(reader)
        self.assertEqual(read_u32(rejected_join, 4), 0x93)
        self.assertEqual(read_u32(rejected_join, 0x0D), 1)
        self.assertEqual(read_u32(rejected_join, 0x11), 0)
        self.assertEqual(read_u32(rejected_join, 0x15), 0)

        writer.write(build_packet(0x92, b"short"))
        writer.write(build_packet(0x12, struct.pack("<I", 0)))
        await writer.drain()
        player_page = await read_packet(reader)
        self.assertEqual(read_u32(player_page, 4), 0x13)
        self.assertEqual(read_c_string(player_page, 0x15, 0x20), "RelayMalformed")

    async def test_relay_host_disconnect_notifies_members_and_clears_state(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayDropHost")
        join_reader, join_writer = await self.connect_and_login("RelayDropJoin")

        # The host receives the joiner's online-lobby presence notification.
        await read_packet(host_reader)
        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayDropRoom"
        )

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        self.assertEqual(read_u32(joined, 0x15), 2)
        forwarded_join = await read_until_opcode(host_reader, 0x94)
        self.assertEqual(read_u32(forwarded_join, 0x11), 2)

        host_writer.transport.abort()
        host_left = await read_until_opcode(join_reader, 0x95)
        self.assertEqual(read_u32(host_left, 0x0D), game_id)
        self.assertEqual(read_u32(host_left, 0x11), 1)
        self.assertNotIn(game_id, self.server.state.games)
        join_session = next(
            client
            for client in self.server.state.clients.values()
            if client.account == "RelayDropJoin"
        )
        self.assertIsNone(join_session.relay_game_id)
        self.assertEqual(join_session.relay_member_id, 0)

    async def test_relay_frame_to_departed_member_does_not_count_no_target(
        self,
    ) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayStaleHost")
        join_reader, join_writer = await self.connect_and_login("RelayStaleJoin")

        # The host receives the joiner's online-lobby presence notification.
        await read_packet(host_reader)
        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayStaleRoom"
        )

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93)
        self.assertEqual(read_u32(joined, 0x15), 2)
        forwarded_join = await read_until_opcode(host_reader, 0x94)
        self.assertEqual(read_u32(forwarded_join, 0x11), 2)

        join_writer.write(build_packet(0x91, struct.pack("<I", game_id)))
        await join_writer.drain()
        member_left = await read_until_opcode(host_reader, 0x95)
        self.assertEqual(read_u32(member_left, 0x11), 2)

        host_writer.write(
            build_packet(
                0x92,
                struct.pack("<III", game_id, 2, 1)
                + relay_wire_payload(b"late"),
            )
        )
        await host_writer.drain()

        self.assertEqual(
            self.server.state.games[game_id].relay_no_target_frames,
            0,
        )
        member_count, distinct_peer_hosts, member_peers = (
            self.server._relay_member_peer_summary(
                self.server.state.games[game_id],
                list(self.server.state.games[game_id].relay_members.items()),
            )
        )
        self.assertEqual(member_count, 2)
        self.assertEqual(distinct_peer_hosts, 1)
        self.assertIn("1@127.0.0.1", member_peers)
        self.assertIn("2@127.0.0.1", member_peers)

    async def test_relay_joiner_can_resume_game_browser_after_host_disconnect(
        self,
    ) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayResumeHost")
        join_reader, join_writer = await self.connect_and_login("RelayResumeJoin")

        await read_packet(host_reader)
        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        empty_list = await read_packet(join_reader)
        self.assertEqual(read_u32(empty_list, 4), 0x1E)

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayResumeRoom"
        )
        listed = await read_until_opcode(join_reader, 0x27)
        self.assertEqual(read_c_string(listed, 0x0D, 0x80), "RelayResumeRoom")

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        join_session = next(
            client
            for client in self.server.state.clients.values()
            if client.account == "RelayResumeJoin"
        )
        self.assertEqual(join_session.view, "link")
        await read_until_opcode(host_reader, 0x94)

        host_writer.transport.abort()
        host_left = await read_until_opcode(join_reader, 0x95)
        self.assertEqual(read_u32(host_left, 0x0D), game_id)
        self.assertEqual(read_u32(host_left, 0x11), 1)

        join_writer.write(build_packet(0x1D, struct.pack("<I", 0)))
        await join_writer.drain()
        refreshed = await read_until_opcode(join_reader, 0x1E)
        self.assertEqual(read_i32(refreshed, 0xAD), -1)
        self.assertEqual(join_session.view, "free_server")

        new_host_reader, new_host_writer = await self.connect_and_login(
            "RelayResumeNewHost"
        )
        await self.advertise_relay_game(
            new_host_reader, new_host_writer, "RelayResumeNewRoom"
        )
        relisted = await read_until_opcode(join_reader, 0x27)
        self.assertEqual(read_c_string(relisted, 0x0D, 0x80), "RelayResumeNewRoom")

    async def test_remove_game_writes_auto_relay_evidence_when_configured(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            evidence_dir = Path(directory) / "relay_evidence"
            self.server.config.relay_evidence_dir = evidence_dir
            host_reader, host_writer = await self.connect_and_login(
                "RelayEvidenceHost"
            )
            join_reader, join_writer = await self.connect_and_login(
                "RelayEvidenceJoin"
            )

            game_id = await self.advertise_relay_game(
                host_reader, host_writer, "RelayEvidenceRoom"
            )
            join_writer.write(
                build_packet(
                    0x90,
                    struct.pack("<I", game_id) + relay_wire_payload(b"join"),
                )
            )
            await join_writer.drain()
            joined = await read_until_opcode(join_reader, 0x93)
            join_member_id = read_u32(joined, 0x15)
            self.assertEqual(join_member_id, 2)
            await read_until_opcode(host_reader, 0x94)

            host_writer.write(
                build_packet(
                    0x92,
                    struct.pack("<III", game_id, join_member_id, 0)
                    + relay_wire_payload(b"link-frame"),
                )
            )
            await host_writer.drain()
            await read_until_opcode(join_reader, 0x94)

            host_writer.write(
                build_packet(
                    0x92,
                    struct.pack("<III", game_id, join_member_id, 1)
                    + relay_wire_payload(b"mode1-host"),
                )
            )
            join_writer.write(
                build_packet(
                    0x92,
                    struct.pack("<III", game_id, 1, 1)
                    + relay_wire_payload(b"mode1-join")
                )
            )
            await host_writer.drain()
            await join_writer.drain()
            await read_until_opcode(join_reader, 0x94)
            await read_until_opcode(host_reader, 0x94)

            await self.server._remove_game(game_id)

            evidence_files = list(evidence_dir.glob("relay_*_game*_*.json"))
            self.assertEqual(len(evidence_files), 1)
            evidence = json.loads(evidence_files[0].read_text(encoding="utf-8"))
            self.assertTrue(evidence["ok"])
            self.assertEqual(evidence["source"], "server_auto_export")
            self.assertEqual(evidence["room"], "RelayEvidenceRoom")
            self.assertEqual(evidence["game_id"], game_id)
            server_summary = evidence["server_summary"]
            self.assertTrue(server_summary["ok"])
            self.assertEqual(server_summary["matched_summary_count"], 1)
            summary = server_summary["summary"]
            self.assertEqual(summary["room"], "RelayEvidenceRoom")
            self.assertEqual(summary["game_id"], game_id)
            self.assertEqual(summary["members"], 2)
            self.assertEqual(summary["distinct_peer_endpoints"], 2)
            self.assertEqual(summary["bidirectional_mode1_members"], 2)
            self.assertIn("relay summary game=", summary["line"])
            self.assertEqual(server_summary["summary_lines"], [summary["line"]])

    async def test_relay_room_capacity_includes_the_host_member(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RelayCapHost")
        joiners: list[tuple[asyncio.StreamReader, asyncio.StreamWriter]] = []
        for index in range(8):
            reader, writer = await self.connect_and_login(f"RelayCap{index}")
            joiners.append((reader, writer))
            await read_until_opcode(host_reader, 7)

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RelayCapRoom"
        )
        for index, (reader, writer) in enumerate(joiners):
            writer.write(
                build_packet(
                    0x90,
                    struct.pack("<I", game_id)
                    + relay_wire_payload(f"join{index}".encode("ascii")),
                )
            )
            await writer.drain()
            joined = await read_until_opcode(reader, 0x93, limit=40)
            if index < 7:
                self.assertEqual(read_u32(joined, 0x0D), 0)
                self.assertEqual(read_u32(joined, 0x15), index + 2)
            else:
                self.assertEqual(read_u32(joined, 0x0D), 2)
                self.assertEqual(read_u32(joined, 0x15), 0)

    async def test_fragmented_login_is_accepted(self) -> None:
        reader, writer = await asyncio.open_connection(
            "127.0.0.1", self.server.bound_port
        )
        self.connections.append((reader, writer))
        packet = login_packet("Fragment", "pw")
        for cut in (packet[:3], packet[3:12], packet[12:200], packet[200:]):
            writer.write(cut)
            await writer.drain()
        response = await read_packet(reader)
        self.assertEqual(read_u32(response, 4), 2)
        self.assertEqual(read_u32(response, 0x0D), 0)

    async def test_bad_checksum_packet_is_ignored(self) -> None:
        reader, writer = await asyncio.open_connection(
            "127.0.0.1", self.server.bound_port
        )
        self.connections.append((reader, writer))
        invalid = bytearray(login_packet("Invalid", "pw"))
        invalid[-1] ^= 0x01
        writer.write(invalid)
        writer.write(login_packet("Valid", "pw"))
        await writer.drain()

        response = await read_packet(reader)
        self.assertEqual(read_u32(response, 4), 2)
        self.assertEqual(read_u32(response, 0x0D), 0)
        writer.write(build_packet(0x12, struct.pack("<I", 0)))
        await writer.drain()
        page = await read_packet(reader)
        self.assertEqual(read_u32(page, 4), 0x13)
        self.assertEqual(read_c_string(page, 0x15, 0x20), "Valid")

    async def test_account_creation_enters_lobby_and_chat_is_broadcast(self) -> None:
        creator_reader, creator_writer = await asyncio.open_connection(
            "127.0.0.1", self.server.bound_port
        )
        self.connections.append((creator_reader, creator_writer))
        creator_writer.write(build_packet(0x39))
        await creator_writer.drain()
        status_text = await read_packet(creator_reader)
        self.assertEqual(read_u32(status_text, 4), 0x3A)

        creator_writer.write(create_account_packet("Creator", "pass1234"))
        await creator_writer.drain()
        created = await read_packet(creator_reader)
        self.assertEqual(read_u32(created, 4), 4)
        self.assertEqual(read_u32(created, 0x0D), 0)

        observer_reader, _ = await self.connect_and_login("Observer", "pass1234")
        presence = await read_packet(creator_reader)
        self.assertEqual(read_u32(presence, 4), 7)
        self.assertEqual(read_c_string(presence, 0x0D, 0x20), "Observer")

        raw_chat = build_colored_text_packet("Creator> ", "hello lobby")
        creator_writer.write(build_packet(0x2A, raw_chat[4:]))
        await creator_writer.drain()
        received = await observer_reader.readexactly(len(raw_chat))
        self.assertEqual(read_u32(received, 0), 0)
        self.assertIn(b"hello lobby\0", received)

    async def test_rank_results_and_replay_round_trip_require_match_token(self) -> None:
        host_reader, host_writer = await self.connect_and_login("RankHost")
        join_reader, join_writer = await self.connect_and_login("RankJoin")
        await read_until_presence(host_reader, "RankJoin")

        game_id = await self.advertise_relay_game(
            host_reader, host_writer, "RankRoom", game_type=2
        )
        game = self.server.state.games[game_id]
        token = game.relay_secret[:16]

        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", game_id) + relay_wire_payload(b"join")
            )
        )
        await join_writer.drain()
        joined = await read_until_opcode(join_reader, 0x93, limit=40)
        self.assertEqual(read_u32(joined, 0x0D), 0)
        await read_until_opcode(host_reader, 0x94, limit=40)

        host_writer.write(build_packet(0x28, b"\0" * 0x20))
        await host_writer.drain()
        await wait_until(lambda: self.server.state.games[game_id].started)

        host_writer.write(build_packet(0x98, struct.pack("<III", 2, 0, game_id) + token))
        join_writer.write(build_packet(0x98, struct.pack("<III", 2, 1, game_id) + token))
        await host_writer.drain()
        await join_writer.drain()
        host_result = await read_until_opcode(host_reader, 0x99, limit=40)
        join_result = await read_until_opcode(join_reader, 0x99, limit=40)
        self.assertEqual(read_u32(host_result, 0x0D), 0)
        self.assertEqual(read_u32(join_result, 0x0D), 0)
        self.assertEqual(self.server.accounts.statistics("RankHost", "rank")["wins"], 1)
        self.assertEqual(self.server.accounts.statistics("RankJoin", "rank")["losses"], 1)
        self.assertEqual(
            self.server.accounts.statistics("RankHost", "normal"),
            {"wins": 0, "losses": 0, "draws": 0},
        )

        host_writer.write(build_packet(0x37, b"RankHost\0" + b"\0" * 23))
        await host_writer.drain()
        profile = await read_until_opcode(host_reader, 0x38, limit=40)
        self.assertEqual(read_u32(profile, 0x16F), 3)
        self.assertEqual(read_u32(profile, 0x173), 1)
        self.assertEqual(read_u32(profile, 0x177), 0)
        self.assertEqual(read_u32(profile, 0x17B), 0)

        host_writer.write(build_packet(0x2F, struct.pack("<I", 0)))
        await host_writer.drain()
        rank_list = await read_until_opcode(host_reader, 0x30, limit=40)
        self.assertEqual(read_c_string(rank_list, 0x11, 0x20), "RankHost")
        self.assertEqual(read_u32(rank_list, 0x31), 3)

        host_writer.write(build_packet(0x98, struct.pack("<III", 2, 0, game_id) + token))
        await host_writer.drain()
        duplicate = await read_until_opcode(host_reader, 0x99, limit=40)
        self.assertEqual(read_u32(duplicate, 0x0D), 1)
        self.assertEqual(self.server.accounts.statistics("RankHost", "rank")["wins"], 1)

        join_writer.write(
            build_packet(0x98, struct.pack("<III", 2, 0, game_id) + b"X" * 16)
        )
        await join_writer.drain()
        forged = await read_until_opcode(join_reader, 0x99, limit=40)
        self.assertEqual(read_u32(forged, 0x0D), 2)

        melee_id = await self.advertise_relay_game(
            host_reader, host_writer, "MeleeRoom", game_type=1
        )
        melee_token = self.server.state.games[melee_id].relay_secret[:16]
        join_writer.write(
            build_packet(
                0x90, struct.pack("<I", melee_id) + relay_wire_payload(b"melee")
            )
        )
        await join_writer.drain()
        self.assertEqual(
            read_u32(await read_until_opcode(join_reader, 0x93, limit=60), 0x0D), 0
        )
        await read_until_opcode(host_reader, 0x94, limit=60)
        host_writer.write(build_packet(0x28, b"\0" * 0x20))
        await host_writer.drain()
        await wait_until(lambda: self.server.state.games[melee_id].started)
        host_writer.write(
            build_packet(0x98, struct.pack("<III", 1, 0, melee_id) + melee_token)
        )
        join_writer.write(
            build_packet(0x98, struct.pack("<III", 1, 1, melee_id) + melee_token)
        )
        await host_writer.drain()
        await join_writer.drain()
        self.assertEqual(
            read_u32(await read_until_opcode(host_reader, 0x99, limit=60), 0x0D), 0
        )
        self.assertEqual(
            read_u32(await read_until_opcode(join_reader, 0x99, limit=60), 0x0D), 0
        )

        top_bottom_id = await self.advertise_relay_game(
            host_reader, host_writer, "TopBottomRoom", game_type=0
        )
        top_bottom_token = self.server.state.games[top_bottom_id].relay_secret[:16]
        join_writer.write(
            build_packet(
                0x90,
                struct.pack("<I", top_bottom_id) + relay_wire_payload(b"top-bottom"),
            )
        )
        await join_writer.drain()
        self.assertEqual(
            read_u32(await read_until_opcode(join_reader, 0x93, limit=60), 0x0D), 0
        )
        await read_until_opcode(host_reader, 0x94, limit=60)
        host_writer.write(build_packet(0x28, b"\0" * 0x20))
        await host_writer.drain()
        await wait_until(lambda: self.server.state.games[top_bottom_id].started)
        for writer in (host_writer, join_writer):
            writer.write(
                build_packet(
                    0x98,
                    struct.pack("<III", 0, 2, top_bottom_id) + top_bottom_token,
                )
            )
        await host_writer.drain()
        await join_writer.drain()
        self.assertEqual(
            read_u32(await read_until_opcode(host_reader, 0x99, limit=60), 0x0D), 0
        )
        self.assertEqual(
            read_u32(await read_until_opcode(join_reader, 0x99, limit=60), 0x0D), 0
        )
        self.assertEqual(
            self.server.accounts.statistics("RankHost", "normal"),
            {"wins": 1, "losses": 0, "draws": 1},
        )
        self.assertEqual(
            self.server.accounts.statistics("RankJoin", "normal"),
            {"wins": 0, "losses": 1, "draws": 1},
        )
        host_writer.write(build_packet(0x37, b"RankHost\0" + b"\0" * 23))
        await host_writer.drain()
        normal_profile = await read_until_opcode(host_reader, 0x38, limit=60)
        self.assertEqual(read_u32(normal_profile, 0x15F), 1)
        self.assertEqual(read_u32(normal_profile, 0x163), 0)
        self.assertEqual(read_u32(normal_profile, 0x167), 1)

        rejected_begin = bytearray(0xB1 - HEADER_BYTES)
        struct.pack_into(
            "<IIIII", rejected_begin, 0, 91, 4, 0, 2, top_bottom_id
        )
        rejected_begin[20:36] = top_bottom_token
        rejected_begin[36:49] = b"NoUpload.ply\0"
        host_writer.write(build_packet(0x9A, rejected_begin))
        await host_writer.drain()
        top_upload = await read_until_opcode(host_reader, 0x9D, limit=60)
        self.assertEqual(read_u32(top_upload, 0x0D), 2)

        replay_bytes = bytes(range(256)) * 150
        upload_id = 77
        begin = bytearray(0xB1 - HEADER_BYTES)
        struct.pack_into("<IIIII", begin, 0, upload_id, len(replay_bytes), 2, 0, game_id)
        begin[20:36] = token
        begin[36 : 36 + len(b"RankMatch.ply")] = b"RankMatch.ply"
        host_writer.write(build_packet(0x9A, begin))
        await host_writer.drain()
        accepted = await read_until_opcode(host_reader, 0x9D, limit=40)
        self.assertEqual(read_u32(accepted, 0x0D), 0)
        self.assertEqual(read_u32(accepted, 0x11), upload_id)

        offset = 0
        while offset < len(replay_bytes):
            chunk = replay_bytes[offset : offset + 32 * 1024]
            host_writer.write(
                build_packet(0x9B, struct.pack("<II", upload_id, offset) + chunk)
            )
            offset += len(chunk)
        host_writer.write(
            build_packet(
                0x9C,
                struct.pack("<IIQ", upload_id, len(replay_bytes), fnv1a64(replay_bytes)),
            )
        )
        await host_writer.drain()
        uploaded = await read_until_opcode(host_reader, 0x9D, limit=40)
        self.assertEqual(read_u32(uploaded, 0x0D), 0)
        replay_id = read_u32(uploaded, 0x15)
        self.assertNotEqual(replay_id, 0)

        join_writer.write(build_packet(0x9E, struct.pack("<I", 0)))
        await join_writer.drain()
        listing = await read_until_opcode(join_reader, 0x9F, limit=40)
        self.assertEqual(read_u32(listing, 0x11), 1)
        self.assertEqual(read_u32(listing, 0x15), replay_id)
        self.assertEqual(read_u32(listing, 0x19), len(replay_bytes))
        self.assertEqual(read_c_string(listing, 0x49, 0x7C), "RankMatch.ply")

        join_writer.write(build_packet(0xA0, struct.pack("<I", replay_id)))
        await join_writer.drain()
        downloaded = bytearray()
        while True:
            packet = await read_packet(join_reader)
            opcode = read_u32(packet, 4)
            if opcode == 0xA1:
                self.assertEqual(read_u32(packet, 0x0D), replay_id)
                self.assertEqual(read_u32(packet, 0x11), len(replay_bytes))
                self.assertEqual(read_u32(packet, 0x15), len(downloaded))
                downloaded.extend(packet[0x19:])
            elif opcode == 0xA2:
                self.assertEqual(read_u32(packet, 0x0D), replay_id)
                self.assertEqual(read_u32(packet, 0x11), 0)
                self.assertEqual(read_u32(packet, 0x15), len(replay_bytes))
                break
        self.assertEqual(bytes(downloaded), replay_bytes)


if __name__ == "__main__":
    unittest.main()
