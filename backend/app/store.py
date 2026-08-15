"""SQLite persistence for devices, monitoring state, and audit events."""

from datetime import datetime, timedelta, timezone
import json
from collections.abc import Mapping
from pathlib import Path
import sqlite3
from typing import Any
from uuid import uuid4

from .device_metrics import DeviceMetrics


_LATEST_SCHEMA_VERSION = 4
DEFAULT_AUDIT_RETENTION_DAYS = 30
DEFAULT_HOST_SAMPLE_PERIOD_SECONDS = 60
DEFAULT_HOST_SAMPLE_RETENTION_DAYS = 30
DEFAULT_DEVICE_TELEMETRY_RETENTION_DAYS = 30
DEFAULT_HOST_SAMPLE_LIMIT = 12
DEFAULT_QUERY_LIMIT = 1000
DEFAULT_DEVICE_OFFLINE_AFTER_SECONDS = 45
_PERCENT_SCALE = 100.0
_ALERT_STATES = frozenset({"normal", "warning", "critical"})


def _utc(value: datetime) -> datetime:
    if value.tzinfo is None:
        return value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc)


def _timestamp(value: datetime) -> str:
    return _utc(value).isoformat().replace("+00:00", "Z")


def _parse_timestamp(value: str | None) -> datetime | None:
    if value is None:
        return None
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _positive_setting(name: str, value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(f"{name} must be a positive integer")
    return value


def _query_limit(limit: int) -> int:
    if isinstance(limit, bool) or not isinstance(limit, int):
        raise ValueError("limit must be an integer")
    if not 1 <= limit <= DEFAULT_QUERY_LIMIT:
        raise ValueError(f"limit must be between 1 and {DEFAULT_QUERY_LIMIT}")
    return limit


def _numeric_metric(metrics: Mapping[str, Any], name: str) -> float | None:
    value = metrics.get(name)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value)


def _percentage_metric(
    metrics: Mapping[str, Any], percentage_name: str, used_name: str, total_name: str
) -> float | None:
    explicit = _numeric_metric(metrics, percentage_name)
    if explicit is not None:
        return explicit
    used = _numeric_metric(metrics, used_name)
    total = _numeric_metric(metrics, total_name)
    if used is None or total is None or total <= 0:
        return None
    return round(used / total * _PERCENT_SCALE, 1)


class HubStore:
    """SQLite-backed device, telemetry, history, alert, and audit repository."""

    def __init__(
        self,
        path: Path,
        *,
        audit_retention_days: int = DEFAULT_AUDIT_RETENTION_DAYS,
        host_sample_period_seconds: int = DEFAULT_HOST_SAMPLE_PERIOD_SECONDS,
        host_sample_retention_days: int = DEFAULT_HOST_SAMPLE_RETENTION_DAYS,
        device_telemetry_retention_days: int = DEFAULT_DEVICE_TELEMETRY_RETENTION_DAYS,
        device_offline_after_seconds: int = DEFAULT_DEVICE_OFFLINE_AFTER_SECONDS,
    ) -> None:
        """Store hub state at ``path`` using named retention and timing settings."""
        self.path = path
        self.audit_retention_days = _positive_setting(
            "audit_retention_days", audit_retention_days
        )
        self.host_sample_period_seconds = _positive_setting(
            "host_sample_period_seconds", host_sample_period_seconds
        )
        self.host_sample_retention_days = _positive_setting(
            "host_sample_retention_days", host_sample_retention_days
        )
        self.device_telemetry_retention_days = _positive_setting(
            "device_telemetry_retention_days", device_telemetry_retention_days
        )
        self.device_offline_after_seconds = _positive_setting(
            "device_offline_after_seconds", device_offline_after_seconds
        )

    def initialize(self) -> None:
        """Create or migrate the database and prune retained records."""
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as connection:
            connection.execute("PRAGMA journal_mode=WAL")
            connection.execute("BEGIN IMMEDIATE")
            self._apply_migrations(connection)
            now = datetime.now(timezone.utc)
            self._prune_audit_events(connection, now)
            self._prune_host_samples(connection, now)
            self._prune_device_telemetry(connection, now)

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

    def devices(self, *, offline_after_seconds: int | None = None) -> list[dict[str, Any]]:
        """Return devices with latest telemetry and derived online status."""
        threshold = self.device_offline_after_seconds
        if offline_after_seconds is not None:
            threshold = _positive_setting("offline_after_seconds", offline_after_seconds)
        query = """
            SELECT d.device_id, d.display_name, d.firmware, d.created_at,
                   d.last_seen_at, t.recorded_at AS latest_telemetry_at,
                   t.sequence AS telemetry_sequence,
                   t.uptime_seconds, t.wifi_rssi_dbm, t.free_heap_bytes,
                   t.touch_ready, t.touch_active, t.audio_state
            FROM devices AS d
            LEFT JOIN device_telemetry_samples AS t
              ON t.id = (
                SELECT latest.id
                FROM device_telemetry_samples AS latest
                WHERE latest.device_id = d.device_id
                ORDER BY latest.recorded_at DESC, latest.id DESC
                LIMIT 1
              )
            ORDER BY d.created_at
        """
        with self._connect() as connection:
            rows = connection.execute(query).fetchall()
        return [self._device_row(row, threshold) for row in rows]

    def active_alert_states(
        self, *, scope: str | None = None
    ) -> list[dict[str, Any]] | dict[str, str]:
        """Return currently active warning and critical alert states."""
        with self._connect() as connection:
            query = (
                "SELECT alert_key, scope, metric, device_id, state, value, "
                "details_json, raised_at, updated_at FROM active_alerts "
                "ORDER BY raised_at, alert_key"
            )
            rows = connection.execute(query).fetchall()
        states = [self._active_alert_row(row) for row in rows]
        if scope is None:
            return states
        return {row["metric"]: row["state"] for row in states if row["scope"] == scope}

    def apply_alert_transition(
        self,
        *,
        scope: str,
        metric: str,
        state: str | None = None,
        value: float | int | None = None,
        details: Mapping[str, Any] | None = None,
        device_id: str | None = None,
        occurred_at: datetime | None = None,
        subject_id: str | None = None,
        previous_state: str | None = None,
        next_state: str | None = None,
    ) -> dict[str, Any] | None:
        """Atomically apply an alert state change and audit the transition.

        ``normal`` removes an existing active alert. ``warning`` and ``critical``
        create or update an active alert. Repeating the current state updates its
        current value without emitting another audit event and returns ``None``.
        """
        if device_id is None:
            device_id = subject_id
        if state is None:
            state = next_state
        if state is None:
            raise ValueError("state or next_state must be provided")
        self._validate_alert_identity(scope, metric, device_id)
        if state not in _ALERT_STATES:
            raise ValueError(f"state must be one of {sorted(_ALERT_STATES)}")
        if details is not None and not isinstance(details, Mapping):
            raise TypeError("details must be a mapping")
        alert_key = self._alert_key(scope, metric, device_id)
        timestamp = _timestamp(occurred_at or datetime.now(timezone.utc))
        detail_values = dict(details or {})
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT state, raised_at FROM active_alerts WHERE alert_key = ?",
                (alert_key,),
            ).fetchone()
            previous_state = row["state"] if row is not None else None
            if state == "normal":
                if row is None:
                    return None
                connection.execute(
                    "DELETE FROM active_alerts WHERE alert_key = ?", (alert_key,)
                )
                event_type = "alert.resolved"
            elif previous_state is None:
                connection.execute(
                    "INSERT INTO active_alerts(alert_key, scope, metric, device_id, "
                    "state, value, details_json, raised_at, updated_at) "
                    "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        alert_key,
                        scope,
                        metric,
                        device_id,
                        state,
                        value,
                        json.dumps(detail_values, sort_keys=True),
                        timestamp,
                        timestamp,
                    ),
                )
                event_type = "alert.raised"
            else:
                connection.execute(
                    "UPDATE active_alerts SET state = ?, value = ?, details_json = ?, "
                    "updated_at = ? WHERE alert_key = ?",
                    (
                        state,
                        value,
                        json.dumps(detail_values, sort_keys=True),
                        timestamp,
                        alert_key,
                    ),
                )
                if previous_state == state:
                    return None
                event_type = "alert.raised"

            event_details = dict(detail_values)
            event_details.update(
                {
                    "alert_key": alert_key,
                    "metric": metric,
                    "previous_state": previous_state,
                    "scope": scope,
                    "state": state,
                    "value": value,
                }
            )
            self._audit(
                connection,
                event_type,
                device_id,
                event_details,
                created_at=occurred_at,
            )
            return {
                "alert_key": alert_key,
                "scope": scope,
                "metric": metric,
                "device_id": device_id,
                "previous_state": previous_state,
                "state": state,
                "value": value,
                "event_type": event_type,
                "changed_at": timestamp,
            }

    def record_host_sample(
        self, metrics: Mapping[str, Any], sampled_at: datetime | None = None
    ) -> dict[str, Any]:
        """Upsert one downsampled host metric sample and return its public row."""
        if not isinstance(metrics, Mapping):
            raise TypeError("metrics must be a mapping")
        snapshot = dict(metrics)
        if sampled_at is None and isinstance(snapshot.get("sampled_at"), datetime):
            sampled_at = snapshot.pop("sampled_at")
        try:
            metrics_json = json.dumps(snapshot, sort_keys=True)
        except (TypeError, ValueError) as error:
            raise ValueError("host metrics must be JSON serializable") from error

        timestamp = _utc(sampled_at or datetime.now(timezone.utc))
        bucket = self._sample_bucket(timestamp)
        values = (
            _numeric_metric(snapshot, "cpu_percent"),
            _percentage_metric(
                snapshot,
                "memory_used_percent",
                "memory_used_bytes",
                "memory_total_bytes",
            ),
            _percentage_metric(
                snapshot, "disk_used_percent", "disk_used_bytes", "disk_total_bytes"
            ),
            _numeric_metric(snapshot, "network_rx_bytes_per_second"),
            _numeric_metric(snapshot, "network_tx_bytes_per_second"),
        )
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            connection.execute(
                "INSERT INTO host_metric_samples(" 
                "sampled_at, cpu_percent, memory_used_percent, disk_used_percent, "
                "network_rx_bytes_per_second, network_tx_bytes_per_second, metrics_json) "
                "VALUES(?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(sampled_at) DO UPDATE SET "
                "cpu_percent = excluded.cpu_percent, "
                "memory_used_percent = excluded.memory_used_percent, "
                "disk_used_percent = excluded.disk_used_percent, "
                "network_rx_bytes_per_second = excluded.network_rx_bytes_per_second, "
                "network_tx_bytes_per_second = excluded.network_tx_bytes_per_second, "
                "metrics_json = excluded.metrics_json",
                (bucket, *values, metrics_json),
            )
            self._prune_host_samples(connection, datetime.now(timezone.utc))
            row = connection.execute(
                "SELECT sampled_at, cpu_percent, memory_used_percent, "
                "disk_used_percent, network_rx_bytes_per_second, "
                "network_tx_bytes_per_second, metrics_json "
                "FROM host_metric_samples WHERE sampled_at = ?",
                (bucket,),
            ).fetchone()
        return self._host_sample_row(row)

    def query_host_samples(
        self,
        limit: int = DEFAULT_HOST_SAMPLE_LIMIT,
        since: datetime | None = None,
    ) -> list[dict[str, Any]]:
        """Return the newest bounded host samples in chronological order."""
        bounded_limit = _query_limit(limit)
        clauses: list[str] = []
        parameters: list[Any] = []
        if since is not None:
            clauses.append("sampled_at >= ?")
            parameters.append(_timestamp(since))
        where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
        query = f"""
            SELECT sampled_at, cpu_percent, memory_used_percent,
                   disk_used_percent, network_rx_bytes_per_second,
                   network_tx_bytes_per_second, metrics_json
            FROM host_metric_samples
            {where}
            ORDER BY sampled_at DESC
            LIMIT ?
        """
        parameters.append(bounded_limit)
        with self._connect() as connection:
            rows = connection.execute(query, parameters).fetchall()
        return [self._host_sample_row(row) for row in reversed(rows)]

    def host_metric_samples(
        self, limit: int = DEFAULT_HOST_SAMPLE_LIMIT
    ) -> list[dict[str, Any]]:
        """Return history rows in the legacy flat shape used by the monitor."""
        samples = self.query_host_samples(limit=limit)
        history: list[dict[str, Any]] = []
        for sample in samples:
            row = dict(sample["metrics"])
            row.update(
                {
                    "sampled_at": sample["sampled_at"],
                    "cpu_percent": sample["cpu_percent"],
                    "memory_used_percent": sample["memory_used_percent"],
                    "disk_used_percent": sample["disk_used_percent"],
                }
            )
            history.append(row)
        return history

    def record_device_telemetry(
        self,
        device_id: str,
        telemetry: DeviceMetrics | Mapping[str, Any],
        recorded_at: datetime | None = None,
    ) -> dict[str, Any]:
        """Validate and persist one terminal telemetry frame idempotently."""
        if not device_id:
            raise ValueError("device_id must not be empty")
        metrics = (
            telemetry
            if isinstance(telemetry, DeviceMetrics)
            else DeviceMetrics.model_validate(telemetry)
        )
        timestamp = _timestamp(recorded_at or datetime.now(timezone.utc))
        payload = metrics.model_dump(mode="json")
        payload_json = json.dumps(payload, sort_keys=True)
        with self._connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            if not connection.execute(
                "SELECT 1 FROM devices WHERE device_id = ?", (device_id,)
            ).fetchone():
                raise ValueError(f"unknown device_id: {device_id}")
            connection.execute(
                "INSERT INTO device_telemetry_samples(" 
                "device_id, sequence, recorded_at, uptime_seconds, wifi_rssi_dbm, "
                "free_heap_bytes, touch_ready, touch_active, audio_state, payload_json) "
                "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(device_id, sequence) DO UPDATE SET "
                "recorded_at = excluded.recorded_at, "
                "uptime_seconds = excluded.uptime_seconds, "
                "wifi_rssi_dbm = excluded.wifi_rssi_dbm, "
                "free_heap_bytes = excluded.free_heap_bytes, "
                "touch_ready = excluded.touch_ready, "
                "touch_active = excluded.touch_active, "
                "audio_state = excluded.audio_state, "
                "payload_json = excluded.payload_json",
                (
                    device_id,
                    metrics.sequence,
                    timestamp,
                    metrics.uptime_seconds,
                    metrics.wifi_rssi_dbm,
                    metrics.free_heap_bytes,
                    int(metrics.touch_ready),
                    int(metrics.touch_active),
                    metrics.audio_state.value,
                    payload_json,
                ),
            )
            connection.execute(
                "UPDATE devices SET last_seen_at = CASE "
                "WHEN last_seen_at IS NULL OR last_seen_at < ? THEN ? "
                "ELSE last_seen_at END WHERE device_id = ?",
                (timestamp, timestamp, device_id),
            )
            self._prune_device_telemetry(connection, datetime.now(timezone.utc))
            row = connection.execute(
                "SELECT device_id, sequence, recorded_at, uptime_seconds, "
                "wifi_rssi_dbm, free_heap_bytes, touch_ready, touch_active, "
                "audio_state, payload_json FROM device_telemetry_samples "
                "WHERE device_id = ? AND sequence = ?",
                (device_id, metrics.sequence),
            ).fetchone()
        return self._telemetry_row(row)

    def latest_device_telemetry(self, device_id: str) -> dict[str, Any] | None:
        """Return the most recently recorded telemetry frame for a device."""
        with self._connect() as connection:
            row = connection.execute(
                "SELECT device_id, sequence, recorded_at, uptime_seconds, "
                "wifi_rssi_dbm, free_heap_bytes, touch_ready, touch_active, "
                "audio_state, payload_json FROM device_telemetry_samples "
                "WHERE device_id = ? ORDER BY recorded_at DESC, id DESC LIMIT 1",
                (device_id,),
            ).fetchone()
        return None if row is None else self._telemetry_row(row)

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
            4: self._migration_four,
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
    def _migration_four(connection: sqlite3.Connection) -> None:
        connection.execute(
            "CREATE TABLE IF NOT EXISTS active_alerts ("
            "alert_key TEXT PRIMARY KEY, scope TEXT NOT NULL, metric TEXT NOT NULL, "
            "device_id TEXT, state TEXT NOT NULL CHECK(state IN ('warning', 'critical')), "
            "value REAL, details_json TEXT NOT NULL, raised_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL)"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS active_alerts_device_index "
            "ON active_alerts(device_id, state)"
        )
        connection.execute(
            "CREATE TABLE IF NOT EXISTS host_metric_samples ("
            "sampled_at TEXT PRIMARY KEY, cpu_percent REAL, "
            "memory_used_percent REAL, disk_used_percent REAL, "
            "network_rx_bytes_per_second REAL, network_tx_bytes_per_second REAL, "
            "metrics_json TEXT NOT NULL)"
        )
        connection.execute(
            "CREATE TABLE IF NOT EXISTS device_telemetry_samples ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, device_id TEXT NOT NULL, "
            "sequence INTEGER NOT NULL, recorded_at TEXT NOT NULL, "
            "uptime_seconds REAL NOT NULL, wifi_rssi_dbm INTEGER NOT NULL, "
            "free_heap_bytes INTEGER NOT NULL, touch_ready INTEGER NOT NULL, "
            "touch_active INTEGER NOT NULL, audio_state TEXT NOT NULL, "
            "payload_json TEXT NOT NULL, UNIQUE(device_id, sequence))"
        )
        connection.execute(
            "CREATE INDEX IF NOT EXISTS device_telemetry_latest_index "
            "ON device_telemetry_samples(device_id, recorded_at DESC)"
        )

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

    def _prune_audit_events(
        self, connection: sqlite3.Connection, now: datetime | None = None
    ) -> None:
        cutoff = _timestamp(
            (now or datetime.now(timezone.utc))
            - timedelta(days=self.audit_retention_days)
        )
        connection.execute("DELETE FROM audit_events WHERE created_at < ?", (cutoff,))

    def _prune_host_samples(
        self, connection: sqlite3.Connection, now: datetime | None = None
    ) -> None:
        cutoff = _timestamp(
            (now or datetime.now(timezone.utc))
            - timedelta(days=self.host_sample_retention_days)
        )
        connection.execute("DELETE FROM host_metric_samples WHERE sampled_at < ?", (cutoff,))

    def _prune_device_telemetry(
        self, connection: sqlite3.Connection, now: datetime | None = None
    ) -> None:
        cutoff = _timestamp(
            (now or datetime.now(timezone.utc))
            - timedelta(days=self.device_telemetry_retention_days)
        )
        connection.execute(
            "DELETE FROM device_telemetry_samples WHERE recorded_at < ?", (cutoff,)
        )

    def _sample_bucket(self, value: datetime) -> str:
        seconds = int(_utc(value).timestamp())
        bucket_seconds = seconds - seconds % self.host_sample_period_seconds
        return _timestamp(datetime.fromtimestamp(bucket_seconds, timezone.utc))

    @staticmethod
    def _validate_alert_identity(
        scope: str, metric: str, device_id: str | None
    ) -> None:
        if not scope or len(scope) > 64:
            raise ValueError("scope must contain 1 to 64 characters")
        if not metric or len(metric) > 128:
            raise ValueError("metric must contain 1 to 128 characters")
        if device_id is not None and not device_id:
            raise ValueError("device_id must not be empty")

    @staticmethod
    def _alert_key(scope: str, metric: str, device_id: str | None) -> str:
        return f"{scope}:{device_id or 'hub'}:{metric}"

    @staticmethod
    def _active_alert_row(row: sqlite3.Row) -> dict[str, Any]:
        return {
            "alert_key": row["alert_key"],
            "scope": row["scope"],
            "metric": row["metric"],
            "device_id": row["device_id"],
            "state": row["state"],
            "value": row["value"],
            "details": json.loads(row["details_json"]),
            "raised_at": row["raised_at"],
            "updated_at": row["updated_at"],
        }

    @staticmethod
    def _host_sample_row(row: sqlite3.Row) -> dict[str, Any]:
        return {
            "sampled_at": row["sampled_at"],
            "cpu_percent": row["cpu_percent"],
            "memory_used_percent": row["memory_used_percent"],
            "disk_used_percent": row["disk_used_percent"],
            "network_rx_bytes_per_second": row["network_rx_bytes_per_second"],
            "network_tx_bytes_per_second": row["network_tx_bytes_per_second"],
            "metrics": json.loads(row["metrics_json"]),
        }

    @staticmethod
    def _telemetry_row(row: sqlite3.Row) -> dict[str, Any]:
        return {
            "device_id": row["device_id"],
            "sequence": row["sequence"],
            "recorded_at": row["recorded_at"],
            "uptime_seconds": row["uptime_seconds"],
            "wifi_rssi_dbm": row["wifi_rssi_dbm"],
            "free_heap_bytes": row["free_heap_bytes"],
            "touch_ready": bool(row["touch_ready"]),
            "touch_active": bool(row["touch_active"]),
            "audio_state": row["audio_state"],
            "payload": json.loads(row["payload_json"]),
        }

    def _device_row(self, row: sqlite3.Row, offline_after_seconds: int) -> dict[str, Any]:
        result = dict(row)
        for field in ("touch_ready", "touch_active"):
            if result[field] is not None:
                result[field] = bool(result[field])
        last_seen = _parse_timestamp(result["last_seen_at"])
        result["online"] = bool(
            last_seen is not None
            and datetime.now(timezone.utc) - _utc(last_seen)
            <= timedelta(seconds=offline_after_seconds)
        )
        return result

    def _audit(
        self,
        connection: sqlite3.Connection,
        event_type: str,
        device_id: str | None,
        details: dict[str, Any],
        *,
        created_at: datetime | None = None,
    ) -> None:
        connection.execute(
            "INSERT INTO audit_events(event_type, device_id, created_at, details_json) "
            "VALUES(?, ?, ?, ?)",
            (
                event_type,
                device_id,
                _timestamp(created_at or datetime.now(timezone.utc)),
                json.dumps(details, sort_keys=True),
            ),
        )
