from pathlib import Path
import sqlite3
from datetime import datetime, timedelta, timezone
import json

import pytest

from app.device_metrics import DeviceMetrics
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


def test_monitoring_migration_creates_persistent_tables(tmp_path: Path) -> None:
    database = tmp_path / "hub.db"
    store = HubStore(database)
    store.initialize()

    with sqlite3.connect(database) as connection:
        tables = {
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            )
        }
        version = connection.execute(
            "SELECT MAX(version) FROM schema_migrations"
        ).fetchone()[0]

    assert version == 4
    assert {
        "active_alerts",
        "host_metric_samples",
        "device_telemetry_samples",
    } <= tables


def test_alert_transition_updates_active_state_and_audit_atomically(
    tmp_path: Path,
) -> None:
    database = tmp_path / "hub.db"
    store = HubStore(database)
    store.initialize()
    warning_at = datetime(2026, 8, 15, 10, 0, tzinfo=timezone.utc)

    raised = store.apply_alert_transition(
        scope="host",
        metric="disk_used_percent",
        state="warning",
        value=86.0,
        details={"threshold": 85},
        occurred_at=warning_at,
    )
    repeated = store.apply_alert_transition(
        scope="host",
        metric="disk_used_percent",
        state="warning",
        value=87.0,
        occurred_at=warning_at + timedelta(seconds=5),
    )
    critical = store.apply_alert_transition(
        scope="host",
        metric="disk_used_percent",
        state="critical",
        value=96.0,
        occurred_at=warning_at + timedelta(seconds=10),
    )
    resolved = store.apply_alert_transition(
        scope="host",
        metric="disk_used_percent",
        state="normal",
        value=80.0,
        occurred_at=warning_at + timedelta(seconds=20),
    )

    assert raised is not None
    assert raised["event_type"] == "alert.raised"
    assert repeated is None
    assert critical is not None
    assert critical["previous_state"] == "warning"
    assert resolved is not None
    assert resolved["event_type"] == "alert.resolved"
    assert store.active_alert_states() == []

    with sqlite3.connect(database) as connection:
        events = connection.execute(
            "SELECT event_type, details_json FROM audit_events "
            "WHERE event_type LIKE 'alert.%' ORDER BY id"
        ).fetchall()
    assert [event[0] for event in events] == [
        "alert.raised",
        "alert.raised",
        "alert.resolved",
    ]
    assert json.loads(events[0][1])["metric"] == "disk_used_percent"


def test_host_samples_are_downsampled_derived_and_bounded(tmp_path: Path) -> None:
    store = HubStore(tmp_path / "hub.db")
    store.initialize()
    now = datetime.now(timezone.utc).replace(second=1, microsecond=0)
    metrics = {
        "cpu_percent": 14.2,
        "memory_used_bytes": 512,
        "memory_total_bytes": 1024,
        "disk_used_bytes": 850,
        "disk_total_bytes": 1000,
        "network_rx_bytes_per_second": 120.0,
        "network_tx_bytes_per_second": 40.0,
    }

    store.record_host_sample(metrics, now)
    store.record_host_sample({**metrics, "cpu_percent": 18.0}, now + timedelta(seconds=30))
    store.record_host_sample({**metrics, "cpu_percent": 22.0}, now + timedelta(minutes=1))

    samples = store.query_host_samples(limit=2)

    assert len(samples) == 2
    assert samples[0]["cpu_percent"] == 18.0
    assert samples[0]["memory_used_percent"] == 50.0
    assert samples[0]["disk_used_percent"] == 85.0
    assert samples[1]["cpu_percent"] == 22.0
    assert samples[0]["metrics"]["disk_total_bytes"] == 1000


def test_device_telemetry_is_persisted_and_joined_into_device_listing(
    tmp_path: Path,
) -> None:
    store = HubStore(tmp_path / "hub.db")
    store.initialize()
    device_id = store.register_device("nonce-0123456789", "0.1.0", "hall")
    telemetry = DeviceMetrics(
        sequence=4,
        uptime_seconds=123.5,
        wifi_rssi_dbm=-57,
        free_heap_bytes=191240,
        touch_ready=True,
        touch_active=False,
        audio_state="idle",
    )
    recorded_at = datetime.now(timezone.utc).replace(microsecond=0)

    recorded = store.record_device_telemetry(device_id, telemetry, recorded_at)
    latest = store.latest_device_telemetry(device_id)
    listed = store.devices()[0]

    assert recorded == latest
    assert latest == {
        "device_id": device_id,
        "sequence": 4,
        "recorded_at": recorded_at.isoformat().replace("+00:00", "Z"),
        "uptime_seconds": 123.5,
        "wifi_rssi_dbm": -57,
        "free_heap_bytes": 191240,
        "touch_ready": True,
        "touch_active": False,
        "audio_state": "idle",
        "payload": {
            "audio_state": "idle",
            "free_heap_bytes": 191240,
            "sequence": 4,
            "touch_active": False,
            "touch_ready": True,
            "uptime_seconds": 123.5,
            "wifi_rssi_dbm": -57,
        },
    }
    assert listed["latest_telemetry_at"] == latest["recorded_at"]
    assert listed["wifi_rssi_dbm"] == -57
    assert listed["online"] is True


def test_device_telemetry_rejects_unknown_device(tmp_path: Path) -> None:
    store = HubStore(tmp_path / "hub.db")
    store.initialize()
    telemetry = DeviceMetrics(
        sequence=0,
        uptime_seconds=0.0,
        wifi_rssi_dbm=-1,
        free_heap_bytes=1,
        touch_ready=False,
        touch_active=False,
        audio_state="unavailable",
    )

    with pytest.raises(ValueError, match="unknown device_id"):
        store.record_device_telemetry("missing", telemetry)
