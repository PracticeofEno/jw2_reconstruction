"""Asyncio implementation of the reconstructed WizardNet control server."""

from __future__ import annotations

import asyncio
import contextlib
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import ipaddress
import hmac
import json
import logging
from pathlib import Path
import socket
import struct
import tempfile
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
from .replays import (
    ReplayCatalog,
    build_replay_filename,
    fnv1a64,
    sanitize_replay_filename,
)
from .state import (
    AdvertisedGame,
    ClientSession,
    CompletedGame,
    LobbyChannel,
    PRESENCE_STATUS_HOSTING,
    PRESENCE_STATUS_LOBBY,
    PRESENCE_STATUS_PLAYING,
    PRESENCE_STATUS_ROOM_MEMBER,
    ServerState,
)


LOGGER = logging.getLogger("ranker_server")

RELAY_JOIN_REQUEST_OPCODE = 0x90
RELAY_LEAVE_REQUEST_OPCODE = 0x91
RELAY_FRAME_REQUEST_OPCODE = 0x92
RELAY_JOIN_STATUS_OPCODE = 0x93
RELAY_FRAME_OPCODE = 0x94
RELAY_MEMBER_LEFT_OPCODE = 0x95
LOBBY_MARK_SET_REQUEST_OPCODE = 0x96
LOBBY_MARK_SET_RESPONSE_OPCODE = 0x97
MATCH_RESULT_REQUEST_OPCODE = 0x98
MATCH_RESULT_RESPONSE_OPCODE = 0x99
REPLAY_UPLOAD_BEGIN_OPCODE = 0x9A
REPLAY_UPLOAD_CHUNK_OPCODE = 0x9B
REPLAY_UPLOAD_END_OPCODE = 0x9C
REPLAY_UPLOAD_STATUS_OPCODE = 0x9D
REPLAY_LIST_REQUEST_OPCODE = 0x9E
REPLAY_LIST_RESPONSE_OPCODE = 0x9F
REPLAY_DOWNLOAD_REQUEST_OPCODE = 0xA0
REPLAY_DOWNLOAD_CHUNK_OPCODE = 0xA1
REPLAY_DOWNLOAD_FINISH_OPCODE = 0xA2
LOBBY_MARK_COUNT = 5
RELAY_MAX_MEMBERS = 8
RELAY_STREAM_LINK = 0
RELAY_STREAM_MODE1 = 1
RELAY_CIPHER_MAGIC = b"WRL1"
RELAY_CIPHER_HEADER_BYTES = 28

RELAY_STATUS_OK = 0
RELAY_STATUS_NO_GAME = 1
RELAY_STATUS_FULL = 2
RELAY_STATUS_NOT_MEMBER = 3
RELAY_STATUS_HOST_MISSING = 4

REPLAY_TRANSFER_CHUNK_BYTES = 32 * 1024
REPLAY_LIST_PAGE_COUNT = 64
REPLAY_LIST_RECORD_BYTES = 0xB0
MATCH_TOKEN_BYTES = 16
MAP_DESCRIPTOR_TITLE_OFFSET = 0x08
MAP_DESCRIPTOR_TITLE_BYTES = 0x20
MAP_DESCRIPTOR_FILENAME_OFFSET = 0x19C
MAP_DESCRIPTOR_FILENAME_BYTES = 0x100


@dataclass(slots=True)
class ActiveReplayUpload:
    upload_id: int
    temporary_path: Path
    display_name: str
    expected_bytes: int
    received_bytes: int
    hash_value: int
    game_type: int
    game_id: int
    match_token: bytes


class RankerServer:
    def __init__(self, config: ServerConfig) -> None:
        self.config = config
        self.state = ServerState(config.default_lobby_name)
        self.accounts = AccountStore(config.account_file)
        self._temporary_replay_directory: tempfile.TemporaryDirectory[str] | None = None
        if config.replay_dir is None:
            self._temporary_replay_directory = tempfile.TemporaryDirectory(
                prefix="ranker_replays_"
            )
            replay_directory = Path(self._temporary_replay_directory.name)
        else:
            replay_directory = config.replay_dir
        self.replays = ReplayCatalog(replay_directory)
        self._replay_uploads: dict[int, ActiveReplayUpload] = {}
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
        for upload in self._replay_uploads.values():
            with contextlib.suppress(OSError):
                upload.temporary_path.unlink()
        self._replay_uploads.clear()
        if self._temporary_replay_directory is not None:
            self._temporary_replay_directory.cleanup()
            self._temporary_replay_directory = None

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

        self._configure_client_keepalive(writer)
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

    def _configure_client_keepalive(self, writer: asyncio.StreamWriter) -> None:
        client_socket = writer.get_extra_info("socket")
        if client_socket is None:
            return
        try:
            client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            if hasattr(socket, "TCP_KEEPIDLE"):
                client_socket.setsockopt(
                    socket.IPPROTO_TCP,
                    socket.TCP_KEEPIDLE,
                    self.config.client_keepalive_idle_seconds,
                )
            elif hasattr(socket, "TCP_KEEPALIVE"):
                client_socket.setsockopt(
                    socket.IPPROTO_TCP,
                    socket.TCP_KEEPALIVE,
                    self.config.client_keepalive_idle_seconds,
                )
            if hasattr(socket, "TCP_KEEPINTVL"):
                client_socket.setsockopt(
                    socket.IPPROTO_TCP,
                    socket.TCP_KEEPINTVL,
                    self.config.client_keepalive_interval_seconds,
                )
            if hasattr(socket, "TCP_KEEPCNT"):
                client_socket.setsockopt(
                    socket.IPPROTO_TCP,
                    socket.TCP_KEEPCNT,
                    self.config.client_keepalive_probe_count,
                )
        except (AttributeError, OSError) as error:
            # Keep serving older platforms even if they expose only part of
            # the TCP keepalive option set. A normal FIN/RST is still handled
            # by _remove_client, while the room TTL remains the final fallback.
            LOGGER.warning("unable to configure client TCP keepalive: %s", error)

    async def _send(self, session: ClientSession, data: bytes) -> None:
        if session.writer.is_closing():
            return
        async with session.send_lock:
            if session.writer.is_closing():
                return
            try:
                session.writer.write(data)
                await asyncio.wait_for(
                    session.writer.drain(),
                    timeout=self.config.send_timeout_seconds,
                )
            except asyncio.TimeoutError:
                LOGGER.warning(
                    "client %d send timed out after %.2fs; closing connection",
                    session.client_id,
                    self.config.send_timeout_seconds,
                )
                session.writer.close()
            except (ConnectionError, OSError) as error:
                LOGGER.debug(
                    "client %d send failed; closing connection: %s",
                    session.client_id,
                    error,
                )
                session.writer.close()

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
            0x28: self._handle_start_game,
            RELAY_JOIN_REQUEST_OPCODE: self._handle_relay_join,
            RELAY_LEAVE_REQUEST_OPCODE: self._handle_relay_leave,
            RELAY_FRAME_REQUEST_OPCODE: self._handle_relay_frame,
            LOBBY_MARK_SET_REQUEST_OPCODE: self._handle_lobby_mark_set,
            MATCH_RESULT_REQUEST_OPCODE: self._handle_match_result,
            REPLAY_UPLOAD_BEGIN_OPCODE: self._handle_replay_upload_begin,
            REPLAY_UPLOAD_CHUNK_OPCODE: self._handle_replay_upload_chunk,
            REPLAY_UPLOAD_END_OPCODE: self._handle_replay_upload_end,
            REPLAY_LIST_REQUEST_OPCODE: self._handle_replay_list,
            REPLAY_DOWNLOAD_REQUEST_OPCODE: self._handle_replay_download,
            0x2A: self._handle_chat,
            0x2F: self._handle_rank_list,
            0x33: self._handle_rank_search,
            0x35: self._handle_profile_update,
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
        stored_mark = self.accounts.profile_value(account, "lobby_mark", 0)
        session.lobby_mark = (
            stored_mark
            if isinstance(stored_mark, int) and 0 <= stored_mark < LOBBY_MARK_COUNT
            else 0
        )
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
            "lobby_mark": 0,
        }
        if not self.accounts.create(account, password, profile):
            await self._send(session, build_status_packet(4, 1))
            return
        session.account = account
        session.lobby_mark = 0
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
        if session.hosted_game_id is not None:
            await self._remove_game(session.hosted_game_id)
        elif session.relay_game_id is not None:
            await self._remove_relay_member(session)
        session.view = "online"
        session.presence_status = PRESENCE_STATUS_LOBBY
        session.presence_location = ""
        # A WizardNet game keeps the authenticated TCP connection alive while
        # both clients are in the P2P session. They can return in either order,
        # so real-time presence notifications alone are insufficient: the
        # later client was not viewing the online lobby when the earlier one
        # returned. Send an authoritative snapshot to the returning client and
        # announce it to peers that have already returned.
        await self._send_online_presence_snapshot(session)
        await self._broadcast_online_presence(
            session, added=True, exclude=session.client_id
        )

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
        payload = bytearray(0x89 - HEADER_BYTES)
        write_u32(payload, 0x0D - HEADER_BYTES, page)
        write_u32(payload, 0x11 - HEADER_BYTES, member.client_id)
        write_fixed_text(payload, 0x15 - HEADER_BYTES, 0x20, member.account)
        write_u32(payload, 0x5D - HEADER_BYTES, member.lobby_mark)
        write_u32(payload, 0x65 - HEADER_BYTES, member.presence_status)
        write_fixed_text(
            payload,
            0x69 - HEADER_BYTES,
            0x20,
            member.presence_location,
        )
        await self._send(session, build_packet(0x13, payload))

    async def _handle_lobby_mark_set(
        self, session: ClientSession, packet: Packet
    ) -> None:
        requested_mark = read_u32(packet.raw, 0x0D, LOBBY_MARK_COUNT)
        if requested_mark >= LOBBY_MARK_COUNT:
            await self._send(
                session,
                build_packet(
                    LOBBY_MARK_SET_RESPONSE_OPCODE,
                    struct.pack("<II", 1, session.lobby_mark),
                ),
            )
            return

        if not self.accounts.set_profile_value(
            session.account, "lobby_mark", requested_mark
        ):
            await self._send(
                session,
                build_packet(
                    LOBBY_MARK_SET_RESPONSE_OPCODE,
                    struct.pack("<II", 2, session.lobby_mark),
                ),
            )
            return

        session.lobby_mark = requested_mark
        await self._send(
            session,
            build_packet(
                LOBBY_MARK_SET_RESPONSE_OPCODE,
                struct.pack("<II", 0, session.lobby_mark),
            ),
        )
        await self._broadcast_online_presence(session, added=True)

    @staticmethod
    def _statistics_bucket(game_type: int) -> str | None:
        if game_type in (0, 1):
            return "normal"
        if game_type == 2:
            return "rank"
        return None

    @staticmethod
    def _outcome_name(outcome: int) -> str | None:
        return {0: "wins", 1: "losses", 2: "draws"}.get(outcome)

    @staticmethod
    def _replay_map_name(map_descriptor: bytes) -> str:
        title = read_c_string(
            map_descriptor, MAP_DESCRIPTOR_TITLE_OFFSET, MAP_DESCRIPTOR_TITLE_BYTES
        ).strip()
        if title:
            return title
        filename = read_c_string(
            map_descriptor,
            MAP_DESCRIPTOR_FILENAME_OFFSET,
            MAP_DESCRIPTOR_FILENAME_BYTES,
        ).strip()
        if filename:
            return Path(filename.replace("\\", "/")).stem
        return "Map"

    def _replay_match_metadata(
        self, game_id: int, match_token: bytes
    ) -> tuple[str, tuple[str, ...]]:
        game = self.state.games.get(game_id)
        if game is not None and hmac.compare_digest(
            game.relay_secret[:MATCH_TOKEN_BYTES], match_token
        ):
            players = game.participant_accounts_ordered or tuple(
                sorted(game.participant_accounts, key=str.casefold)
            )
            return self._replay_map_name(game.map_descriptor), players

        completed = self.state.completed_games.get(match_token)
        if completed is not None and completed.game_id == game_id:
            return completed.map_name, completed.participant_accounts_ordered
        return "Map", ()

    def _validated_match(
        self,
        session: ClientSession,
        game_id: int,
        game_type: int,
        match_token: bytes,
        *,
        require_host: bool,
    ) -> bool:
        game = self.state.games.get(game_id)
        if game is not None:
            host = self.state.clients.get(game.host_client_id)
            host_account = host.account if host is not None else ""
            is_participant = session.account.casefold() in {
                account.casefold() for account in game.participant_accounts
            }
            return (
                game.started
                and game.game_type == game_type
                and len(game.participant_accounts) >= 2
                and is_participant
                and (not require_host or session.account.casefold() == host_account.casefold())
                and hmac.compare_digest(game.relay_secret[:MATCH_TOKEN_BYTES], match_token)
            )

        completed = self.state.completed_games.get(match_token)
        if completed is None:
            return False
        is_participant = session.account.casefold() in {
            account.casefold() for account in completed.participant_accounts
        }
        return (
            completed.game_id == game_id
            and completed.game_type == game_type
            and len(completed.participant_accounts) >= 2
            and is_participant
            and (
                not require_host
                or session.account.casefold() == completed.host_account.casefold()
            )
        )

    async def _handle_match_result(
        self, session: ClientSession, packet: Packet
    ) -> None:
        if len(packet.raw) < 0x29:
            await self._send(
                session, build_packet(MATCH_RESULT_RESPONSE_OPCODE, struct.pack("<II", 2, 0))
            )
            return
        game_type = read_u32(packet.raw, 0x0D)
        outcome = read_u32(packet.raw, 0x11)
        game_id = read_u32(packet.raw, 0x15)
        match_token = bytes(packet.raw[0x19:0x29])
        bucket = self._statistics_bucket(game_type)
        outcome_name = self._outcome_name(outcome)
        status = 0
        if bucket is None or outcome_name is None:
            status = 3
        elif not self._validated_match(
            session, game_id, game_type, match_token, require_host=False
        ):
            status = 2
        elif not self.accounts.record_match(
            session.account, bucket, outcome_name, match_token.hex()
        ):
            status = 1
        LOGGER.info(
            "match result account=%s game=%d type=%d outcome=%d status=%d",
            session.account,
            game_id,
            game_type,
            outcome,
            status,
        )
        await self._send(
            session,
            build_packet(MATCH_RESULT_RESPONSE_OPCODE, struct.pack("<II", status, game_id)),
        )

    async def _send_replay_upload_status(
        self, session: ClientSession, status: int, upload_id: int, replay_id: int = 0
    ) -> None:
        await self._send(
            session,
            build_packet(
                REPLAY_UPLOAD_STATUS_OPCODE,
                struct.pack("<III", status, upload_id, replay_id),
            ),
        )

    def _discard_replay_upload(self, client_id: int) -> None:
        upload = self._replay_uploads.pop(client_id, None)
        if upload is not None:
            with contextlib.suppress(OSError):
                upload.temporary_path.unlink()

    async def _handle_replay_upload_begin(
        self, session: ClientSession, packet: Packet
    ) -> None:
        if len(packet.raw) < 0xB1:
            await self._send_replay_upload_status(session, 2, 0)
            return
        upload_id = read_u32(packet.raw, 0x0D)
        expected_bytes = read_u32(packet.raw, 0x11)
        game_type = read_u32(packet.raw, 0x15)
        game_id = read_u32(packet.raw, 0x1D)
        match_token = bytes(packet.raw[0x21:0x31])
        display_name = sanitize_replay_filename(
            read_c_string(packet.raw, 0x31, 0x80)
        )
        if (
            upload_id == 0
            or expected_bytes == 0
            or expected_bytes > self.config.max_replay_bytes
            or game_type not in (1, 2)
            or not self._validated_match(
                session, game_id, game_type, match_token, require_host=False
            )
        ):
            await self._send_replay_upload_status(session, 2, upload_id)
            return

        self._discard_replay_upload(session.client_id)
        temporary_path = self.replays.directory / (
            f".upload_{session.client_id}_{upload_id}.part"
        )
        try:
            temporary_path.write_bytes(b"")
        except OSError:
            await self._send_replay_upload_status(session, 4, upload_id)
            return
        self._replay_uploads[session.client_id] = ActiveReplayUpload(
            upload_id=upload_id,
            temporary_path=temporary_path,
            display_name=display_name,
            expected_bytes=expected_bytes,
            received_bytes=0,
            hash_value=0xCBF29CE484222325,
            game_type=game_type,
            game_id=game_id,
            match_token=match_token,
        )
        await self._send_replay_upload_status(session, 0, upload_id)

    async def _handle_replay_upload_chunk(
        self, session: ClientSession, packet: Packet
    ) -> None:
        upload_id = read_u32(packet.raw, 0x0D)
        offset = read_u32(packet.raw, 0x11)
        data = packet.raw[0x15:]
        upload = self._replay_uploads.get(session.client_id)
        if (
            upload is None
            or upload.upload_id != upload_id
            or offset != upload.received_bytes
            or not data
            or len(data) > REPLAY_TRANSFER_CHUNK_BYTES
            or upload.received_bytes + len(data) > upload.expected_bytes
        ):
            self._discard_replay_upload(session.client_id)
            await self._send_replay_upload_status(session, 3, upload_id)
            return
        try:
            with upload.temporary_path.open("ab") as stream:
                stream.write(data)
        except OSError:
            self._discard_replay_upload(session.client_id)
            await self._send_replay_upload_status(session, 4, upload_id)
            return
        upload.received_bytes += len(data)
        upload.hash_value = fnv1a64(data, upload.hash_value)

    async def _handle_replay_upload_end(
        self, session: ClientSession, packet: Packet
    ) -> None:
        upload_id = read_u32(packet.raw, 0x0D)
        declared_bytes = read_u32(packet.raw, 0x11)
        declared_hash = (
            struct.unpack_from("<Q", packet.raw, 0x15)[0]
            if len(packet.raw) >= 0x1D
            else -1
        )
        upload = self._replay_uploads.pop(session.client_id, None)
        if (
            upload is None
            or upload.upload_id != upload_id
            or declared_bytes != upload.expected_bytes
            or upload.received_bytes != upload.expected_bytes
            or declared_hash != upload.hash_value
        ):
            if upload is not None:
                with contextlib.suppress(OSError):
                    upload.temporary_path.unlink()
            await self._send_replay_upload_status(session, 3, upload_id)
            return
        try:
            map_name, players = self._replay_match_metadata(
                upload.game_id, upload.match_token
            )
            display_name = build_replay_filename(map_name, players)
            match_key = hashlib.sha256(upload.match_token).hexdigest()
            commit = self.replays.commit_upload(
                upload.temporary_path,
                display_name=display_name,
                uploader=session.account,
                byte_count=upload.received_bytes,
                game_type=upload.game_type,
                game_id=upload.game_id,
                match_key=match_key,
            )
        except OSError:
            with contextlib.suppress(OSError):
                upload.temporary_path.unlink()
            await self._send_replay_upload_status(session, 4, upload_id)
            return
        entry = commit.entry
        if commit.stored:
            LOGGER.info(
                "replay uploaded id=%d account=%s game=%d bytes=%d name=%s",
                entry.replay_id,
                session.account,
                entry.game_id,
                entry.byte_count,
                entry.display_name,
            )
        else:
            LOGGER.info(
                "replay duplicate discarded id=%d account=%s game=%d bytes=%d",
                entry.replay_id,
                session.account,
                upload.game_id,
                upload.received_bytes,
            )
        await self._send_replay_upload_status(
            session, 0, upload_id, entry.replay_id
        )

    async def _handle_replay_list(
        self, session: ClientSession, packet: Packet
    ) -> None:
        offset = read_u32(packet.raw, 0x0D)
        entries = self.replays.entries()
        selected = entries[offset : offset + REPLAY_LIST_PAGE_COUNT]
        next_offset = (
            offset + len(selected)
            if offset + len(selected) < len(entries)
            else 0xFFFFFFFF
        )
        payload = bytearray(8 + len(selected) * REPLAY_LIST_RECORD_BYTES)
        write_u32(payload, 0, next_offset)
        write_u32(payload, 4, len(selected))
        for index, entry in enumerate(selected):
            base = 8 + index * REPLAY_LIST_RECORD_BYTES
            write_u32(payload, base, entry.replay_id)
            write_u32(payload, base + 4, entry.byte_count)
            struct.pack_into("<Q", payload, base + 8, entry.uploaded_at)
            write_u32(payload, base + 16, entry.game_type)
            write_fixed_text(payload, base + 20, 0x20, entry.uploader)
            write_fixed_text(payload, base + 52, 0x7C, entry.display_name)
        await self._send(session, build_packet(REPLAY_LIST_RESPONSE_OPCODE, payload))

    async def _handle_replay_download(
        self, session: ClientSession, packet: Packet
    ) -> None:
        replay_id = read_u32(packet.raw, 0x0D)
        entry = self.replays.get(replay_id)
        if entry is None:
            await self._send(
                session,
                build_packet(
                    REPLAY_DOWNLOAD_FINISH_OPCODE,
                    struct.pack("<III", replay_id, 1, 0),
                ),
            )
            return
        path = self.replays.path_for(entry)
        try:
            with path.open("rb") as stream:
                offset = 0
                while data := stream.read(REPLAY_TRANSFER_CHUNK_BYTES):
                    await self._send(
                        session,
                        build_packet(
                            REPLAY_DOWNLOAD_CHUNK_OPCODE,
                            struct.pack("<III", replay_id, entry.byte_count, offset)
                            + data,
                        ),
                    )
                    offset += len(data)
        except OSError:
            await self._send(
                session,
                build_packet(
                    REPLAY_DOWNLOAD_FINISH_OPCODE,
                    struct.pack("<III", replay_id, 2, 0),
                ),
            )
            return
        await self._send(
            session,
            build_packet(
                REPLAY_DOWNLOAD_FINISH_OPCODE,
                struct.pack("<III", replay_id, 0, entry.byte_count),
            ),
        )

    async def _handle_chat(self, session: ClientSession, packet: Packet) -> None:
        if len(packet.raw) <= HEADER_BYTES + 8:
            return
        if session.view == "link":
            LOGGER.debug("client %d ignored lobby chat while in relay link view", session.client_id)
            return
        session.view = "online"
        raw_chat = struct.pack("<I", 0) + packet.raw[HEADER_BYTES:]
        targets = [
            client
            for client in self.state.lobby_clients(session.lobby_id)
            if client.view == "online"
        ]
        await self._broadcast(
            targets,
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
        target = read_c_string(packet.raw, 0x0D, 0x20).strip() or session.account
        await self._send(session, self._build_profile_packet(0x38, target))

    async def _handle_rank_search(
        self, session: ClientSession, packet: Packet
    ) -> None:
        target = read_c_string(packet.raw, 0x11, 0x20).strip() or session.account
        await self._send(session, self._build_profile_packet(0x34, target))

    async def _handle_profile_update(
        self, session: ClientSession, packet: Packet
    ) -> None:
        location = read_u32(packet.raw, 0x35)
        description = read_c_string(packet.raw, 0x39, 0xFA)
        self.accounts.set_profile_value(session.account, "location", location)
        self.accounts.set_profile_value(session.account, "description", description)
        await self._send(session, build_status_packet(0x36, 0))

    def _ranked_accounts(self) -> list[str]:
        names = [
            name
            for name in self.accounts.account_names()
            if sum(self.accounts.statistics(name, "rank").values()) != 0
        ]
        return sorted(
            names,
            key=lambda name: (-self.accounts.rank_points(name), name.casefold()),
        )

    def _rank_position(self, account: str) -> int:
        statistics = self.accounts.statistics(account, "rank")
        if sum(statistics.values()) == 0:
            return -1
        folded = account.casefold()
        for index, name in enumerate(self._ranked_accounts(), start=1):
            if name.casefold() == folded:
                return index
        return -1

    def _build_profile_packet(self, opcode: int, target: str) -> bytes:
        if not self.accounts.exists(target):
            target = ""
        payload = bytearray(0x200 - HEADER_BYTES)
        if not target:
            return build_packet(opcode, payload)

        def absolute(offset: int) -> int:
            return offset - HEADER_BYTES

        write_fixed_text(payload, absolute(0x0D), 0x20, target)
        write_i32(payload, absolute(0x2D), -1)
        write_fixed_text(payload, absolute(0x31), 0x20,
            str(self.accounts.profile_value(target, "guild_name", "")))
        write_i32(payload, absolute(0x51),
            int(self.accounts.profile_value(target, "birth_year", 0)))
        write_i32(payload, absolute(0x55),
            int(self.accounts.profile_value(target, "sex", -1)))
        write_i32(payload, absolute(0x59),
            int(self.accounts.profile_value(target, "location", -1)))
        description = self.accounts.profile_value(
            target,
            "description",
            self.accounts.profile_value(target, "intro", ""),
        )
        write_fixed_text(payload, absolute(0x5D), 0x100, str(description))

        normal = self.accounts.statistics(target, "normal")
        ranked = self.accounts.statistics(target, "rank")
        for index, key in enumerate(("wins", "losses", "draws")):
            write_i32(payload, absolute(0x15F + index * 4), normal[key])
            write_i32(payload, absolute(0x173 + index * 4), ranked[key])
            write_i32(payload, absolute(0x187 + index * 4), 0)
            write_i32(payload, absolute(0x19B + index * 4), 0)
        write_i32(payload, absolute(0x16B), self._rank_position(target))
        write_i32(payload, absolute(0x16F), self.accounts.rank_points(target))
        write_i32(payload, absolute(0x193), -1)
        write_i32(payload, absolute(0x197), 0)
        for index in range(8):
            write_i32(payload, absolute(0x1A7 + index * 4), -1)
            write_i32(payload, absolute(0x1C7 + index * 4), 0)
        return build_packet(opcode, payload)

    async def _handle_rank_list(
        self, session: ClientSession, packet: Packet
    ) -> None:
        top = read_u32(packet.raw, 0x0D)
        names = self._ranked_accounts()
        selected = names[top : top + 16]
        payload = bytearray(4 + 16 * 0x38)
        write_u32(payload, 0, top)
        for index, name in enumerate(selected):
            offset = 4 + index * 0x38
            statistics = self.accounts.statistics(name, "rank")
            write_fixed_text(payload, offset, 0x20, name)
            write_u32(payload, offset + 0x20, self.accounts.rank_points(name))
            write_fixed_text(
                payload,
                offset + 0x24,
                0x10,
                f"{statistics['wins']}-{statistics['losses']}-{statistics['draws']}",
            )
            write_u32(payload, offset + 0x34, self.accounts.rank_points(name))
        await self._send(session, build_packet(0x30, payload))

    def _public_sockaddr(self, session: ClientSession, source: bytes) -> bytes:
        if len(source) < 16:
            return b"\0" * 16
        result = bytearray(source[:16])
        try:
            source_ip = ipaddress.ip_address(socket.inet_ntoa(result[4:8]))
            peer_ip = ipaddress.ip_address(session.peer_host)
        except ValueError:
            return bytes(result)
        if not self.config.advertise_peer_address or source_ip.is_global:
            return bytes(result)

        replacement_ip = peer_ip
        if (peer_ip.is_loopback or peer_ip.is_private or peer_ip.is_unspecified) and \
                self.config.public_address:
            replacement_ip = ipaddress.ip_address(self.config.public_address)
        if not replacement_ip.is_unspecified:
            result[4:8] = socket.inet_aton(str(replacement_ip))
        return bytes(result)

    async def _handle_host_game(self, session: ClientSession, packet: Packet) -> None:
        session.view = "create"
        name = read_c_string(packet.raw, 0x0D, 0x80).strip()
        password = read_c_string(packet.raw, 0x8D, 0x80)
        duplicate = next(
            (
                game
                for game in self.state.lobby_games(session.lobby_id)
                if game.host_client_id != session.client_id
                and game.name.casefold() == name.casefold()
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
        game.relay_members[session.client_id] = 1
        game.relay_member_peers[1] = (session.peer_host, session.peer_port)
        game.participant_accounts.add(session.account)
        self.state.games[game_id] = game
        session.hosted_game_id = game_id
        session.relay_game_id = game_id
        session.relay_member_id = 1
        session.view = "link"
        session.presence_status = PRESENCE_STATUS_HOSTING
        session.presence_location = game.name
        await self._send(
            session,
            build_packet(0x1A, struct.pack("<III", 1, game_id, 1) + game.relay_secret),
        )
        LOGGER.info("%s advertised game %s (%d)", session.account, name, game_id)
        await self._broadcast_game_update(game, added=True, exclude=session.client_id)
        await self._broadcast_online_presence(
            session, added=True, exclude=session.client_id
        )

    async def _handle_game_list(self, session: ClientSession, packet: Packet) -> None:
        session.view = "free_server"
        start_index = read_u32(packet.raw, 0x0D)
        games = self.state.lobby_games(session.lobby_id)
        if start_index < len(games):
            response = self._build_game_record_packet(0x1E, games[start_index], start_index)
            LOGGER.debug(
                "client %d game-list response opcode=0x1e game=%s id=%d index=%d total=%d bytes=%d",
                session.client_id,
                games[start_index].name,
                games[start_index].game_id,
                start_index,
                len(games),
                len(response),
            )
        else:
            response = self._build_empty_game_record_packet(0x1E)
            LOGGER.debug(
                "client %d game-list response opcode=0x1e empty index=%d total=%d bytes=%d",
                session.client_id,
                start_index,
                len(games),
                len(response),
            )
        await self._send(session, response)

    async def _handle_remove_game(self, session: ClientSession, packet: Packet) -> None:
        game_id = read_u32(packet.raw, 0x0D)
        game = self.state.games.get(game_id)
        if game is None or game.host_client_id != session.client_id:
            LOGGER.warning(
                "client %d (%s) attempted to remove unowned game %d",
                session.client_id,
                session.account,
                game_id,
            )
            return
        if len(game.relay_members) > 1:
            await self._retire_game_advertisement(game)
            return
        await self._remove_game(game_id)

    async def _handle_start_game(self, session: ClientSession, packet: Packet) -> None:
        del packet
        if session.hosted_game_id is None:
            return
        game = self.state.games.get(session.hosted_game_id)
        if game is None or game.host_client_id != session.client_id:
            return
        started_participants_ordered = tuple(
            member.account
            for client_id, _member_id in sorted(
                game.relay_members.items(), key=lambda item: item[1]
            )
            if (member := self.state.clients.get(client_id)) is not None
            and member.authenticated
        )
        started_participants = set(started_participants_ordered)
        if len(started_participants) < 2:
            LOGGER.warning(
                "client %d (%s) attempted to start game %d with fewer than two participants",
                session.client_id,
                session.account,
                game.game_id,
            )
            return
        game.participant_accounts = started_participants
        game.participant_accounts_ordered = started_participants_ordered
        game.started = True
        for client_id in game.relay_members:
            member = self.state.clients.get(client_id)
            if member is None:
                continue
            member.presence_status = PRESENCE_STATUS_PLAYING
            member.presence_location = game.name
            await self._broadcast_online_presence(
                member, added=True, exclude=member.client_id
            )
        await self._retire_game_advertisement(game)

    async def _handle_relay_join(self, session: ClientSession, packet: Packet) -> None:
        game_id = read_u32(packet.raw, 0x0D)
        game = self.state.games.get(game_id)
        if game is None:
            LOGGER.info(
                "relay join rejected no-game client=%d account=%s game=%d",
                session.client_id,
                session.account,
                game_id,
            )
            await self._send_relay_join_status(session, RELAY_STATUS_NO_GAME, game_id, 0)
            return
        if not game.advertised and session.client_id not in game.relay_members:
            LOGGER.info(
                "relay join rejected hidden-room client=%d account=%s game=%d",
                session.client_id,
                session.account,
                game_id,
            )
            await self._send_relay_join_status(session, RELAY_STATUS_NO_GAME, game_id, 0)
            return
        host = self.state.clients.get(game.host_client_id)
        if host is None or host.writer.is_closing():
            LOGGER.info(
                "relay join rejected missing-host client=%d account=%s game=%d",
                session.client_id,
                session.account,
                game_id,
            )
            await self._send_relay_join_status(
                session, RELAY_STATUS_HOST_MISSING, game_id, 0
            )
            return
        if session.relay_game_id is not None and session.relay_game_id != game_id:
            await self._remove_relay_member(session)
        member_id = game.relay_members.get(session.client_id, 0)
        if member_id == 0:
            if len(game.relay_members) >= RELAY_MAX_MEMBERS:
                LOGGER.info(
                    "relay join rejected full client=%d account=%s game=%d members=%d",
                    session.client_id,
                    session.account,
                    game_id,
                    len(game.relay_members),
                )
                await self._send_relay_join_status(
                    session, RELAY_STATUS_FULL, game_id, 0
                )
                return
            member_id = self._allocate_relay_member_id(game)
            if member_id == 0:
                await self._send_relay_join_status(
                    session, RELAY_STATUS_FULL, game_id, 0
                )
                return
            game.relay_members[session.client_id] = member_id
        game.relay_member_peers[member_id] = (session.peer_host, session.peer_port)
        game.participant_accounts.add(session.account)
        game.relay_departed_members.discard(member_id)
        session.relay_game_id = game_id
        session.relay_member_id = member_id
        session.view = "link"
        session.presence_status = PRESENCE_STATUS_ROOM_MEMBER
        session.presence_location = game.name
        await self._send_relay_join_status(
            session, RELAY_STATUS_OK, game_id, member_id, game.relay_secret
        )
        LOGGER.info(
            "relay join accepted game=%d client=%d account=%s member=%d members=%d",
            game_id,
            session.client_id,
            session.account,
            member_id,
            len(game.relay_members),
        )
        await self._broadcast_online_presence(
            session, added=True, exclude=session.client_id
        )
        if len(packet.raw) > 0x11:
            join_payload = packet.raw[0x11:]
            if not self._relay_payload_has_cipher_wrapper(join_payload):
                game.relay_invalid_payload_frames += 1
                LOGGER.debug(
                    "relay join ignored invalid-payload client=%d account=%s game=%d bytes=%d",
                    session.client_id,
                    session.account,
                    game_id,
                    len(join_payload),
                )
                return
            self._record_relay_frame_stats(
                game,
                member_id,
                RELAY_STREAM_LINK,
                len(join_payload),
                [host],
            )
            await self._send_relay_frame(
                host, game_id, member_id, RELAY_STREAM_LINK, join_payload
            )

    async def _handle_relay_leave(self, session: ClientSession, packet: Packet) -> None:
        requested_game_id = read_u32(packet.raw, 0x0D) if len(packet.raw) >= 0x11 else 0
        if (
            requested_game_id != 0
            and session.relay_game_id is not None
            and requested_game_id != session.relay_game_id
        ):
            LOGGER.debug(
                "client %d ignored stale relay leave for game %d while in game %d",
                session.client_id,
                requested_game_id,
                session.relay_game_id,
            )
            return
        await self._remove_relay_member(session)

    async def _handle_relay_frame(self, session: ClientSession, packet: Packet) -> None:
        if len(packet.raw) < 0x19:
            return
        game_id = read_u32(packet.raw, 0x0D)
        target_member_id = read_u32(packet.raw, 0x11)
        stream_id = read_u32(packet.raw, 0x15)
        data = packet.raw[0x19:]
        game = self.state.games.get(game_id)
        if (
            game is None
            or session.relay_game_id != game_id
            or game.relay_members.get(session.client_id) != session.relay_member_id
            or session.relay_member_id == 0
        ):
            await self._send_relay_join_status(
                session, RELAY_STATUS_NOT_MEMBER, game_id, 0
            )
            return
        if not data:
            return
        if stream_id not in (RELAY_STREAM_LINK, RELAY_STREAM_MODE1):
            game.relay_invalid_stream_frames += 1
            LOGGER.debug(
                "relay frame ignored invalid-stream client=%d account=%s game=%d stream=%d",
                session.client_id,
                session.account,
                game_id,
                stream_id,
            )
            return
        if not self._relay_payload_has_cipher_wrapper(data):
            game.relay_invalid_payload_frames += 1
            LOGGER.debug(
                "relay frame ignored invalid-payload client=%d account=%s game=%d stream=%d bytes=%d",
                session.client_id,
                session.account,
                game_id,
                stream_id,
                len(data),
            )
            return
        targets = self._relay_targets(game, session.client_id, target_member_id)
        if not targets:
            if target_member_id == 0 or target_member_id in game.relay_departed_members:
                LOGGER.debug(
                    "relay frame ignored stale target game=%d from_member=%d target_member=%d stream=%d bytes=%d",
                    game_id,
                    session.relay_member_id,
                    target_member_id,
                    stream_id,
                    len(data),
                )
                return
            game.relay_no_target_frames += 1
            LOGGER.debug(
                "relay frame had no targets game=%d from_member=%d target_member=%d stream=%d bytes=%d",
                game_id,
                session.relay_member_id,
                target_member_id,
                stream_id,
                len(data),
            )
            return
        self._record_relay_frame_stats(
            game, session.relay_member_id, stream_id, len(data), targets
        )
        LOGGER.debug(
            "relay frame game=%d from_member=%d target_member=%d stream=%d bytes=%d targets=%d",
            game_id,
            session.relay_member_id,
            target_member_id,
            stream_id,
            len(data),
            len(targets),
        )
        await self._broadcast(
            targets,
            build_packet(
                RELAY_FRAME_OPCODE,
                struct.pack("<III", game_id, session.relay_member_id, stream_id) + data,
            ),
        )

    async def _send_relay_join_status(
        self,
        session: ClientSession,
        status: int,
        game_id: int,
        member_id: int,
        relay_secret: bytes | None = None,
    ) -> None:
        payload = struct.pack("<III", status, game_id, member_id)
        if status == RELAY_STATUS_OK and relay_secret is not None:
            payload += relay_secret
        await self._send(
            session,
            build_packet(RELAY_JOIN_STATUS_OPCODE, payload),
        )

    async def _send_relay_frame(
        self,
        session: ClientSession,
        game_id: int,
        from_member_id: int,
        stream_id: int,
        data: bytes,
    ) -> None:
        if not data:
            return
        await self._send(
            session,
            build_packet(
                RELAY_FRAME_OPCODE,
                struct.pack("<III", game_id, from_member_id, stream_id) + data,
            ),
        )

    @staticmethod
    def _relay_payload_has_cipher_wrapper(data: bytes) -> bool:
        if len(data) < RELAY_CIPHER_HEADER_BYTES:
            return False
        if data[:4] != RELAY_CIPHER_MAGIC:
            return False
        plain_bytes = struct.unpack_from("<I", data, 4)[0]
        return plain_bytes > 0 and plain_bytes == len(data) - RELAY_CIPHER_HEADER_BYTES

    def _relay_targets(
        self,
        game: AdvertisedGame,
        source_client_id: int,
        target_member_id: int,
    ) -> list[ClientSession]:
        targets: list[ClientSession] = []
        for client_id, member_id in game.relay_members.items():
            if client_id == source_client_id:
                continue
            if target_member_id != 0 and member_id != target_member_id:
                continue
            session = self.state.clients.get(client_id)
            if session is not None and not session.writer.is_closing():
                targets.append(session)
        return targets

    def _allocate_relay_member_id(self, game: AdvertisedGame) -> int:
        used_member_ids = set(game.relay_members.values())
        for member_id in range(2, RELAY_MAX_MEMBERS + 1):
            if member_id not in used_member_ids:
                return member_id
        return 0

    def _record_relay_frame_stats(
        self,
        game: AdvertisedGame,
        from_member_id: int,
        stream_id: int,
        byte_count: int,
        targets: list[ClientSession],
    ) -> None:
        if stream_id == RELAY_STREAM_LINK:
            game.relay_link_frames += 1
            game.relay_link_bytes += byte_count
        elif stream_id == RELAY_STREAM_MODE1:
            game.relay_mode1_frames += 1
            game.relay_mode1_bytes += byte_count
        else:
            return
        game.relay_deliveries += len(targets)
        self._record_relay_member_stats(game, from_member_id, stream_id, targets)

    @staticmethod
    def _record_relay_member_stats(
        game: AdvertisedGame,
        from_member_id: int,
        stream_id: int,
        targets: list[ClientSession],
    ) -> None:
        if stream_id == RELAY_STREAM_LINK:
            tx_counts = game.relay_member_link_tx
            rx_counts = game.relay_member_link_rx
        elif stream_id == RELAY_STREAM_MODE1:
            tx_counts = game.relay_member_mode1_tx
            rx_counts = game.relay_member_mode1_rx
        else:
            return
        tx_counts[from_member_id] = tx_counts.get(from_member_id, 0) + 1
        for target in targets:
            target_member_id = game.relay_members.get(target.client_id, 0)
            if target_member_id != 0:
                rx_counts[target_member_id] = rx_counts.get(target_member_id, 0) + 1

    def _relay_member_peer_summary(
        self, game: AdvertisedGame, relay_members: list[tuple[int, int]]
    ) -> tuple[int, int, str]:
        peer_records = self._relay_member_peer_records(game, relay_members)
        peer_hosts = {
            str(peer["host"])
            for peer in peer_records.values()
            if peer.get("host") != "disconnected"
        }
        peer_parts = [
            f"{member_id}@{peer['host']}:{peer['port']}"
            for member_id, peer in sorted(peer_records.items())
        ]
        return len(peer_records), len(peer_hosts), ",".join(peer_parts)

    def _relay_member_peer_records(
        self, game: AdvertisedGame, relay_members: list[tuple[int, int]]
    ) -> dict[int, dict[str, object]]:
        peers_by_member = dict(game.relay_member_peers)
        for client_id, member_id in relay_members:
            member = self.state.clients.get(client_id)
            if member is not None:
                peers_by_member[member_id] = (member.peer_host, member.peer_port)
        result: dict[int, dict[str, object]] = {}
        for member_id, (peer_host, peer_port) in peers_by_member.items():
            result[member_id] = {
                "host": peer_host,
                "port": peer_port,
                "endpoint": f"{peer_host}:{peer_port}",
            }
        return result

    @staticmethod
    def _relay_member_frame_records(game: AdvertisedGame) -> dict[int, dict[str, int]]:
        member_ids = set(game.relay_member_peers)
        member_ids.update(game.relay_members.values())
        member_ids.update(game.relay_member_link_tx)
        member_ids.update(game.relay_member_link_rx)
        member_ids.update(game.relay_member_mode1_tx)
        member_ids.update(game.relay_member_mode1_rx)
        return {
            member_id: {
                "link_tx": game.relay_member_link_tx.get(member_id, 0),
                "link_rx": game.relay_member_link_rx.get(member_id, 0),
                "mode1_tx": game.relay_member_mode1_tx.get(member_id, 0),
                "mode1_rx": game.relay_member_mode1_rx.get(member_id, 0),
            }
            for member_id in sorted(member_ids)
        }

    @staticmethod
    def _relay_member_peer_summary_from_records(
        peer_records: dict[int, dict[str, object]]
    ) -> str:
        return ",".join(
            f"{member_id}@{peer['host']}:{peer['port']}"
            for member_id, peer in sorted(peer_records.items())
        )

    @staticmethod
    def _relay_member_frame_summary(game: AdvertisedGame) -> str:
        return RankerServer._relay_member_frame_summary_from_records(
            RankerServer._relay_member_frame_records(game)
        )

    @staticmethod
    def _relay_member_frame_summary_from_records(
        member_frames: dict[int, dict[str, int]]
    ) -> str:
        parts: list[str] = []
        for member_id, counters in member_frames.items():
            parts.append(
                f"{member_id}:"
                f"link_tx={counters['link_tx']}:"
                f"link_rx={counters['link_rx']}:"
                f"mode1_tx={counters['mode1_tx']}:"
                f"mode1_rx={counters['mode1_rx']}"
            )
        return ";".join(parts)

    def _build_relay_summary_record(
        self, game: AdvertisedGame, relay_members: list[tuple[int, int]]
    ) -> dict[str, object]:
        peer_records = self._relay_member_peer_records(game, relay_members)
        peer_hosts = {
            str(peer["host"])
            for peer in peer_records.values()
            if peer.get("host") != "disconnected"
        }
        member_peers = self._relay_member_peer_summary_from_records(peer_records)
        member_frames = self._relay_member_frame_records(game)
        member_frames_text = self._relay_member_frame_summary_from_records(
            member_frames
        )
        distinct_peer_endpoints = len(
            {
                str(peer.get("endpoint", ""))
                for peer in peer_records.values()
                if peer.get("endpoint")
            }
        )
        bidirectional_mode1_members = sum(
            1
            for counters in member_frames.values()
            if counters.get("mode1_tx", 0) > 0
            and counters.get("mode1_rx", 0) > 0
        )
        line = (
            f"relay summary game={game.game_id} name={game.name} "
            f"link_frames={game.relay_link_frames} "
            f"link_bytes={game.relay_link_bytes} "
            f"mode1_frames={game.relay_mode1_frames} "
            f"mode1_bytes={game.relay_mode1_bytes} "
            f"deliveries={game.relay_deliveries} "
            f"no_target={game.relay_no_target_frames} "
            f"invalid_stream={game.relay_invalid_stream_frames} "
            f"invalid_payload={game.relay_invalid_payload_frames} "
            f"members={len(peer_records)} "
            f"distinct_peer_hosts={len(peer_hosts)} "
            f"member_peers={member_peers} "
            f"member_frames={member_frames_text}"
        )
        return {
            "line_number": 1,
            "line": line,
            "room": game.name,
            "game_id": game.game_id,
            "link_frames": game.relay_link_frames,
            "link_bytes": game.relay_link_bytes,
            "mode1_frames": game.relay_mode1_frames,
            "mode1_bytes": game.relay_mode1_bytes,
            "deliveries": game.relay_deliveries,
            "no_target": game.relay_no_target_frames,
            "invalid_stream": game.relay_invalid_stream_frames,
            "invalid_payload": game.relay_invalid_payload_frames,
            "members": len(peer_records),
            "distinct_peer_hosts": len(peer_hosts),
            "member_peers": member_peers,
            "parsed_member_peers": peer_records,
            "distinct_peer_endpoints": distinct_peer_endpoints,
            "member_frames": member_frames,
            "bidirectional_mode1_members": bidirectional_mode1_members,
        }

    @staticmethod
    def _build_relay_evidence_summary(
        summary: dict[str, object]
    ) -> dict[str, object]:
        criteria = {
            "min_link_frames": 1,
            "min_mode1_frames": 1,
            "min_deliveries": 1,
            "min_members": 2,
            "min_distinct_peer_hosts": 0,
            "min_distinct_peer_endpoints": 2,
            "min_bidirectional_mode1_members": 2,
            "max_no_target": 0,
            "max_invalid_stream": 0,
            "max_invalid_payload": 0,
        }
        checks = {
            "summary_found": True,
            "link_frames_min": int(summary["link_frames"]) >= 1,
            "mode1_frames_min": int(summary["mode1_frames"]) >= 1,
            "deliveries_min": int(summary["deliveries"]) >= 1,
            "no_target_max": int(summary["no_target"]) <= 0,
            "invalid_stream_max": int(summary["invalid_stream"]) <= 0,
            "invalid_payload_max": int(summary["invalid_payload"]) <= 0,
            "members_min": int(summary["members"]) >= 2,
            "distinct_peer_hosts_min": True,
            "distinct_peer_endpoints_min": int(summary["distinct_peer_endpoints"])
            >= 2,
            "bidirectional_mode1_members_min": int(
                summary["bidirectional_mode1_members"]
            )
            >= 2,
        }
        missing = [name for name, ok in checks.items() if not ok]
        return {
            "ok": not missing,
            "log": None,
            "tail_bytes": 0,
            "room": summary["room"],
            "game_id": summary["game_id"],
            "criteria": criteria,
            "checks": checks,
            "missing_checks": missing,
            "summary": summary,
            "matched_summary_count": 1,
            "summary_lines": [summary["line"]],
        }

    @staticmethod
    def _safe_relay_evidence_name(text: str) -> str:
        safe = "".join(
            char
            if char.isascii() and (char.isalnum() or char in "._-")
            else "_"
            for char in text
        ).strip("._")
        return safe[:80] or "room"

    def _write_relay_evidence(self, summary: dict[str, object]) -> None:
        evidence_dir = self.config.relay_evidence_dir
        if evidence_dir is None:
            return
        exported_at = datetime.now(timezone.utc)
        server_summary = self._build_relay_evidence_summary(summary)
        evidence = {
            "ok": server_summary["ok"],
            "exported_utc": exported_at.isoformat(),
            "source": "server_auto_export",
            "source_log": None,
            "room": summary["room"],
            "game_id": summary["game_id"],
            "server_summary": server_summary,
        }
        room_name = self._safe_relay_evidence_name(str(summary["room"]))
        timestamp = exported_at.strftime("%Y%m%dT%H%M%S_%fZ")
        output_dir = Path(evidence_dir)
        output_path = (
            output_dir
            / f"relay_{timestamp}_game{summary['game_id']}_{room_name}.json"
        )
        temporary_path = output_path.with_suffix(output_path.suffix + ".tmp")
        try:
            output_dir.mkdir(parents=True, exist_ok=True)
            temporary_path.write_text(
                json.dumps(evidence, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            temporary_path.replace(output_path)
            LOGGER.info(
                "relay evidence exported game=%d name=%s ok=%s path=%s",
                summary["game_id"],
                summary["room"],
                server_summary["ok"],
                output_path,
            )
        except Exception:
            LOGGER.exception(
                "failed to write relay evidence game=%s name=%s path=%s",
                summary.get("game_id"),
                summary.get("room"),
                output_path,
            )


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
        packet = (
            self._build_online_presence_packet(subject)
            if added
            else build_packet(0x23, struct.pack("<I", subject.client_id))
        )
        targets = [
            client
            for client in self.state.lobby_clients(target_lobby)
            if client.view == "online"
        ]
        await self._broadcast(targets, packet, exclude=exclude)

    @staticmethod
    def _build_online_presence_packet(subject: ClientSession) -> bytes:
        payload = bytearray(0x85 - HEADER_BYTES)
        write_fixed_text(payload, 0, 0x20, subject.account)
        write_u32(payload, 0x4D - HEADER_BYTES, subject.client_id)
        write_u32(payload, 0x59 - HEADER_BYTES, subject.lobby_mark)
        write_u32(payload, 0x61 - HEADER_BYTES, subject.presence_status)
        write_fixed_text(
            payload,
            0x65 - HEADER_BYTES,
            0x20,
            subject.presence_location,
        )
        return build_packet(7, payload)

    async def _send_online_presence_snapshot(self, session: ClientSession) -> None:
        for member in self.state.lobby_clients(session.lobby_id):
            if member.authenticated:
                await self._send(session, self._build_online_presence_packet(member))

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
        if game.started:
            host = self.state.clients.get(game.host_client_id)
            host_account = host.account if host is not None else ""
            self.state.completed_games[game.relay_secret[:MATCH_TOKEN_BYTES]] = (
                CompletedGame(
                    game_id=game.game_id,
                    game_type=game.game_type,
                    match_token=game.relay_secret[:MATCH_TOKEN_BYTES],
                    host_account=host_account,
                    participant_accounts=set(game.participant_accounts),
                    participant_accounts_ordered=(
                        game.participant_accounts_ordered or tuple(
                            sorted(game.participant_accounts, key=str.casefold)
                        )
                    ),
                    map_name=self._replay_map_name(game.map_descriptor),
                )
            )
        relay_members = list(game.relay_members.items())
        for client_id, member_id in relay_members:
            member = self.state.clients.get(client_id)
            if member is not None:
                member.relay_game_id = None
                member.relay_member_id = 0
        await self._broadcast(
            (
                self.state.clients[client_id]
                for client_id, _ in relay_members
                if client_id in self.state.clients and client_id != game.host_client_id
            ),
            build_packet(RELAY_MEMBER_LEFT_OPCODE, struct.pack("<II", game_id, 1)),
        )
        host = self.state.clients.get(game.host_client_id)
        if host is not None and host.hosted_game_id == game_id:
            host.hosted_game_id = None
        if game.advertised:
            await self._broadcast_game_update(game, added=False)
        relay_summary = self._build_relay_summary_record(game, relay_members)
        LOGGER.info("%s", relay_summary["line"])
        self._write_relay_evidence(relay_summary)
        LOGGER.info("removed game %s (%d)", game.name, game.game_id)

    async def _retire_game_advertisement(self, game: AdvertisedGame) -> None:
        if not game.advertised:
            return
        game.advertised = False
        await self._broadcast_game_update(game, added=False)
        LOGGER.info(
            "retired advertised game %s (%d) while preserving %d relay members",
            game.name,
            game.game_id,
            len(game.relay_members),
        )

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
        elif session.relay_game_id is not None:
            await self._remove_relay_member(session)
        self.state.clients.pop(session.client_id, None)
        self._discard_replay_upload(session.client_id)
        session.writer.close()
        with contextlib.suppress(Exception):
            await session.writer.wait_closed()
        LOGGER.info("client %d disconnected", session.client_id)

    async def _cleanup_expired_games(self) -> None:
        while True:
            await asyncio.sleep(30)
            await self._cleanup_expired_games_once()

    async def _cleanup_expired_games_once(self) -> None:
        cutoff = time.monotonic() - self.config.room_ttl_seconds
        expired = [
            game
            for game in self.state.games.values()
            if game.created_at < cutoff and game.advertised
        ]
        for game in expired:
            if len(game.relay_members) > 1:
                await self._retire_game_advertisement(game)
            else:
                await self._remove_game(game.game_id)
        completed_cutoff = time.monotonic() - self.config.room_ttl_seconds
        self.state.completed_games = {
            token: game
            for token, game in self.state.completed_games.items()
            if game.completed_at >= completed_cutoff
        }

    async def _remove_relay_member(self, session: ClientSession) -> None:
        game_id = session.relay_game_id
        member_id = session.relay_member_id
        session.relay_game_id = None
        session.relay_member_id = 0
        if game_id is None or member_id == 0:
            return
        game = self.state.games.get(game_id)
        if game is None:
            return
        if game.host_client_id == session.client_id:
            await self._remove_game(game_id)
            return
        if game.relay_members.pop(session.client_id, None) is None:
            return
        game.relay_departed_members.add(member_id)
        LOGGER.info(
            "relay member left game=%d client=%d account=%s member=%d members=%d",
            game_id,
            session.client_id,
            session.account,
            member_id,
            len(game.relay_members),
        )
        await self._broadcast(
            self._relay_targets(game, session.client_id, 0),
            build_packet(RELAY_MEMBER_LEFT_OPCODE, struct.pack("<II", game_id, member_id)),
        )
