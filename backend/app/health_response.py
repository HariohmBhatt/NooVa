"""One-shot health response model."""

from datetime import datetime
from typing import Any

from pydantic import BaseModel


class HealthResponse(BaseModel):
    """Unauthenticated one-shot health response for diagnostics clients."""

    protocol_version: int
    server_version: str
    server_time: datetime
    payload: dict[str, Any]
