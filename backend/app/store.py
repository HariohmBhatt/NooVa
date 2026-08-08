"""SQLite persistence for automatically registered devices and audit events."""

from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import sqlite3
from typing import Any
from uuid import uuid4


_LATEST_SCHEMA_VERSION = 3


def _timestamp(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


class HubStore:
    """SQLite-backed device registration and audit repository."""

    def __init__(self, path: Path) -> None:
        """Store hub state in the SQLite database at `path`."""
        self.path = path

    def initialize(self) -> None:
        """Create or migrate the database and prune expired audit events."""
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as connection:
            connection.execute("PRAGMA journal_mode=WAL")
            connection.execute("BEGIN IMMEDIATE")
            self._apply_migrations(connection)
            self._prune_audit_events(connection)

    def register_device(
        self, installation_nonce: str, firmware: str, display_name: str
    ) -> str:
        """Create or refresh a device and return its stable audit identity."""
        now = datetime.now(timezone.utc)
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT device_id FROM devices WHERE installation_nonce = ?",
                (installation_nonce,),
            ).fetchone()
            if row is not None:
                device_id = row["device_id"]
                connection.execute(
                    "UPDATE devices SET display_name = ?, firmware = ?, "
                    "last_seen_at = ? WHERE device_id = ?",
                    (display_name, firmware, _timestamp(now), device_id),
                )
                self._audit(
                    connection,
                    "device.registration_refreshed",
                    device_id,
                    {"firmware": firmware},
                )
                return device_id

            device_id = str(uuid4())
            connection.execute(
                "INSERT INTO devices(device_id, display_name, installation_nonce, "
                "firmware, created_at) VALUES(?, ?, ?, ?, ?)",
                (
                    device_id,
                    display_name,
                    installation_nonce,
                    firmware,
                    _timestamp(now),
                ),
            )
            self._audit(connection, "device.registered", device_id, {"firmware": firmware})
            return device_id

    def mark_seen(self, device_id: str, installation_nonce: str) -> None:
        """Record activity for a matching registered installation."""
        with self._connect() as connection:
            connection.execute(
                "UPDATE devices SET last_seen_at = ? "
                "WHERE device_id = ? AND installation_nonce = ?",
                (_timestamp(datetime.now(timezone.utc)), device_id, installation_nonce),
            )

    def devices(self) -> list[dict[str, Any]]:
        """Return registered devices in creation order for operator diagnostics."""
        with self._connect() as connection:
            rows = connection.execute(
                "SELECT device_id, display_name, firmware, created_at, last_seen_at "
                "FROM devices ORDER BY created_at"
            ).fetchall()
        return [dict(row) for row in rows]

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=5)
        connection.row_factory = sqlite3.Row
        return connection

    def _apply_migrations(self, connection: sqlite3.Connection) -> None:
        connection.execute(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)"
        )
        applied = {
            row["version"]
            for row in connection.execute("SELECT version FROM schema_migrations")
        }
        migrations = {
            1: self._migration_one,
            2: self._migration_two,
            3: self._migration_three,
        }
        for version in range(1, _LATEST_SCHEMA_VERSION + 1):
            if version in applied:
                continue
            migrations[version](connection)
            connection.execute(
                "INSERT INTO schema_migrations(version, applied_at) VALUES(?, ?)",
                (version, _timestamp(datetime.now(timezone.utc))),
            )

    @staticmethod
    def _migration_one(connection: sqlite3.Connection) -> None:
        HubStore._create_registration_tables(connection)

    @staticmethod
    def _migration_two(connection: sqlite3.Connection) -> None:
        HubStore._remove_legacy_authentication_columns(connection)

    @staticmethod
    def _migration_three(connection: sqlite3.Connection) -> None:
        HubStore._remove_legacy_authentication_columns(connection)

    @staticmethod
    def _create_registration_tables(connection: sqlite3.Connection) -> None:
        connection.execute(
            "CREATE TABLE IF NOT EXISTS devices ("
            "device_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, "
            "installation_nonce TEXT NOT NULL UNIQUE, firmware TEXT NOT NULL, "
            "created_at TEXT NOT NULL, last_seen_at TEXT)"
        )
        connection.execute(
            "CREATE TABLE IF NOT EXISTS audit_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, event_type TEXT NOT NULL, "
            "device_id TEXT, created_at TEXT NOT NULL, details_json TEXT NOT NULL)"
        )

    @staticmethod
    def _remove_legacy_authentication_columns(connection: sqlite3.Connection) -> None:
        connection.execute("DROP TABLE IF EXISTS pairing_codes")
        columns = {
            row["name"] for row in connection.execute("PRAGMA table_info(devices)")
        }
        registration_columns = {
            "device_id",
            "display_name",
            "installation_nonce",
            "firmware",
            "created_at",
            "last_seen_at",
        }
        if columns == registration_columns:
            return
        if not registration_columns.issubset(columns):
            raise RuntimeError("devices table cannot be migrated")

        connection.execute(
            "CREATE TABLE devices_registration ("
            "device_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, "
            "installation_nonce TEXT NOT NULL UNIQUE, firmware TEXT NOT NULL, "
            "created_at TEXT NOT NULL, last_seen_at TEXT)"
        )
        connection.execute(
            "INSERT OR IGNORE INTO devices_registration "
            "SELECT device_id, display_name, installation_nonce, firmware, "
            "created_at, last_seen_at FROM devices "
            "ORDER BY COALESCE(last_seen_at, created_at) DESC"
        )
        connection.execute("DROP TABLE devices")
        connection.execute("ALTER TABLE devices_registration RENAME TO devices")

    @staticmethod
    def _prune_audit_events(connection: sqlite3.Connection) -> None:
        cutoff = _timestamp(datetime.now(timezone.utc) - timedelta(days=30))
        connection.execute("DELETE FROM audit_events WHERE created_at < ?", (cutoff,))

    @staticmethod
    def _audit(
        connection: sqlite3.Connection,
        event_type: str,
        device_id: str | None,
        details: dict[str, Any],
    ) -> None:
        connection.execute(
            "INSERT INTO audit_events(event_type, device_id, created_at, details_json) "
            "VALUES(?, ?, ?, ?)",
            (
                event_type,
                device_id,
                _timestamp(datetime.now(timezone.utc)),
                json.dumps(details, sort_keys=True),
            ),
        )
