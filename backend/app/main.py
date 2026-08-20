"""FastAPI transport adapter for the cached NOVA status snapshot."""

import asyncio
import hmac
import json
import time
from collections import deque
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager, suppress
from typing import Protocol

from fastapi import FastAPI, Request
from fastapi.responses import Response

from .adapters import HttpProbeClient, LinuxObserver
from .config import Settings
from .domain import StatusSnapshot
from .monitor import StatusMonitor

_MAX_STATUS_BYTES = 4096
_MAX_ERROR_BYTES = 512


class Monitor(Protocol):
    def current(self) -> StatusSnapshot: ...


class _SnapshotScheduler:
    """Refresh on a timer so HTTP requests never initiate expensive host work."""

    def __init__(self, monitor: Monitor, interval_seconds: float) -> None:
        self._monitor = monitor
        self._interval = interval_seconds
        self.snapshot: StatusSnapshot | None = None
        self.failed = False

    async def refresh(self) -> None:
        try:
            self.snapshot = await asyncio.to_thread(self._monitor.current)
            self.failed = False
        except Exception:  # noqa: BLE001 - scheduler must survive unknown adapters.
            # Adapter messages can contain paths or URLs, so they are not exposed.
            self.failed = True

    async def run(self) -> None:
        while True:
            await asyncio.sleep(self._interval)
            await self.refresh()


class _RateLimiter:
    """Small process-local limiter; one backend instance serves provisioned devices."""

    def __init__(self, requests_per_minute: int) -> None:
        self._limit = requests_per_minute
        self._accepted: deque[float] = deque()

    def accept(self) -> bool:
        now = time.monotonic()
        while self._accepted and now - self._accepted[0] >= 60:
            self._accepted.popleft()
        if len(self._accepted) >= self._limit:
            return False
        self._accepted.append(now)
        return True


def create_app(settings: Settings | None = None, monitor: Monitor | None = None) -> FastAPI:
    config = settings or Settings.from_env()
    status_monitor = monitor or StatusMonitor(
        LinuxObserver(config.proc_root, config.disk_root),
        HttpProbeClient(),
        config.policy,
        config.services,
        collection_interval_seconds=config.collection_interval_seconds,
    )
    scheduler = _SnapshotScheduler(status_monitor, config.collection_interval_seconds)
    rate_limiter = _RateLimiter(config.rate_limit_per_minute)

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        # Produce the first item before accepting traffic, then keep collection
        # independent of the five-second device polling cycle.
        await scheduler.refresh()
        task = asyncio.create_task(scheduler.run(), name="status-snapshot-scheduler")
        try:
            yield
        finally:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task

    app = FastAPI(
        title="NOVA Sentinel API",
        version="0.1.0",
        docs_url=None,
        redoc_url=None,
        openapi_url=None,
        lifespan=lifespan,
    )

    @app.get("/healthz", include_in_schema=False)
    async def healthz() -> Response:
        if scheduler.failed or scheduler.snapshot is None:
            return _error(
                503, "snapshot_unavailable", "No current status snapshot is available"
            )
        return _json_response(200, {"status": "ok"})

    @app.get("/v1/status", include_in_schema=False)
    async def status(request: Request) -> Response:
        authorization = request.headers.get("authorization", "")
        expected = f"Bearer {config.device_token}"
        if not hmac.compare_digest(authorization.encode(), expected.encode()):
            return _error(401, "unauthorized", "Device credentials were rejected")
        if request.headers.get("x-nova-schema") != "1":
            return _error(426, "unsupported_schema", "Only NOVA schema 1 is supported")
        if request.headers.get("accept") != "application/json":
            return _error(406, "not_acceptable", "Accept application/json is required")
        if not rate_limiter.accept():
            return _error(
                429,
                "rate_limited",
                "Status request rate exceeded",
                headers={"Retry-After": "60"},
            )
        if scheduler.failed or scheduler.snapshot is None:
            return _error(
                503, "snapshot_unavailable", "No current status snapshot is available"
            )
        encoded = _encode(scheduler.snapshot.to_wire())
        if len(encoded) > _MAX_STATUS_BYTES:
            return _error(
                503, "snapshot_too_large", "Status snapshot exceeded its protocol bound"
            )
        return Response(
            content=encoded,
            status_code=200,
            media_type="application/json",
            headers={"Cache-Control": "no-store"},
        )

    return app


def _error(
    status_code: int,
    code: str,
    message: str,
    headers: dict[str, str] | None = None,
) -> Response:
    encoded = _encode({"schema_version": 1, "error": {"code": code, "message": message}})
    if len(encoded) > _MAX_ERROR_BYTES:  # Defensive assertion for future error additions.
        raise RuntimeError("protocol error response exceeds 512 bytes")
    return Response(
        content=encoded,
        status_code=status_code,
        media_type="application/json",
        headers={"Cache-Control": "no-store", **(headers or {})},
    )


def _json_response(status_code: int, payload: dict[str, object]) -> Response:
    return Response(
        content=_encode(payload),
        status_code=status_code,
        media_type="application/json",
        headers={"Cache-Control": "no-store"},
    )


def _encode(payload: object) -> bytes:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
