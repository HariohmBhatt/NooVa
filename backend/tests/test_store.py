from pathlib import Path
import sqlite3

from app.store import HubStore


def test_registration_reuses_the_device_identity_for_an_installation(tmp_path: Path) -> None:
    store = HubStore(tmp_path / "hub.db")
    store.initialize()

    device_id = store.register_device("nonce-0123456789", "0.1.0", "hall")
    refreshed = store.register_device("nonce-0123456789", "0.2.0", "hall")
    devices = store.devices()

    assert refreshed == device_id
    assert len(devices) == 1
    assert devices[0]["device_id"] == device_id
    assert devices[0]["firmware"] == "0.2.0"
    assert devices[0]["last_seen_at"] is not None


def test_migration_removes_legacy_authentication_columns(tmp_path: Path) -> None:
    database = tmp_path / "hub.db"
    with sqlite3.connect(database) as connection:
        connection.executescript(
            """
            CREATE TABLE schema_migrations (
                version INTEGER PRIMARY KEY,
                applied_at TEXT NOT NULL
            );
            INSERT INTO schema_migrations VALUES (1, '2026-08-08T00:00:00Z');
            INSERT INTO schema_migrations VALUES (2, '2026-08-08T00:00:00Z');
            CREATE TABLE devices (
                device_id TEXT PRIMARY KEY,
                display_name TEXT NOT NULL,
                token_hash TEXT NOT NULL,
                installation_nonce TEXT NOT NULL,
                firmware TEXT NOT NULL,
                created_at TEXT NOT NULL,
                revoked_at TEXT,
                last_seen_at TEXT,
                session_secret_hash TEXT,
                approved_at TEXT
            );
            INSERT INTO devices VALUES (
                'device-1', 'hall', '', 'nonce-0123456789', '0.1.0',
                '2026-08-08T00:00:00Z', NULL, NULL, 'legacy-secret', NULL
            );
            CREATE TABLE audit_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                event_type TEXT NOT NULL,
                device_id TEXT,
                created_at TEXT NOT NULL,
                details_json TEXT NOT NULL
            );
            """
        )

    store = HubStore(database)
    store.initialize()

    with sqlite3.connect(database) as connection:
        columns = {row[1] for row in connection.execute("PRAGMA table_info(devices)")}

    assert columns == {
        "device_id",
        "display_name",
        "installation_nonce",
        "firmware",
        "created_at",
        "last_seen_at",
    }
    assert store.devices()[0]["device_id"] == "device-1"
