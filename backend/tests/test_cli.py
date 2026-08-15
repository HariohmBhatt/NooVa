from pathlib import Path
from datetime import datetime, timedelta, timezone
import json

from app.cli import main
from app.device_metrics import DeviceMetrics
from app.store import HubStore


def test_operator_can_list_a_registered_device(monkeypatch, capsys, tmp_path: Path) -> None:
    database = tmp_path / "hub.db"
    store = HubStore(database)
    store.initialize()
    device_id = store.register_device("nonce-0123456789", "0.1.0", "hall")
    monkeypatch.setenv("NOVA_DB_PATH", str(database))

    assert main(["devices"]) == 0

    output = capsys.readouterr().out
    assert f"{device_id}\thall" in output


def test_devices_shows_last_seen_and_latest_telemetry(
    monkeypatch, capsys, tmp_path: Path
) -> None:
    database = tmp_path / "hub.db"
    store = HubStore(database)
    store.initialize()
    device_id = store.register_device("nonce-0123456789", "0.1.0", "hall")
    store.record_device_telemetry(
        device_id,
        DeviceMetrics(
            sequence=1,
            uptime_seconds=42.0,
            wifi_rssi_dbm=-60,
            free_heap_bytes=200000,
            touch_ready=True,
            touch_active=False,
            audio_state="idle",
        ),
    )
    monkeypatch.setenv("NOVA_DB_PATH", str(database))

    assert main(["devices"]) == 0

    output = capsys.readouterr().out
    assert "last_seen=" in output
    assert "online=yes" in output
    assert "rssi=-60" in output
    assert "audio=idle" in output


def test_metrics_is_bounded_and_supports_json_output(
    monkeypatch, capsys, tmp_path: Path
) -> None:
    database = tmp_path / "hub.db"
    store = HubStore(database)
    store.initialize()
    now = datetime.now(timezone.utc).replace(second=0, microsecond=0)
    store.record_host_sample({"cpu_percent": 10.0}, now)
    store.record_host_sample({"cpu_percent": 20.0}, now + timedelta(minutes=1))
    monkeypatch.setenv("NOVA_DB_PATH", str(database))

    assert main(["metrics", "--limit", "1"]) == 0
    human = capsys.readouterr().out
    assert human.count("\n") == 2
    assert "20.0" in human

    assert main(["metrics", "--limit", "1", "--json"]) == 0
    parsed = json.loads(capsys.readouterr().out)
    assert len(parsed) == 1
    assert parsed[0]["cpu_percent"] == 20.0
