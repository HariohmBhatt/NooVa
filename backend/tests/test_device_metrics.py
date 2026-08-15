import pytest
from pydantic import ValidationError

from app.device_metrics import (
    MAX_FREE_HEAP_BYTES,
    MAX_SEQUENCE,
    DeviceMetrics,
)


def _valid_payload() -> dict[str, object]:
    return {
        "sequence": 7,
        "uptime_seconds": 1842.5,
        "wifi_rssi_dbm": -57,
        "free_heap_bytes": 191240,
        "touch_ready": True,
        "touch_active": False,
        "audio_state": "idle",
    }


def test_device_metrics_accepts_valid_payload_and_forbids_extra_fields() -> None:
    metrics = DeviceMetrics.model_validate(_valid_payload())

    assert metrics.sequence == 7
    assert metrics.audio_state.value == "idle"
    with pytest.raises(ValidationError):
        DeviceMetrics.model_validate({**_valid_payload(), "unexpected": True})


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("sequence", -1),
        ("sequence", MAX_SEQUENCE + 1),
        ("uptime_seconds", -0.1),
        ("wifi_rssi_dbm", -128),
        ("wifi_rssi_dbm", 1),
        ("free_heap_bytes", -1),
        ("free_heap_bytes", MAX_FREE_HEAP_BYTES + 1),
        ("audio_state", "playing"),
    ],
)
def test_device_metrics_rejects_out_of_range_values(field: str, value: object) -> None:
    with pytest.raises(ValidationError):
        DeviceMetrics.model_validate({**_valid_payload(), field: value})


def test_device_metrics_uses_strict_boolean_fields() -> None:
    with pytest.raises(ValidationError):
        DeviceMetrics.model_validate({**_valid_payload(), "touch_ready": 1})
