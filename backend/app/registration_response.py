"""Automatic device registration response model."""

from datetime import datetime

from pydantic import BaseModel


class RegistrationResponse(BaseModel):
    """Stable audit identity returned after automatic registration."""

    protocol_version: int
    device_id: str
    hub_host: str
    hub_port: int
    server_time: datetime
