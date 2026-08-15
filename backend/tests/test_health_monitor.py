from datetime import datetime, timezone
from pathlib import Path
import sqlite3

from app.config import Settings
from app.health_monitor import HealthMonitor
from app.store import HubStore


class FakeCollector:
    def __init__(self, disk_used_bytes: int) -> None:
        self.disk_used_bytes = disk_used_bytes

    def collect(self) -> dict[str, object]:
        return {
            "cpu_percent": 10.0,
            "memory_used_bytes": 20,
            "memory_total_bytes": 100,
            "disk_used_bytes": self.disk_used_bytes,
            "disk_total_bytes": 100,
            "network_rx_bytes_per_second": 1.0,
            "network_tx_bytes_per_second": 2.0,
            "collection_errors": [],
            "dependency_status": "healthy",
        }


def _settings(database: Path) -> Settings:
    return Settings(
        db_path=database,
        metrics_history_period_seconds=60,
        health_trend_points=4,
    )


def test_monitor_persists_one_alert_transition_and_resolves_it(tmp_path: Path) -> None:
    database = tmp_path / "hub.db"
    settings = _settings(database)
    store = HubStore(database)
    store.initialize()
    collector = FakeCollector(86)
    monitor = HealthMonitor(
        settings,
        store,
        collector,  # type: ignore[arg-type]
        clock=lambda: datetime(2026, 8, 15, tzinfo=timezone.utc),
    )

    warning = monitor.sample()

    assert warning["health_grade"] == "warning"
    assert warning["active_alerts"][0]["metric"] == "disk_used_percent"
    assert store.active_alert_states()[0]["state"] == "warning"

    collector.disk_used_bytes = 80
    resolved = monitor.sample()

    assert resolved["health_grade"] == "normal"
    assert resolved["active_alerts"] == []
    assert store.active_alert_states() == []
    with sqlite3.connect(database) as connection:
        events = connection.execute(
            "SELECT event_type FROM audit_events ORDER BY id"
        ).fetchall()
    assert [event[0] for event in events] == [
        "alert.raised",
        "alert.resolved",
    ]


def test_monitor_snapshot_contains_bounded_trends(tmp_path: Path) -> None:
    database = tmp_path / "hub.db"
    settings = _settings(database)
    store = HubStore(database)
    store.initialize()
    monitor = HealthMonitor(
        settings,
        store,
        FakeCollector(70),  # type: ignore[arg-type]
        clock=lambda: datetime(2026, 8, 15, tzinfo=timezone.utc),
    )

    for _ in range(6):
        monitor.sample()

    trends = monitor.snapshot()["trends"]
    assert trends["period_seconds"] == 60
    assert len(trends["cpu_percent"]) == 1
    assert len(trends["disk_used_percent"]) == 1
