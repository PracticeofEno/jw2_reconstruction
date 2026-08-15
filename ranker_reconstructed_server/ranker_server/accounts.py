"""Persistent account records for the reconstructed WizardNet server."""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
from typing import Any


_HASH_ROUNDS = 120_000


class AccountStore:
    def __init__(self, path: Path | None = None) -> None:
        self.path = path
        self._records: dict[str, dict[str, Any]] = {}
        self.load()

    def load(self) -> None:
        self._records.clear()
        if self.path is None or not self.path.exists():
            return
        with self.path.open("r", encoding="utf-8") as stream:
            root = json.load(stream)
        records = root.get("accounts", {}) if isinstance(root, dict) else {}
        if isinstance(records, dict):
            self._records = {
                str(key): value
                for key, value in records.items()
                if isinstance(value, dict)
            }

    def exists(self, account: str) -> bool:
        return account.casefold() in self._records

    def verify(self, account: str, password: str) -> bool:
        record = self._records.get(account.casefold())
        if record is None:
            return False
        try:
            salt = base64.b64decode(str(record["salt"]), validate=True)
            expected = base64.b64decode(str(record["password_hash"]), validate=True)
        except (KeyError, ValueError):
            return False
        actual = hashlib.pbkdf2_hmac(
            "sha256", password.encode("utf-8"), salt, _HASH_ROUNDS
        )
        return hmac.compare_digest(actual, expected)

    def create(
        self, account: str, password: str, profile: dict[str, Any] | None = None
    ) -> bool:
        key = account.casefold()
        if key in self._records:
            return False
        salt = os.urandom(16)
        digest = hashlib.pbkdf2_hmac(
            "sha256", password.encode("utf-8"), salt, _HASH_ROUNDS
        )
        self._records[key] = {
            "account": account,
            "salt": base64.b64encode(salt).decode("ascii"),
            "password_hash": base64.b64encode(digest).decode("ascii"),
            "profile": profile or {},
        }
        self.save()
        return True

    def profile_value(self, account: str, key: str, default: Any = None) -> Any:
        record = self._records.get(account.casefold())
        if record is None:
            return default
        profile = record.get("profile")
        if not isinstance(profile, dict):
            return default
        return profile.get(key, default)

    def set_profile_value(self, account: str, key: str, value: Any) -> bool:
        record = self._records.get(account.casefold())
        if record is None:
            return False
        profile = record.get("profile")
        if not isinstance(profile, dict):
            profile = {}
            record["profile"] = profile
        profile[key] = value
        self.save()
        return True

    def save(self) -> None:
        if self.path is None:
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(self.path.suffix + ".tmp")
        with temporary.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(
                {"version": 1, "accounts": self._records},
                stream,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            stream.write("\n")
        os.replace(temporary, self.path)
