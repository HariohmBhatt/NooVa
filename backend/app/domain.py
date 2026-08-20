"""Immutable, bounded domain records for the schema-v1 status response."""

import re
from dataclasses import dataclass
from typing import Literal

Severity = Literal["healthy", "warning", "critical"]
ReasonSeverity = Literal["warning", "critical"]
ServiceState = Literal["healthy", "warning", "critical", "unknown"]

_IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")


def _bounded_utf8(value: str, field: str, minimum: int, maximum: int) -> None:
    length = len(value.encode("utf-8"))
    if not minimum <= length <= maximum:
        raise ValueError(f"{field} must be {minimum}..{maximum} UTF-8 bytes")


def _identifier(value: str, field: str, maximum: int) -> None:
    if not _IDENTIFIER.fullmatch(value) or len(value) > maximum:
        raise ValueError(f"{field} must be a lowercase ASCII identifier")


def _optional_uint(value: int | None, field: str, maximum: int) -> None:
    if value is not None and (
        not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= maximum
    ):
        raise ValueError(f"{field} is outside its protocol range")


def _uint(value: int, field: str, maximum: int) -> None:
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= maximum:
        raise ValueError(f"{field} is outside its protocol range")


@dataclass(frozen=True, slots=True)
class Reason:
    code: str
    severity: ReasonSeverity
    message: str

    def __post_init__(self) -> None:
        _identifier(self.code, "reason code", 32)
        if self.severity not in ("warning", "critical"):
            raise ValueError("reason severity is invalid")
        _bounded_utf8(self.message, "reason message", 1, 96)

    def to_wire(self) -> dict[str, object]:
        return {"code": self.code, "severity": self.severity, "message": self.message}


@dataclass(frozen=True, slots=True)
class Metrics:
    cpu_percent_tenths: int | None
    memory_percent_tenths: int | None
    disk_percent_tenths: int | None
    uptime_seconds: int | None

    def __post_init__(self) -> None:
        for name in ("cpu_percent_tenths", "memory_percent_tenths", "disk_percent_tenths"):
            _optional_uint(getattr(self, name), name, 1000)
        _optional_uint(self.uptime_seconds, "uptime_seconds", 0xFFFFFFFF)

    def to_wire(self) -> dict[str, int | None]:
        return {
            "cpu_percent_tenths": self.cpu_percent_tenths,
            "memory_percent_tenths": self.memory_percent_tenths,
            "disk_percent_tenths": self.disk_percent_tenths,
            "uptime_seconds": self.uptime_seconds,
        }


@dataclass(frozen=True, slots=True)
class ServiceStatus:
    id: str
    name: str
    state: ServiceState

    def __post_init__(self) -> None:
        _identifier(self.id, "service id", 24)
        _bounded_utf8(self.name, "service name", 1, 24)
        if self.state not in ("healthy", "warning", "critical", "unknown"):
            raise ValueError("service state is invalid")

    def to_wire(self) -> dict[str, str]:
        return {"id": self.id, "name": self.name, "state": self.state}


@dataclass(frozen=True, slots=True)
class StatusSnapshot:
    sequence: int
    generated_at_epoch_s: int
    overall: Severity
    summary: str
    reasons: tuple[Reason, ...]
    metrics: Metrics
    services: tuple[ServiceStatus, ...]
    schema_version: int = 1

    def __post_init__(self) -> None:
        if self.schema_version != 1:
            raise ValueError("schema version must be one")
        _uint(self.sequence, "sequence", 0xFFFFFFFF)
        _uint(self.generated_at_epoch_s, "generated_at_epoch_s", 0xFFFFFFFFFFFFFFFF)
        if self.overall not in ("healthy", "warning", "critical"):
            raise ValueError("overall severity is invalid")
        _bounded_utf8(self.summary, "summary", 1, 64)
        if len(self.reasons) > 3:
            raise ValueError("snapshot permits at most three reasons")
        if len(self.services) > 4:
            raise ValueError("snapshot permits at most four services")
        if len({service.id for service in self.services}) != len(self.services):
            raise ValueError("service ids must be unique")
        if self.overall == "healthy" and self.reasons:
            raise ValueError("healthy snapshot cannot contain reasons")
        if self.overall != "healthy" and not self.reasons:
            raise ValueError("unhealthy snapshot requires a reason")
        if self.reasons and self.reasons[0].severity != self.overall:
            raise ValueError("first reason severity must equal overall")
        if self.reasons and self.summary != self.reasons[0].message:
            raise ValueError("summary must equal the first reason")
        if not self.reasons and self.summary != "All monitored systems normal":
            raise ValueError("healthy summary is invalid")

    def to_wire(self) -> dict[str, object]:
        return {
            "schema_version": self.schema_version,
            "sequence": self.sequence,
            "generated_at_epoch_s": self.generated_at_epoch_s,
            "overall": self.overall,
            "summary": self.summary,
            "reasons": [reason.to_wire() for reason in self.reasons],
            "metrics": self.metrics.to_wire(),
            "services": [service.to_wire() for service in self.services],
        }
