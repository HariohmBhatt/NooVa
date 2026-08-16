"""FastAPI application for the local NOVA hub."""

import asyncio
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
import json
import time
from typing import Any
from zoneinfo import ZoneInfo

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect, status
from fastapi.responses import StreamingResponse

from .config import Settings
from .device_hello import DeviceHello
from .health_response import HealthResponse
from .metrics import HostMetricsCollector
from .protocol import envelope, utc_now
from .registration_request import RegistrationRequest
from .registration_response import RegistrationResponse
from .store import HubStore
from .telemetry_stream import STREAM_CONTENT_TYPE, STREAM_VERSION, encode_server_item


class _TelemetrySnapshotCache:
    """Share one host/GPU collection between all telemetry clients."""

    def __init__(self, config: Settings, metrics: HostMetricsCollector) -> None:
        self.config = config
        self.metrics = metrics
        self.payload: dict[str, Any] | None = None
        self.collected_at = 0.0

    def current(self) -> dict[str, Any]:
        now = time.monotonic()
        interval = max(self.config.telemetry_interval_seconds, 0.1)
        if self.payload is None or now - self.collected_at >= interval:
            self.payload = _health_payload(self.config, self.metrics)
            self.collected_at = now
        return self.payload


def create_app(settings: Settings | None = None) -> FastAPI:
    config = settings or Settings.from_env()
    store = HubStore(config.db_path)
    metrics = HostMetricsCollector(
        config.metrics_proc_root,
        config.metrics_sys_root,
        config.metrics_disk_root,
        config.metrics_interface,
        config.metrics_gpu_library,
    )
    telemetry_cache = _TelemetrySnapshotCache(config, metrics)

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        store.initialize()
        yield

    app = FastAPI(
        title="NOVA Hub API", version=config.hub_version, lifespan=lifespan
    )
    app.state.settings = config
    app.state.store = store
    app.state.metrics = metrics

    @app.get("/healthz")
    async def healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.post("/v1/register", response_model=RegistrationResponse)
    async def register(payload: RegistrationRequest) -> RegistrationResponse:
        if payload.protocol_version != config.protocol_version:
            raise HTTPException(status_code=426, detail="protocol version unsupported")
        display_name = f"terminal-{payload.installation_nonce[-8:]}"
        device_id = store.register_device(
            payload.installation_nonce, payload.firmware, display_name
        )
        return RegistrationResponse(
            protocol_version=config.protocol_version,
            device_id=device_id,
            hub_host=config.hub_host,
            hub_port=config.hub_port,
            server_time=utc_now(),
        )

    @app.get("/v1/health", response_model=HealthResponse)
    async def health() -> HealthResponse:
        return _telemetry_response(config, telemetry_cache)

    @app.get("/v1/telemetry", response_model=HealthResponse)
    async def telemetry() -> HealthResponse:
        """Return one current host and GPU telemetry snapshot."""
        return _telemetry_response(config, telemetry_cache)

    @app.get("/v1/telemetry/stream")
    async def telemetry_stream() -> StreamingResponse:
        """Stream fixed-width server items until the device disconnects."""
        return StreamingResponse(
            _telemetry_items(config, telemetry_cache),
            media_type=STREAM_CONTENT_TYPE,
            headers={
                "Cache-Control": "no-store",
                "X-Nova-Telemetry-Version": str(STREAM_VERSION),
            },
        )

    @app.websocket("/v1/ws")
    async def websocket_session(websocket: WebSocket) -> None:
        await websocket.accept()
        try:
            hello = await _receive_hello(websocket, config.protocol_version)
            store.mark_seen(hello.device_id, hello.installation_nonce)
            await websocket.send_json(
                envelope(
                    config.protocol_version,
                    "session.ready",
                    {
                        "device_id": hello.device_id,
                        "server_version": config.hub_version,
                        "server_time": utc_now().isoformat().replace("+00:00", "Z"),
                    },
                )
            )
            await _stream_health(
                websocket, config.protocol_version, config, telemetry_cache
            )
        except WebSocketDisconnect:
            return
        except (ValueError, json.JSONDecodeError, asyncio.TimeoutError):
            await websocket.close(code=status.WS_1002_PROTOCOL_ERROR, reason="invalid message")

    return app


async def _telemetry_items(
    config: Settings, telemetry_cache: _TelemetrySnapshotCache
) -> AsyncIterator[bytes]:
    sequence = 0
    while True:
        now = utc_now()
        yield encode_server_item(
            sequence,
            telemetry_cache.current(),
            int(now.timestamp()),
        )
        sequence = (sequence + 1) & 0xFFFFFFFF
        await asyncio.sleep(config.telemetry_interval_seconds)


async def _receive_hello(
    websocket: WebSocket, protocol_version: int
) -> DeviceHello:
    raw = await asyncio.wait_for(websocket.receive_text(), timeout=10)
    message = json.loads(raw)
    if message.get("protocol_version") != protocol_version:
        raise ValueError("protocol version unsupported")
    if message.get("type") != "device.hello":
        raise ValueError("device.hello required")
    return DeviceHello.model_validate(message.get("payload", {}))


async def _stream_health(
    websocket: WebSocket,
    protocol_version: int,
    config: Settings,
    telemetry_cache: _TelemetrySnapshotCache,
) -> None:
    while True:
        await websocket.send_json(
            envelope(
                protocol_version,
                "health.snapshot",
                telemetry_cache.current(),
            )
        )
        try:
            raw = await asyncio.wait_for(websocket.receive_text(), timeout=5)
        except asyncio.TimeoutError:
            continue
        message = json.loads(raw)
        if message.get("protocol_version") != protocol_version:
            await websocket.send_json(
                envelope(
                    protocol_version,
                    "protocol.error",
                    {
                        "code": "incompatible_version",
                        "message": "protocol version unsupported",
                    },
                )
            )
            await websocket.close(
                code=status.WS_1002_PROTOCOL_ERROR,
                reason="protocol version unsupported",
            )
            return
        if message.get("type") == "device.ping":
            await websocket.send_json(envelope(protocol_version, "server.pong", {}))
        else:
            await websocket.send_json(
                envelope(
                    protocol_version,
                    "protocol.error",
                    {"code": "unknown_message", "message": "message ignored"},
                )
            )

def _health_payload(
    config: Settings, metrics: HostMetricsCollector
) -> dict[str, Any]:
    payload = metrics.collect()
    now = utc_now()
    payload["server_version"] = config.hub_version
    payload["home_time"] = now.astimezone(ZoneInfo(config.home_timezone)).replace(
        microsecond=0
    ).isoformat()
    payload["service_status"] = {
        "hub_api": "healthy",
        "metrics": "healthy" if not payload["collection_errors"] else "degraded",
    }
    return payload


def _telemetry_response(
    config: Settings, telemetry_cache: _TelemetrySnapshotCache
) -> HealthResponse:
    return HealthResponse(
        protocol_version=config.protocol_version,
        server_version=config.hub_version,
        server_time=utc_now(),
        payload=telemetry_cache.current(),
    )


app = create_app()
