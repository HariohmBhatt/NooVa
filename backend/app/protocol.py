"""Protocol envelope helpers."""

from datetime import datetime, timezone
from typing import Any


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def envelope(
    protocol_version: int, message_type: str, payload: dict[str, Any]
) -> dict[str, Any]:
    return {
        "protocol_version": protocol_version,
        "type": message_type,
        "timestamp": utc_now().isoformat().replace("+00:00", "Z"),
        "payload": payload,
    }
