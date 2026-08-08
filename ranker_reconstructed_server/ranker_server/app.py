"""Asyncio implementation of the reconstructed WizardNet control server."""

from __future__ import annotations

import asyncio
import contextlib
import ipaddress
import logging
import socket
import struct
import time
from typing import Iterable

from .accounts import AccountStore
from .config import ServerConfig
from .protocol import (
    HEADER_BYTES,
    Packet,
    ProtocolError,
    StreamDecoder,
    build_colored_text_packet,
    build_packet,
    build_status_packet,
    read_c_string,
    read_u32,
    write_fixed_text,
    write_i32,
    write_u32,
)
from .state import AdvertisedGame, ClientSession, LobbyChannel, ServerState


LOGGER = logging.getLogger("ranker_server")


class RankerServer:
    def __init__(self, config: ServerConfig) -> None:
        self.config = config
        self.state = ServerState(config.default_lobby_name)
        self.accounts = AccountStore(config.account_file)
        self._server: asyncio.Server | None = None
        self._cleanup_task: asyncio.Task[None] | None = None

    @property
    def bound_port(self) -> int:
        if self._server is None or not self._server.sockets:
            return 0
        return int(self._server.sockets[0].getsockname()[1])

    async def start(self) -> None:
        if self._server is not None:
            return
        self._server = await asyncio.start_server(
            self._accept_client,
            self.config.host,
            self.config.port,
            limit=1024 * 1024,
        )
        self._cleanup_task = asyncio.create_task(self._cleanup_expired_games())
        sockets = ", ".join(str(sock.getsockname()) for sock in self._server.sockets or [])
        LOGGER.info("%s listening on %s", self.config.server_name, sockets)

    async def serve_forever(self) -> None:
        await self.start()
        assert self._server is not None
        async with self._server:
            await self._server.serve_forever()

    async def close(self) -> None:
        if self._cleanup_task is not None:
            self._cleanup_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._cleanup_task
            self._cleanup_task = None
        sessions = list(self.state.clients.values())
        for session in sessions:
            session.writer.close()
        for session in sessions:
            with contextlib.suppress(Exception):
                await session.writer.wait_closed()
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None

    async def _accept_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        peer = writer.get_extra_info("peername")
        peer_host = str(peer[0]) if peer else "0.0.0.0"
        peer_port = int(peer[1]) if peer else 0
        if len(self.state.clients) >= self.config.max_clients:
            writer.write(build_status_packet(2, 6))
            await writer.drain()
            writer.close()
            await writer.wait_closed()
            return

        client_id = self.state.allocate_client_id()
        session = ClientSession(client_id, reader, writer, peer_host, peer_port)
        self.state.clients[client_id] = session
        LOGGER.info("client %d connected from %s:%d", client_id, peer_host, peer_port)
        decoder = StreamDecoder()
        try:
            while data := await reader.read(16384):
                for packet in decoder.feed(data):
                    if not packet.checksum_valid:
                        LOGGER.warning(
                            "client %d opcode 0x%02x has a bad checksum",
                            client_id,
                            packet.opcode,
                        )
                        continue
                    await self._dispatch(session, packet)
        except ProtocolError as error:
            LOGGER.warning("client %d protocol error: %s", client_id, error)
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        except Exception:
            LOGGER.exception("client %d handler failed", client_id)
        finally:
            await self._remove_client(session)

    async def _send(self, session: ClientSession, data: bytes) -> None:
        if session.writer.is_closing():
            return
        async with session.send_lock:
            session.writer.write(data)
            await session.writer.drain()

    async def _broadcast(
        self,
        sessions: Iterable[ClientSession],
        data: bytes,
        *,
        exclude: int | None = None,
    ) -> None:
        targets = [
            session
            for session in sessions
            if session.client_id != exclude and not session.writer.is_closing()
        ]
        if targets:
            await asyncio.gather(
                *(self._send(session, data) for session in targets),
                return_exceptions=True,
            )

    async def _dispatch(self, session: ClientSession, packet: Packet) -> None:
        opcode = packet.opcode
        LOGGER.debug(
            "client %d (%s) opcode=0x%02x bytes=%d",
            session.client_id,
            session.account or "unauthenticated",
            opcode,
            len(packet.raw),
        )

        if opcode == 0x1F:
            session.locale = read_c_string(packet.raw, 0x0D, 0x80)
            return
        if opcode in (0x2B, 0x2D, 0x40, 0x43):
            return
        if opcode == 0x01:
            await self._handle_login(session, packet)
            return
        if opcode == 0x03:
            await self._handle_create_account(session, packet)
            return
        if opcode == 0x39:
            await self._handle_account_status(session, packet)
            return
        if not session.authenticated:
            await self._send(session, build_status_packet(2, 7))
            return

        handlers = {
            0x05: self._handle_join_lobby,
            0x08: self._handle_create_lobby,
            0x0E: self._handle_lobby_reconnect,
            0x10: self._handle_online_reset,
            0x12: self._handle_online_page,
            0x14: self._handle_lobby_list,
            0x19: self._handle_host_game,
            0x1B: self._handle_remove_game,
            0x1D: self._handle_game_list,
            0x2A: self._handle_chat,
            0x37: self._handle_profile,
            0x3D: self._handle_top_bottom_counts,
            0x45: self._handle_game_type_counts,
            0x63: self._handle_use_map_counts,
            0x75: self._handle_empty_friend_list,
            0x77: self._handle_friend_add,
            0x79: self._handle_friend_remove,
            0x7D: self._handle_empty_guild_list,
            0x7F: self._handle_empty_guild_site,
            0x83: self._handle_search,
        }
        handler = handlers.get(opcode)
        if handler is None:
            LOGGER.debug("client %d unimplemented opcode 0x%02x", session.client_id, opcode)
            return
        await handler(session, packet)

    async def _handle_login(self, session: ClientSession, packet: Packet) -> None:
        account = read_c_string(packet.raw, 0x0D, 0x80).strip()
        password = read_c_string(packet.raw, 0x8D, 0x80)
        if not account:
            await self._send(session, build_status_packet(2, 7))
            return
        active = self.state.find_client_by_account(account)
        if active is not None and active.client_id != session.client_id:
            await self._send(session, build_status_packet(2, 1))
            return
        if not self.accounts.exists(account):
            if not self.config.auto_register_accounts:
                await self._send(session, build_status_packet(2, 7))
                return
            self.accounts.create(account, password, {"source": "automatic_login"})
        elif not self.accounts.verify(account, password):
            await self._send(session, build_status_packet(2, 2))
            return

        old_lobby = self.state.lobbies.get(session.lobby_id)
        session.account = account
        session.view = "online"
        if old_lobby is not None:
            old_lobby.members.add(session.client_id)
        await self._send(session, build_status_packet(2, 0))
        LOGGER.info("client %d logged in as %s", session.client_id, account)
        await self._broadcast_online_presence(session, added=True, exclude=session.client_id)

    async def _handle_account_status(
        self, session: ClientSession, packet: Packet
    ) -> None:
        message = "Create a local WizardNet account. All fields are required."
        await self._send(session, build_packet(0x3A, message.encode("cp949") + b"\0"))

    async def _handle_create_account(
        self, session: ClientSession, packet: Packet
    ) -> None:
        account = read_c_string(packet.raw, 0x0D, 0x20).strip()
        password = read_c_string(packet.raw, 0x2D, 0x20)
        if self.accounts.exists(account):
            await self._send(session, build_status_packet(4, 1))
            return
        if not self._valid_account_text(account, 4, 31):
            await self._send(session, build_status_packet(4, 6))
            return
        if not self._valid_account_text(password, 4, 9):
            await self._send(session, build_status_packet(4, 7))
            return
        profile = {
            "intro": read_c_string(packet.raw, 0x5D, 0x20),
            "avatar": read_u32(packet.raw, 0x7D),
            "location": read_u32(packet.raw, 0x81),
            "birth_year": read_u32(packet.raw, 0x85),
            "sex": read_u32(packet.raw, 0x89),
            "hp": read_u32(packet.raw, 0x8D),
            "mp": read_u32(packet.raw, 0x91),
            "op": read_u32(packet.raw, 0x95),
            "dp": read_u32(packet.raw, 0x99),
        }
        if not self.accounts.create(account, password, profile):
            await self._send(session, build_status_packet(4, 1))
            return
        session.account = account
        session.view = "online"
        lobby = self.state.lobbies.get(session.lobby_id)
        if lobby is not None:
            lobby.members.add(session.client_id)
        await self._send(session, build_status_packet(4, 0))
        LOGGER.info("client %d created account %s", session.client_id, account)
        await self._broadcast_online_presence(session, added=True, exclude=session.client_id)

    @staticmethod
    def _valid_account_text(value: str, minimum: int, maximum: int) -> bool:
        if not minimum <= len(value) <= maximum:
            return False
        return all(character.isprintable() and not character.isspace() for character in value)

    async def _handle_lobby_reconnect(self, session: ClientSession, packet: Packet) -> None:
        session.view = "online"

    async def _handle_online_reset(self, session: ClientSession, packet: Packet) -> None:
        session.view = "online"
        await self._send(session, build_packet(0x11))

    async def _handle_online_page(self, session: ClientSession, packet: Packet) -> None:
        session.view = "online"
        page = read_u32(packet.raw, 0x0D)
        members = self.state.lobby_clients(session.lobby_id)
        if page >= len(members):
            payload = bytearray(4)
            write_i32(payload, 0, -1)
            await self._send(session, build_packet(0x13, payload))
            return
        member = members[page]
        payload = bytearray(0x65 - HEADER_BYTES)
        write_u32(payload, 0x0D - HEADER_BYTES, page)
        write_u32(payload, 0x11 - HEADER_BYTES, member.client_id)
        write_fixed_text(payload, 0x15 - HEADER_BYTES, 0x20, member.account)
        await self._send(session, build_packet(0x13, payload))

    async def _handle_chat(self, session: ClientSession, packet: Packet) -> None:
        if len(packet.raw) <= HEADER_BYTES + 8:
            return
        session.view = "online" if session.view != "link" else session.view
        raw_chat = struct.pack("<I", 0) + packet.raw[HEADER_BYTES:]
        await self._broadcast(
            self.state.lobby_clients(session.lobby_id),
            raw_chat,
            exclude=session.client_id,
        )

    async def _handle_lobby_list(self, session: ClientSession, packet: Packet) -> None:
        session.view = "change"
        start_index = read_u32(packet.raw, 0x0D)
        lobbies = sorted(self.state.lobbies.values(), key=lambda lobby: lobby.lobby_id)
        payload = bytearray(0x99 - HEADER_BYTES)
        if start_index < len(lobbies):
            lobby = lobbies[start_index]
            write_fixed_text(payload, 0x0D - HEADER_BYTES, 0x80, lobby.name)
            write_u32(payload, 0x8D - HEADER_BYTES, 1 if lobby.password else 0)
            write_i32(payload, 0x91 - HEADER_BYTES, start_index)
            write_u32(payload, 0x95 - HEADER_BYTES, lobby.lobby_id)
        else:
            write_i32(payload, 0x91 - HEADER_BYTES, -1)
        await self._send(session, build_packet(0x15, payload))

    async def _move_client_to_lobby(
        self, session: ClientSession, lobby: LobbyChannel
    ) -> None:
        old_lobby_id = session.lobby_id
        old_lobby = self.state.lobbies.get(old_lobby_id)
        if old_lobby is not None:
            old_lobby.members.discard(session.client_id)
            await self._broadcast_online_presence(
                session, added=False, lobby_id=old_lobby_id, exclude=session.client_id
            )
        session.lobby_id = lobby.lobby_id
        lobby.members.add(session.client_id)
        await self._broadcast_online_presence(session, added=True, exclude=session.client_id)

    async def _handle_join_lobby(self, session: ClientSession, packet: Packet) -> None:
        lobby_id = read_u32(packet.raw, 0x0D)
        password = read_c_string(packet.raw, 0x11, 0x20)
        lobby = self.state.lobbies.get(lobby_id)
        status = 0
        if lobby is None:
            status = 0
        elif len(lobby.members) >= self.config.max_lobby_members:
            status = 1
        elif lobby.password != password:
            status = 2
        else:
            status = 3
            await self._move_client_to_lobby(session, lobby)
        await self._send(session, build_status_packet(6, status))

    async def _handle_create_lobby(self, session: ClientSession, packet: Packet) -> None:
        name = read_c_string(packet.raw, 0x0D, 0x80).strip()
        password = read_c_string(packet.raw, 0x8D, 0x20)
        if not name or self.state.find_lobby_by_name(name) is not None:
            await self._send(session, build_status_packet(9, 0))
            return
        lobby = LobbyChannel(self.state.allocate_lobby_id(), name, password)
        self.state.lobbies[lobby.lobby_id] = lobby
        await self._move_client_to_lobby(session, lobby)
        await self._send(session, build_status_packet(9, 1))

    async def _handle_search(self, session: ClientSession, packet: Packet) -> None:
        session.view = "search"
        page = read_u32(packet.raw, 0x1D)
        all_clients = sorted(
            (client for client in self.state.clients.values() if client.authenticated),
            key=lambda client: client.account.casefold(),
        )
        page_size = 10
        selected = all_clients[page * page_size : (page + 1) * page_size]
        payload = bytearray(4 + len(selected) * 0x2C)
        write_u32(payload, 0, len(selected))
        for index, client in enumerate(selected):
            offset = 4 + index * 0x2C
            write_fixed_text(payload, offset, 0x20, client.account)
            write_u32(payload, offset + 0x20, 0)
            write_u32(payload, offset + 0x24, 20)
            write_u32(payload, offset + 0x28, 0)
        await self._send(session, build_packet(0x84, payload))

    async def _handle_profile(self, session: ClientSession, packet: Packet) -> None:
        target = read_c_string(packet.raw, 0x0D, 0x20)
        payload = bytearray(0x200 - HEADER_BYTES)
        write_fixed_text(payload, 0, 0x20, target)
        await self._send(session, build_packet(0x38, payload))

    def _public_sockaddr(self, session: ClientSession, source: bytes) -> bytes:
        if len(source) < 16:
            return b"\0" * 16
        result = bytearray(source[:16])
        try:
            source_ip = ipaddress.ip_address(socket.inet_ntoa(result[4:8]))
            peer_ip = ipaddress.ip_address(session.peer_host)
        except ValueError:
            return bytes(result)
        if self.config.advertise_peer_address and (
            source_ip.is_unspecified
            or source_ip.is_loopback != peer_ip.is_loopback
            or (not peer_ip.is_private and source_ip.is_private)
        ):
            result[4:8] = socket.inet_aton(str(peer_ip))
        return bytes(result)

    async def _handle_host_game(self, session: ClientSession, packet: Packet) -> None:
        session.view = "create"
        name = read_c_string(packet.raw, 0x0D, 0x80).strip()
        password = read_c_string(packet.raw, 0x8D, 0x80)
        duplicate = next(
            (
                game
                for game in self.state.lobby_games(session.lobby_id)
                if game.name.casefold() == name.casefold()
            ),
            None,
        )
        if not name or duplicate is not None:
            await self._send(session, build_status_packet(0x1A, 0))
            return
        if len(packet.raw) < 0x12D + 0x2DC:
            await self._send(session, build_status_packet(0x1A, 0))
            return
        if session.hosted_game_id is not None:
            await self._remove_game(session.hosted_game_id)
        game_id = self.state.allocate_game_id()
        game = AdvertisedGame(
            game_id=game_id,
            host_client_id=session.client_id,
            lobby_id=session.lobby_id,
            name=name,
            password=password,
            sockaddr=self._public_sockaddr(session, packet.raw[0x10D:0x11D]),
            game_type=read_u32(packet.raw, 0x11D),
            map_descriptor=bytes(packet.raw[0x12D : 0x12D + 0x2DC]),
        )
        self.state.games[game_id] = game
        session.hosted_game_id = game_id
        session.view = "link"
        await self._send(session, build_status_packet(0x1A, 1))
        LOGGER.info("%s advertised game %s (%d)", session.account, name, game_id)
        await self._broadcast_game_update(game, added=True, exclude=session.client_id)

    async def _handle_game_list(self, session: ClientSession, packet: Packet) -> None:
        session.view = "free_server"
        start_index = read_u32(packet.raw, 0x0D)
        games = self.state.lobby_games(session.lobby_id)
        if start_index < len(games):
            response = self._build_game_record_packet(0x1E, games[start_index], start_index)
        else:
            response = self._build_empty_game_record_packet(0x1E)
        await self._send(session, response)

    async def _handle_remove_game(self, session: ClientSession, packet: Packet) -> None:
        game_id = read_u32(packet.raw, 0x0D)
        await self._remove_game(game_id)

    async def _handle_top_bottom_counts(
        self, session: ClientSession, packet: Packet
    ) -> None:
        session.view = "free_server"
        count = self.config.rank_game_count
        await self._send(session, build_packet(0x3E, struct.pack("<III", count, count, 0)))

    async def _handle_game_type_counts(
        self, session: ClientSession, packet: Packet
    ) -> None:
        session.view = "free_server"
        await self._send(session, build_packet(0x46, b"\0" * 20))

    async def _handle_use_map_counts(
        self, session: ClientSession, packet: Packet
    ) -> None:
        session.view = "free_server"
        count = self.config.rank_game_count
        await self._send(session, build_packet(100, struct.pack("<10I", *([count] * 10))))

    async def _handle_empty_friend_list(
        self, session: ClientSession, packet: Packet
    ) -> None:
        await self._send(session, build_packet(0x76, struct.pack("<i", 0)))

    async def _handle_friend_add(self, session: ClientSession, packet: Packet) -> None:
        await self._send(session, build_status_packet(0x78, 0))

    async def _handle_friend_remove(
        self, session: ClientSession, packet: Packet
    ) -> None:
        await self._send(session, build_status_packet(0x7A, 0))

    async def _handle_empty_guild_list(
        self, session: ClientSession, packet: Packet
    ) -> None:
        await self._send(session, build_packet(0x7E, struct.pack("<i", 0)))

    async def _handle_empty_guild_site(
        self, session: ClientSession, packet: Packet
    ) -> None:
        await self._send(session, build_packet(0x80, b"\0"))

    def _build_game_record_packet(
        self, opcode: int, game: AdvertisedGame, next_index: int = -1
    ) -> bytes:
        total_bytes = 0x0B9 + 0x2DC
        payload = bytearray(total_bytes - HEADER_BYTES)
        write_fixed_text(payload, 0x0D - HEADER_BYTES, 0x80, game.name)
        payload[0x8D - HEADER_BYTES : 0x8D - HEADER_BYTES + 16] = game.sockaddr
        payload[0x9D - HEADER_BYTES : 0x9D - HEADER_BYTES + 16] = game.sockaddr
        write_i32(payload, 0xAD - HEADER_BYTES, next_index)
        write_u32(payload, 0xB1 - HEADER_BYTES, game.game_id)
        write_u32(payload, 0xB5 - HEADER_BYTES, 1 if game.password else 0)
        payload[0xB9 - HEADER_BYTES :] = game.map_descriptor
        return build_packet(opcode, payload)

    def _build_empty_game_record_packet(self, opcode: int) -> bytes:
        payload = bytearray(0x0B9 + 0x2DC - HEADER_BYTES)
        write_i32(payload, 0xAD - HEADER_BYTES, -1)
        return build_packet(opcode, payload)

    async def _broadcast_online_presence(
        self,
        subject: ClientSession,
        *,
        added: bool,
        lobby_id: int | None = None,
        exclude: int | None = None,
    ) -> None:
        target_lobby = subject.lobby_id if lobby_id is None else lobby_id
        if added:
            payload = bytearray(0x61 - HEADER_BYTES)
            write_fixed_text(payload, 0, 0x20, subject.account)
            write_u32(payload, 0x4D - HEADER_BYTES, subject.client_id)
            packet = build_packet(7, payload)
        else:
            packet = build_packet(0x23, struct.pack("<I", subject.client_id))
        targets = [
            client
            for client in self.state.lobby_clients(target_lobby)
            if client.view == "online"
        ]
        await self._broadcast(targets, packet, exclude=exclude)

    async def _broadcast_game_update(
        self, game: AdvertisedGame, *, added: bool, exclude: int | None = None
    ) -> None:
        targets = [
            client
            for client in self.state.lobby_clients(game.lobby_id)
            if client.view == "free_server"
        ]
        packet = (
            self._build_game_record_packet(0x27, game)
            if added
            else build_packet(0x26, struct.pack("<I", game.game_id))
        )
        await self._broadcast(targets, packet, exclude=exclude)

    async def _remove_game(self, game_id: int) -> None:
        game = self.state.games.pop(game_id, None)
        if game is None:
            return
        host = self.state.clients.get(game.host_client_id)
        if host is not None and host.hosted_game_id == game_id:
            host.hosted_game_id = None
        await self._broadcast_game_update(game, added=False)
        LOGGER.info("removed advertised game %s (%d)", game.name, game.game_id)

    async def _remove_client(self, session: ClientSession) -> None:
        if session.client_id not in self.state.clients:
            return
        if session.authenticated:
            await self._broadcast_online_presence(
                session, added=False, exclude=session.client_id
            )
        lobby = self.state.lobbies.get(session.lobby_id)
        if lobby is not None:
            lobby.members.discard(session.client_id)
        if session.hosted_game_id is not None:
            await self._remove_game(session.hosted_game_id)
        self.state.clients.pop(session.client_id, None)
        session.writer.close()
        with contextlib.suppress(Exception):
            await session.writer.wait_closed()
        LOGGER.info("client %d disconnected", session.client_id)

    async def _cleanup_expired_games(self) -> None:
        while True:
            await asyncio.sleep(30)
            cutoff = time.monotonic() - self.config.room_ttl_seconds
            expired = [
                game.game_id
                for game in self.state.games.values()
                if game.created_at < cutoff
            ]
            for game_id in expired:
                await self._remove_game(game_id)
