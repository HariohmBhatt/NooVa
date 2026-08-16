from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.config import Settings
from app.main import create_app


def test_database_initializes_only_during_application_lifespan(
    tmp_path: Path,
) -> None:
    database = tmp_path / "hub.db"
    app = create_app(Settings(db_path=database))

    assert not database.exists()

    with TestClient(app):
        assert database.exists()


def test_healthz_is_available_without_device_credentials(tmp_path: Path) -> None:
    app = create_app(Settings(db_path=tmp_path / "hub.db"))

    with TestClient(app) as client:
        response = client.get("/healthz")

    assert response.status_code == 200
    assert response.json() == {"status": "ok"}


def test_v1_health_is_available_without_device_credentials(tmp_path: Path) -> None:
    app = create_app(Settings(db_path=tmp_path / "hub.db"))

    with TestClient(app) as client:
        response = client.get("/v1/health")

    message = response.json()
    assert response.status_code == 200
    assert message["protocol_version"] == 1
    assert message["payload"]["server_version"] == "0.1.0"


def test_v1_telemetry_exposes_server_cpu_and_gpu_fields(tmp_path: Path) -> None:
    app = create_app(Settings(db_path=tmp_path / "hub.db"))

    with TestClient(app) as client:
        response = client.get("/v1/telemetry")

    message = response.json()
    assert response.status_code == 200
    assert "cpu_percent" in message["payload"]
    assert "gpu_state" in message["payload"]
    assert "gpu_utilization_percent" in message["payload"]
    assert "gpu_temperature_c" in message["payload"]
    assert "gpu_vram_used_bytes" in message["payload"]
    assert "gpu_vram_total_bytes" in message["payload"]


def test_production_configuration_does_not_require_peppers(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("NOVA_ENV", "production")
    monkeypatch.setenv("NOVA_DB_PATH", str(tmp_path / "hub.db"))

    create_app()


def test_registration_returns_a_stable_identity_without_a_secret(
    tmp_path: Path,
) -> None:
    app = create_app(Settings(db_path=tmp_path / "hub.db"))
    payload = {
        "protocol_version": 1,
        "installation_nonce": "nonce-0123456789",
        "firmware": "0.1.0",
        "capabilities": ["touch"],
    }

    with TestClient(app) as client:
        first = client.post("/v1/register", json=payload)
        second = client.post("/v1/register", json=payload)

    assert first.status_code == 200
    assert first.json()["hub_host"] == "nova-hub.local"
    assert "token" not in first.json()
    assert "session_secret" not in first.json()
    assert second.json()["device_id"] == first.json()["device_id"]


def test_websocket_streams_health_without_authentication(tmp_path: Path) -> None:
    app = create_app(Settings(db_path=tmp_path / "hub.db"))

    with TestClient(app) as client:
        registered = client.post(
            "/v1/register",
            json={
                "protocol_version": 1,
                "installation_nonce": "nonce-0123456789",
                "firmware": "0.1.0",
                "capabilities": ["touch"],
            },
        ).json()
        with client.websocket_connect("/v1/ws") as socket:
            socket.send_json(
                {
                    "protocol_version": 1,
                    "type": "device.hello",
                    "payload": {
                        "device_id": registered["device_id"],
                        "installation_nonce": "nonce-0123456789",
                        "firmware": "0.1.0",
                        "capabilities": ["touch"],
                    },
                }
            )
            ready = socket.receive_json()
            snapshot = socket.receive_json()

    assert ready["type"] == "session.ready"
    assert snapshot["type"] == "health.snapshot"
    assert snapshot["payload"]["server_version"] == "0.1.0"
    assert snapshot["payload"]["service_status"]["hub_api"] == "healthy"
    assert "+05:30" in snapshot["payload"]["home_time"]
