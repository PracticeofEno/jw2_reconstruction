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

    def account_names(self) -> list[str]:
        return [
            str(record.get("account", key))
            for key, record in self._records.items()
        ]

    @staticmethod
    def _empty_statistics() -> dict[str, int]:
        return {"wins": 0, "losses": 0, "draws": 0}

    def statistics(self, account: str, bucket: str) -> dict[str, int]:
        if bucket not in ("normal", "rank"):
            raise ValueError("unknown statistics bucket")
        record = self._records.get(account.casefold())
        if record is None:
            return self._empty_statistics()
        profile = record.get("profile")
        if not isinstance(profile, dict):
            return self._empty_statistics()
        raw = profile.get(f"{bucket}_statistics")
        if not isinstance(raw, dict):
            return self._empty_statistics()
        return {
            key: max(0, int(raw.get(key, 0)))
            for key in ("wins", "losses", "draws")
        }

    def record_match(
        self, account: str, bucket: str, outcome: str, match_token: str
    ) -> bool:
        if bucket not in ("normal", "rank"):
            raise ValueError("unknown statistics bucket")
        if outcome not in ("wins", "losses", "draws"):
            raise ValueError("unknown match outcome")
        record = self._records.get(account.casefold())
        if record is None or not match_token:
            return False

        reported = record.get("reported_matches")
        if not isinstance(reported, list):
            reported = []
        if match_token in reported:
            return False

        profile = record.get("profile")
        if not isinstance(profile, dict):
            profile = {}
            record["profile"] = profile
        statistics = self.statistics(account, bucket)
        statistics[outcome] += 1
        profile[f"{bucket}_statistics"] = statistics

        # Room secrets are random across server restarts. Keeping a bounded
        # history makes a client retry idempotent without growing accounts.json
        # forever.
        reported.append(match_token)
        record["reported_matches"] = reported[-256:]
        self.save()
        return True

    def rank_points(self, account: str) -> int:
        statistics = self.statistics(account, "rank")
        return statistics["wins"] * 3 + statistics["draws"]

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
