"""FastAPI application for the local NOVA hub."""

import asyncio
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
import json
import time

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect, status

from .config import Settings
from .device_hello import DeviceHello
from .device_metrics import DeviceMetrics
from .health_monitor import HealthMonitor
from .health_response import HealthResponse
from .metrics import HostMetricsCollector
from .protocol import envelope, utc_now
from .registration_request import RegistrationRequest
from .registration_response import RegistrationResponse
from .store import HubStore


def create_app(settings: Settings | None = None) -> FastAPI:
    config = settings or Settings.from_env()
    store = HubStore(
        config.db_path,
        host_sample_period_seconds=config.metrics_history_period_seconds,
        host_sample_retention_days=config.metrics_history_retention_days,
        device_offline_after_seconds=config.device_offline_after_seconds,
    )
    metrics = HostMetricsCollector(
        config.metrics_proc_root,
        config.metrics_sys_root,
        config.metrics_disk_root,
        config.metrics_interface,
    )
    monitor: HealthMonitor | None = None
    monitor_task: asyncio.Task[None] | None = None

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        nonlocal monitor, monitor_task
        store.initialize()
        monitor = HealthMonitor(config, store, metrics)
        monitor.sample()
        app.state.monitor = monitor
        monitor_task = asyncio.create_task(monitor.run())
        try:
            yield
        finally:
            if monitor_task is not None:
                monitor_task.cancel()
                await asyncio.gather(monitor_task, return_exceptions=True)

    app = FastAPI(
        title="NOVA Hub API", version=config.hub_version, lifespan=lifespan
    )
    app.state.settings = config
    app.state.store = store
    app.state.metrics = metrics
    app.state.monitor = None

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
        if monitor is None:
            raise HTTPException(status_code=503, detail="monitor unavailable")
        return HealthResponse(
            protocol_version=config.protocol_version,
            server_version=config.hub_version,
            server_time=utc_now(),
            payload=monitor.snapshot(),
        )

    @app.websocket("/v1/ws")
    async def websocket_session(websocket: WebSocket) -> None:
        await websocket.accept()
        try:
            hello = await _receive_hello(websocket, config.protocol_version)
            store.mark_seen(hello.device_id, hello.installation_nonce)
            accepted_capabilities = (
                ["device_metrics_v1"]
                if "device_metrics_v1" in hello.capabilities
                else []
            )
            await websocket.send_json(
                envelope(
                    config.protocol_version,
                    "session.ready",
                    {
                        "device_id": hello.device_id,
                        "server_version": config.hub_version,
                        "server_time": utc_now().isoformat().replace("+00:00", "Z"),
                        "accepted_capabilities": accepted_capabilities,
                    },
                )
            )
            if monitor is None:
                raise RuntimeError("monitor unavailable")
            await _stream_health(
                websocket, monitor, hello, config.protocol_version, config
            )
        except WebSocketDisconnect:
            return
        except (ValueError, json.JSONDecodeError, asyncio.TimeoutError):
            await websocket.close(code=status.WS_1002_PROTOCOL_ERROR, reason="invalid message")

    return app


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
    monitor: HealthMonitor,
    hello: DeviceHello,
    protocol_version: int,
    config: Settings,
) -> None:
    last_device_metrics_at: float | None = None
    while True:
        await websocket.send_json(
            envelope(
                protocol_version,
                "health.snapshot",
                monitor.snapshot(hello.device_id),
            )
        )
        try:
            raw = await asyncio.wait_for(
                websocket.receive_text(), timeout=config.health_sample_period_seconds
            )
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
        elif message.get("type") == "device.metrics":
            now = time.monotonic()
            if (
                last_device_metrics_at is not None
                and now - last_device_metrics_at
                < config.device_telemetry_period_seconds
            ):
                await websocket.send_json(
                    envelope(
                        protocol_version,
                        "protocol.error",
                        {
                            "code": "device_metrics_rate_limited",
                            "message": "device.metrics cadence exceeded",
                        },
                    )
                )
                continue
            try:
                frame = DeviceMetrics.model_validate(message.get("payload", {}))
            except ValueError:
                await websocket.send_json(
                    envelope(
                        protocol_version,
                        "protocol.error",
                        {
                            "code": "invalid_device_metrics",
                            "message": "device.metrics payload rejected",
                        },
                    )
                )
                continue
            try:
                monitor.record_device_metrics(
                    hello.device_id, frame.model_dump(mode="json")
                )
            except ValueError:
                await websocket.send_json(
                    envelope(
                        protocol_version,
                        "protocol.error",
                        {
                            "code": "device_metrics_rejected",
                            "message": "device.metrics could not be persisted",
                        },
                    )
                )
                continue
            last_device_metrics_at = now
        else:
            await websocket.send_json(
                envelope(
                    protocol_version,
                    "protocol.error",
                    {"code": "unknown_message", "message": "message ignored"},
                )
                )


app = create_app()
