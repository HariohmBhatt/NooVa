"""Fixed-width binary telemetry items for low-overhead device streaming."""

from dataclasses import dataclass
import math
import struct
from typing import Any
import zlib


STREAM_MAGIC = b"NVT1"
STREAM_VERSION = 1
SERVER_ITEM_KIND = 1
STREAM_CONTENT_TYPE = "application/x-nova-telemetry"
GPU_AVAILABLE_FLAG = 1 << 0
DEGRADED_FLAG = 1 << 1
FRAME_FORMAT = "<4sBBHIQhhhHQQII8sI"
FRAME_SIZE = struct.calcsize(FRAME_FORMAT)
_FRAME_WITHOUT_CRC_FORMAT = "<4sBBHIQhhhHQQII8s"
_FRAME_WITHOUT_CRC_SIZE = struct.calcsize(_FRAME_WITHOUT_CRC_FORMAT)


@dataclass(frozen=True)
class TelemetryFrame:
    """Decoded fixed-width item crossing the server/device seam."""

    kind: int
    sequence: int
    timestamp_seconds: int
    cpu_tenths: int
    gpu_utilization_tenths: int
    gpu_temperature_tenths: int
    flags: int
    metric_a: int
    metric_b: int
    uptime_seconds: int
    error_count: int


def encode_server_item(
    sequence: int, payload: dict[str, Any], timestamp_seconds: int
) -> bytes:
    """Encode one server health snapshot without JSON or string fields."""

    gpu_available = payload.get("gpu_state") == "available"
    flags = GPU_AVAILABLE_FLAG if gpu_available else 0
    if payload.get("dependency_status") != "healthy":
        flags |= DEGRADED_FLAG
    collection_errors = payload.get("collection_errors")
    error_count = (
        len(collection_errors)
        if isinstance(collection_errors, (list, tuple))
        else 0
    )
    body = struct.pack(
        _FRAME_WITHOUT_CRC_FORMAT,
        STREAM_MAGIC,
        STREAM_VERSION,
        SERVER_ITEM_KIND,
        FRAME_SIZE,
        sequence & 0xFFFFFFFF,
        max(0, int(timestamp_seconds)),
        _scaled(payload.get("cpu_percent")),
        _scaled(payload.get("gpu_utilization_percent")),
        _scaled(payload.get("gpu_temperature_c")),
        flags,
        _unsigned(payload.get("gpu_vram_used_bytes")),
        _unsigned(payload.get("gpu_vram_total_bytes")),
        _unsigned(payload.get("uptime_seconds")),
        error_count,
        b"\0" * 8,
    )
    checksum = zlib.crc32(body) & 0xFFFFFFFF
    return body + struct.pack("<I", checksum)


def decode_frame(raw: bytes) -> TelemetryFrame:
    """Validate and decode one complete frame for protocol tests/tools."""

    if len(raw) != FRAME_SIZE:
        raise ValueError("telemetry frame has an unexpected size")
    fields = struct.unpack(FRAME_FORMAT, raw)
    if fields[0] != STREAM_MAGIC or fields[1] != STREAM_VERSION:
        raise ValueError("telemetry frame header is unsupported")
    if fields[3] != FRAME_SIZE:
        raise ValueError("telemetry frame length is unsupported")
    expected = zlib.crc32(raw[:_FRAME_WITHOUT_CRC_SIZE]) & 0xFFFFFFFF
    if fields[-1] != expected:
        raise ValueError("telemetry frame checksum failed")
    return TelemetryFrame(
        kind=fields[2],
        sequence=fields[4],
        timestamp_seconds=fields[5],
        cpu_tenths=fields[6],
        gpu_utilization_tenths=fields[7],
        gpu_temperature_tenths=fields[8],
        flags=fields[9],
        metric_a=fields[10],
        metric_b=fields[11],
        uptime_seconds=fields[12],
        error_count=fields[13],
    )


def _scaled(value: object) -> int:
    if value is None:
        return -1
    try:
        number = float(value)
    except (TypeError, ValueError):
        return -1
    if not math.isfinite(number):
        return -1
    return max(-32768, min(32767, int(round(number * 10))))


def _unsigned(value: object) -> int:
    if value is None:
        return 0
    try:
        number = int(value)
    except (TypeError, ValueError):
        return 0
    return max(0, min(0xFFFFFFFFFFFFFFFF, number))
