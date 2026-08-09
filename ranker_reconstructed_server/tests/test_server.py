from __future__ import annotations

import asyncio
import socket
import struct
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


async def read_packet(reader: asyncio.StreamReader) -> bytes:
    header = await reader.readexactly(12)
    packet_bytes = read_u32(header, 8)
    return header + await reader.readexactly(packet_bytes - 12)


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


if __name__ == "__main__":
    unittest.main()
