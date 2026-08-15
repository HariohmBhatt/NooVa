"""Validated telemetry reported by a NOVA terminal."""

from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field, model_validator


MAX_SEQUENCE = 2**32 - 1
MAX_UPTIME_SECONDS = 100 * 365 * 24 * 60 * 60
MIN_WIFI_RSSI_DBM = -127
MAX_WIFI_RSSI_DBM = 0
MAX_FREE_HEAP_BYTES = 64 * 1024 * 1024
MAX_DEVICE_METRICS_PAYLOAD_BYTES = 2048


class AudioState(StrEnum):
    """Finite set of audio states accepted from a terminal."""

    UNAVAILABLE = "unavailable"
    IDLE = "idle"
    LISTENING = "listening"
    SPEAKING = "speaking"
    ERROR = "error"


class DeviceMetrics(BaseModel):
    """A bounded, strictly typed ``device.metrics`` payload."""

    model_config = ConfigDict(extra="forbid", strict=True)

    sequence: int = Field(ge=0, le=MAX_SEQUENCE)
    uptime_seconds: float = Field(ge=0, le=MAX_UPTIME_SECONDS)
    wifi_rssi_dbm: int = Field(ge=MIN_WIFI_RSSI_DBM, le=MAX_WIFI_RSSI_DBM)
    free_heap_bytes: int = Field(ge=0, le=MAX_FREE_HEAP_BYTES)
    touch_ready: bool
    touch_active: bool
    audio_state: AudioState = Field(strict=False)

    @model_validator(mode="after")
    def payload_is_bounded(self) -> "DeviceMetrics":
        """Reject serialized payloads larger than the protocol budget."""
        if len(self.model_dump_json().encode("utf-8")) > MAX_DEVICE_METRICS_PAYLOAD_BYTES:
            raise ValueError("device.metrics payload exceeds the size limit")
        return self


# Compatibility name for the WebSocket integration while the protocol layer
# transitions from the generic frame terminology to the domain model name.
DeviceMetricsFrame = DeviceMetrics
