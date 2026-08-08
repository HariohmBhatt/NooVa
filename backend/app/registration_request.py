"""Automatic device registration request model."""

from pydantic import BaseModel, Field


class RegistrationRequest(BaseModel):
    """Terminal request to register itself with the local hub."""

    protocol_version: int = Field(ge=1)
    installation_nonce: str = Field(min_length=16, max_length=128)
    firmware: str = Field(min_length=1, max_length=64)
    capabilities: list[str] = Field(default_factory=list, max_length=32)
