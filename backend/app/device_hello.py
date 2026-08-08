"""Terminal WebSocket hello model."""

from pydantic import BaseModel, Field


class DeviceHello(BaseModel):
    """Unauthenticated first message on a terminal WebSocket session."""

    device_id: str
    installation_nonce: str = Field(min_length=16, max_length=128)
    firmware: str = Field(min_length=1, max_length=64)
    capabilities: list[str] = Field(default_factory=list, max_length=32)
