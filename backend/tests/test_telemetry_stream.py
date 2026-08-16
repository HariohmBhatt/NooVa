import asyncio

from app.config import Settings
from app.main import _TelemetrySnapshotCache, _telemetry_items
from app.metrics import HostMetricsCollector
from app.telemetry_stream import (
    FRAME_SIZE,
    GPU_AVAILABLE_FLAG,
    SERVER_ITEM_KIND,
    decode_frame,
    encode_server_item,
)


def test_stream_generator_emits_a_decodable_server_item() -> None:
    async def read_first_item() -> bytes:
        config = Settings(telemetry_interval_seconds=1.0)
        generator = _telemetry_items(
            config, _TelemetrySnapshotCache(config, HostMetricsCollector())
        )
        try:
            return await anext(generator)
        finally:
            await generator.aclose()

    raw = asyncio.run(read_first_item())

    assert decode_frame(raw).kind == SERVER_ITEM_KIND


def test_server_item_is_fixed_width_and_round_trips() -> None:
    raw = encode_server_item(
        7,
        {
            "cpu_percent": 12.3,
            "gpu_state": "available",
            "gpu_utilization_percent": 45.6,
            "gpu_temperature_c": 67.8,
            "gpu_vram_used_bytes": 123,
            "gpu_vram_total_bytes": 456,
            "uptime_seconds": 99,
            "collection_errors": [],
            "dependency_status": "healthy",
        },
        1_700_000_000,
    )

    decoded = decode_frame(raw)

    assert len(raw) == FRAME_SIZE
    assert decoded.sequence == 7
    assert decoded.timestamp_seconds == 1_700_000_000
    assert decoded.cpu_tenths == 123
    assert decoded.gpu_utilization_tenths == 456
    assert decoded.gpu_temperature_tenths == 678
    assert decoded.flags & GPU_AVAILABLE_FLAG
    assert decoded.metric_a == 123
    assert decoded.metric_b == 456


def test_invalid_frame_checksum_is_rejected() -> None:
    raw = bytearray(
        encode_server_item(
            1,
            {"cpu_percent": 1.0, "dependency_status": "healthy"},
            1,
        )
    )
    raw[20] ^= 0x01

    try:
        decode_frame(bytes(raw))
    except ValueError as error:
        assert "checksum" in str(error)
    else:
        raise AssertionError("corrupt frame was accepted")


def test_snapshot_cache_collects_once_within_the_interval() -> None:
    class CountingMetrics:
        def __init__(self) -> None:
            self.calls = 0

        def collect(self) -> dict[str, object]:
            self.calls += 1
            return {"collection_errors": [], "dependency_status": "healthy"}

    config = Settings(telemetry_interval_seconds=10.0)
    metrics = CountingMetrics()
    cache = _TelemetrySnapshotCache(config, metrics)  # type: ignore[arg-type]

    assert cache.current() == cache.current()
    assert metrics.calls == 1
