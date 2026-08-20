"""Validated configuration shared by monitoring and deployment adapters."""

import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urlsplit


@dataclass(frozen=True, slots=True)
class Policy:
    cpu_warning: int = 850
    cpu_critical: int = 950
    memory_warning: int = 850
    memory_critical: int = 950
    disk_warning: int = 800
    disk_critical: int = 900
    persist_collections: int = 3
    clear_collections: int = 2
    cpu_memory_clear_margin: int = 50
    disk_clear_margin: int = 20

    def __post_init__(self) -> None:
        for prefix in ("cpu", "memory", "disk"):
            warning = getattr(self, f"{prefix}_warning")
            critical = getattr(self, f"{prefix}_critical")
            if not 0 <= warning < critical <= 1000:
                raise ValueError(
                    f"{prefix} thresholds must satisfy 0 <= warning < critical <= 1000"
                )
        if self.persist_collections < 1 or self.clear_collections < 1:
            raise ValueError("persistence counts must be positive")
        if not 0 <= self.cpu_memory_clear_margin <= 200:
            raise ValueError("CPU/memory clear margin must be 0..200")
        if not 0 <= self.disk_clear_margin <= 200:
            raise ValueError("disk clear margin must be 0..200")


@dataclass(frozen=True, slots=True)
class ServiceDefinition:
    id: str
    name: str
    url: str
    importance: str
    timeout_seconds: float

    def __post_init__(self) -> None:
        if not re.fullmatch(r"[a-z][a-z0-9_]{0,23}", self.id):
            raise ValueError("service id must be a lowercase ASCII identifier")
        if not 1 <= len(self.name.encode("utf-8")) <= 24:
            raise ValueError("service name must be 1..24 UTF-8 bytes")
        parsed = urlsplit(self.url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            raise ValueError("service URL must be absolute HTTP(S)")
        if parsed.username or parsed.password:
            raise ValueError("service URL must not contain credentials")
        if self.importance not in ("critical", "advisory"):
            raise ValueError("service importance must be critical or advisory")
        if not 0 < self.timeout_seconds <= 1.5:
            raise ValueError(
                "service timeout must be greater than zero and at most 1.5 seconds"
            )


@dataclass(frozen=True, slots=True)
class Settings:
    device_token: str
    services: tuple[ServiceDefinition, ...] = ()
    policy: Policy = Policy()
    collection_interval_seconds: float = 5.0
    rate_limit_per_minute: int = 30
    lan_port: int = 443
    proc_root: Path = Path("/proc")
    disk_root: Path = Path("/")

    def __post_init__(self) -> None:
        if len(self.device_token.encode("utf-8")) < 32:
            raise ValueError("NOVA_DEVICE_TOKEN must contain at least 32 bytes")
        if not 0.5 <= self.collection_interval_seconds <= 60:
            raise ValueError("collection interval must be 0.5..60 seconds")
        if not 1 <= self.rate_limit_per_minute <= 600:
            raise ValueError("rate limit must be 1..600 requests per minute")
        if not 1 <= self.lan_port <= 65535:
            raise ValueError("LAN port must be 1..65535")
        if len(self.services) > 4:
            raise ValueError("at most four services may be configured")
        if len({service.id for service in self.services}) != len(self.services):
            raise ValueError("configured service ids must be unique")

    @classmethod
    def from_env(cls) -> "Settings":
        def integer(name: str, default: int) -> int:
            try:
                return int(os.environ.get(name, str(default)))
            except ValueError as error:
                raise ValueError(f"{name} must be an integer") from error

        policy = Policy(
            cpu_warning=integer("NOVA_CPU_WARNING_TENTHS", 850),
            cpu_critical=integer("NOVA_CPU_CRITICAL_TENTHS", 950),
            memory_warning=integer("NOVA_MEMORY_WARNING_TENTHS", 850),
            memory_critical=integer("NOVA_MEMORY_CRITICAL_TENTHS", 950),
            disk_warning=integer("NOVA_DISK_WARNING_TENTHS", 800),
            disk_critical=integer("NOVA_DISK_CRITICAL_TENTHS", 900),
        )
        try:
            interval = float(os.environ.get("NOVA_COLLECTION_INTERVAL_SECONDS", "5"))
        except ValueError as error:
            raise ValueError("NOVA_COLLECTION_INTERVAL_SECONDS must be numeric") from error
        services = _parse_services(os.environ.get("NOVA_SERVICES_JSON", "[]"))
        return cls(
            device_token=os.environ.get("NOVA_DEVICE_TOKEN", ""),
            services=services,
            policy=policy,
            collection_interval_seconds=interval,
            rate_limit_per_minute=integer("NOVA_RATE_LIMIT_PER_MINUTE", 30),
            lan_port=integer("NOVA_LAN_PORT", 443),
            proc_root=Path(os.environ.get("NOVA_PROC_ROOT", "/proc")),
            disk_root=Path(os.environ.get("NOVA_DISK_ROOT", "/")),
        )


def _parse_services(raw: str) -> tuple[ServiceDefinition, ...]:
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as error:
        raise ValueError("NOVA_SERVICES_JSON must be valid JSON") from error
    if not isinstance(parsed, list):
        raise ValueError("NOVA_SERVICES_JSON must be an array")  # noqa: TRY004
    allowed = {"id", "name", "url", "importance", "timeout_seconds"}
    services: list[ServiceDefinition] = []
    for item in parsed:
        if not isinstance(item, dict) or set(item) != allowed:
            raise ValueError(f"each service must contain exactly {sorted(allowed)}")
        try:
            service = ServiceDefinition(
                id=item["id"],
                name=item["name"],
                url=item["url"],
                importance=item["importance"],
                timeout_seconds=float(item["timeout_seconds"]),
            )
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError("service configuration is invalid") from error
        services.append(service)
    return tuple(services)
