"""Configuration loading for the reconstructed server."""

from __future__ import annotations

from dataclasses import dataclass
import ipaddress
import json
from pathlib import Path
from typing import Any


@dataclass(slots=True)
class ServerConfig:
    host: str = "0.0.0.0"
    port: int = 19777
    server_name: str = "Ranker Reconstructed WizardNet"
    default_lobby_name: str = "WizardNet"
    max_clients: int = 128
    max_lobby_members: int = 64
    auto_register_accounts: bool = True
    advertise_peer_address: bool = True
    public_address: str = ""
    room_ttl_seconds: int = 21600
    rank_game_count: int = 10
    log_level: str = "INFO"
    account_file: Path | None = None

    def validate(self) -> None:
        if not self.host:
            raise ValueError("listen.host must not be empty")
        if not 0 <= self.port <= 65535:
            raise ValueError("listen.port must be between 0 and 65535")
        if not 1 <= self.max_clients <= 10000:
            raise ValueError("server.max_clients is outside the supported range")
        if not 1 <= self.max_lobby_members <= self.max_clients:
            raise ValueError("server.max_lobby_members is outside the supported range")
        if self.room_ttl_seconds < 60:
            raise ValueError("server.room_ttl_seconds must be at least 60")
        if self.rank_game_count < 0:
            raise ValueError("server.rank_game_count must not be negative")
        if self.public_address:
            try:
                public_address = ipaddress.ip_address(self.public_address)
            except ValueError as error:
                raise ValueError(
                    "server.public_address must be a valid IPv4 address"
                ) from error
            if public_address.version != 4:
                raise ValueError(
                    "server.public_address must be a valid IPv4 address"
                )


def _section(root: dict[str, Any], name: str) -> dict[str, Any]:
    value = root.get(name, {})
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be a JSON object")
    return value


def load_config(path: str | Path | None = None) -> ServerConfig:
    config = ServerConfig()
    if path is None:
        config.validate()
        return config

    config_path = Path(path)
    with config_path.open("r", encoding="utf-8") as stream:
        root = json.load(stream)
    if not isinstance(root, dict):
        raise ValueError("the configuration root must be a JSON object")

    listen = _section(root, "listen")
    server = _section(root, "server")
    logging = _section(root, "logging")
    data = _section(root, "data")

    config.host = str(listen.get("host", config.host))
    config.port = int(listen.get("port", config.port))
    config.server_name = str(server.get("name", config.server_name))
    config.default_lobby_name = str(
        server.get("default_lobby", config.default_lobby_name)
    )
    config.max_clients = int(server.get("max_clients", config.max_clients))
    config.max_lobby_members = int(
        server.get("max_lobby_members", config.max_lobby_members)
    )
    config.auto_register_accounts = bool(
        server.get("auto_register_accounts", config.auto_register_accounts)
    )
    config.advertise_peer_address = bool(
        server.get("advertise_peer_address", config.advertise_peer_address)
    )
    config.public_address = str(
        server.get("public_address", config.public_address)
    ).strip()
    config.room_ttl_seconds = int(
        server.get("room_ttl_seconds", config.room_ttl_seconds)
    )
    config.rank_game_count = int(
        server.get("rank_game_count", config.rank_game_count)
    )
    config.log_level = str(logging.get("level", config.log_level)).upper()
    account_file = str(data.get("account_file", "")).strip()
    if account_file:
        candidate = Path(account_file)
        config.account_file = (
            candidate if candidate.is_absolute() else config_path.parent / candidate
        )
    config.validate()
    return config
