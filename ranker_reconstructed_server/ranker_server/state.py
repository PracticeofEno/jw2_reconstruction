"""In-memory accounts, lobby channels, clients, and advertised games."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
import secrets
import time


@dataclass(slots=True)
class ClientSession:
    client_id: int
    reader: asyncio.StreamReader
    writer: asyncio.StreamWriter
    peer_host: str
    peer_port: int
    account: str = ""
    locale: str = ""
    lobby_id: int = 1
    view: str = "login"
    hosted_game_id: int | None = None
    relay_game_id: int | None = None
    relay_member_id: int = 0
    lobby_mark: int = 0
    connected_at: float = field(default_factory=time.monotonic)
    send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    @property
    def authenticated(self) -> bool:
        return bool(self.account)


@dataclass(slots=True)
class LobbyChannel:
    lobby_id: int
    name: str
    password: str = ""
    members: set[int] = field(default_factory=set)


@dataclass(slots=True)
class AdvertisedGame:
    game_id: int
    host_client_id: int
    lobby_id: int
    name: str
    password: str
    sockaddr: bytes
    game_type: int
    map_descriptor: bytes
    relay_members: dict[int, int] = field(default_factory=dict)
    relay_member_peers: dict[int, tuple[str, int]] = field(default_factory=dict)
    relay_departed_members: set[int] = field(default_factory=set)
    relay_secret: bytes = field(default_factory=lambda: secrets.token_bytes(32))
    advertised: bool = True
    created_at: float = field(default_factory=time.monotonic)
    relay_link_frames: int = 0
    relay_link_bytes: int = 0
    relay_mode1_frames: int = 0
    relay_mode1_bytes: int = 0
    relay_deliveries: int = 0
    relay_no_target_frames: int = 0
    relay_invalid_stream_frames: int = 0
    relay_invalid_payload_frames: int = 0
    relay_member_link_tx: dict[int, int] = field(default_factory=dict)
    relay_member_link_rx: dict[int, int] = field(default_factory=dict)
    relay_member_mode1_tx: dict[int, int] = field(default_factory=dict)
    relay_member_mode1_rx: dict[int, int] = field(default_factory=dict)


class ServerState:
    def __init__(self, default_lobby_name: str) -> None:
        self.clients: dict[int, ClientSession] = {}
        self.lobbies: dict[int, LobbyChannel] = {
            1: LobbyChannel(1, default_lobby_name)
        }
        self.games: dict[int, AdvertisedGame] = {}
        self.next_client_id = 1
        self.next_lobby_id = 2
        self.next_game_id = 1

    def allocate_client_id(self) -> int:
        value = self.next_client_id
        self.next_client_id += 1
        return value

    def allocate_lobby_id(self) -> int:
        value = self.next_lobby_id
        self.next_lobby_id += 1
        return value

    def allocate_game_id(self) -> int:
        value = self.next_game_id
        self.next_game_id += 1
        return value

    def find_lobby_by_name(self, name: str) -> LobbyChannel | None:
        folded = name.casefold()
        return next(
            (lobby for lobby in self.lobbies.values() if lobby.name.casefold() == folded),
            None,
        )

    def find_client_by_account(self, account: str) -> ClientSession | None:
        folded = account.casefold()
        return next(
            (
                client
                for client in self.clients.values()
                if client.authenticated and client.account.casefold() == folded
            ),
            None,
        )

    def lobby_clients(self, lobby_id: int) -> list[ClientSession]:
        lobby = self.lobbies.get(lobby_id)
        if lobby is None:
            return []
        return [
            self.clients[client_id]
            for client_id in sorted(lobby.members)
            if client_id in self.clients
        ]

    def lobby_games(self, lobby_id: int) -> list[AdvertisedGame]:
        return sorted(
            (
                game
                for game in self.games.values()
                if game.lobby_id == lobby_id and game.advertised
            ),
            key=lambda game: game.game_id,
        )
