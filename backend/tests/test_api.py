from fastapi.testclient import TestClient

from app.config import Settings
from app.domain import Metrics, Reason, ServiceStatus, StatusSnapshot
from app.main import create_app

TOKEN = "a" * 32


class FixedMonitor:
    def __init__(self, snapshot: StatusSnapshot | Exception) -> None:
        self.snapshot = snapshot

    def current(self) -> StatusSnapshot:
        if isinstance(self.snapshot, Exception):
            raise self.snapshot
        return self.snapshot


def warning_snapshot() -> StatusSnapshot:
    reason = Reason("disk_high", "warning", "System disk is 82% full")
    return StatusSnapshot(
        sequence=42,
        generated_at_epoch_s=1_787_227_200,
        overall="warning",
        summary=reason.message,
        reasons=(reason,),
        metrics=Metrics(241, 610, 821, 48_210),
        services=(ServiceStatus("hub", "Hub", "healthy"),),
    )


def request_headers(token: str = TOKEN, schema: str = "1") -> dict[str, str]:
    return {
        "Authorization": f"Bearer {token}",
        "X-Nova-Schema": schema,
        "Accept": "application/json",
    }


def test_authenticated_status_response_has_exact_contract_and_transport_headers() -> None:
    app = create_app(Settings(device_token=TOKEN), FixedMonitor(warning_snapshot()))
    with TestClient(app) as client:
        response = client.get("/v1/status", headers=request_headers())

    assert response.status_code == 200
    assert response.headers["content-type"] == "application/json"
    assert response.headers["cache-control"] == "no-store"
    assert int(response.headers["content-length"]) == len(response.content)
    assert len(response.content) <= 4096
    assert response.json() == warning_snapshot().to_wire()


def test_missing_or_incorrect_token_returns_bounded_unauthorized_error() -> None:
    app = create_app(Settings(device_token=TOKEN), FixedMonitor(warning_snapshot()))
    with TestClient(app) as client:
        missing = client.get("/v1/status", headers={"X-Nova-Schema": "1"})
        incorrect = client.get("/v1/status", headers=request_headers("b" * 32))

    expected = {
        "schema_version": 1,
        "error": {
            "code": "unauthorized",
            "message": "Device credentials were rejected",
        },
    }
    assert missing.status_code == incorrect.status_code == 401
    assert missing.json() == incorrect.json() == expected
    assert len(missing.content) <= 512
    assert missing.headers["cache-control"] == "no-store"


def test_missing_or_unsupported_schema_returns_upgrade_required() -> None:
    app = create_app(Settings(device_token=TOKEN), FixedMonitor(warning_snapshot()))
    with TestClient(app) as client:
        missing = client.get("/v1/status", headers={"Authorization": f"Bearer {TOKEN}"})
        unsupported = client.get("/v1/status", headers=request_headers(schema="2"))

    assert missing.status_code == unsupported.status_code == 426
    assert unsupported.json()["error"] == {
        "code": "unsupported_schema",
        "message": "Only NOVA schema 1 is supported",
    }


def test_authenticated_request_requires_json_accept_header() -> None:
    app = create_app(Settings(device_token=TOKEN), FixedMonitor(warning_snapshot()))
    headers = request_headers()
    headers["Accept"] = "text/plain"
    with TestClient(app) as client:
        response = client.get("/v1/status", headers=headers)

    assert response.status_code == 406
    assert response.json()["error"] == {
        "code": "not_acceptable",
        "message": "Accept application/json is required",
    }


def test_scheduler_failure_returns_transient_service_unavailable() -> None:
    app = create_app(Settings(device_token=TOKEN), FixedMonitor(RuntimeError("boom")))
    with TestClient(app) as client:
        response = client.get("/v1/status", headers=request_headers())

    assert response.status_code == 503
    assert response.json()["error"]["code"] == "snapshot_unavailable"
    assert len(response.content) <= 512


def test_route_refuses_an_encoded_snapshot_over_the_whole_body_limit() -> None:
    class OversizedMonitor:
        def current(self):
            class OversizedSnapshot:
                def to_wire(self) -> dict[str, str]:
                    return {"padding": "x" * 4097}

            return OversizedSnapshot()

    app = create_app(Settings(device_token=TOKEN), OversizedMonitor())
    with TestClient(app) as client:
        response = client.get("/v1/status", headers=request_headers())

    assert response.status_code == 503
    assert response.json()["error"]["code"] == "snapshot_too_large"
    assert len(response.content) <= 512


def test_status_endpoint_rate_limit_is_transient_and_supplies_retry_after() -> None:
    app = create_app(
        Settings(device_token=TOKEN, rate_limit_per_minute=2), FixedMonitor(warning_snapshot())
    )
    with TestClient(app) as client:
        assert client.get("/v1/status", headers=request_headers()).status_code == 200
        assert client.get("/v1/status", headers=request_headers()).status_code == 200
        limited = client.get("/v1/status", headers=request_headers())

    assert limited.status_code == 429
    assert limited.headers["retry-after"] == "60"
    assert limited.json()["error"]["code"] == "rate_limited"
