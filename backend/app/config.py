"""Environment-backed hub configuration."""

from dataclasses import dataclass
import os
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    """Runtime settings for the hub API and host metrics collector."""

    db_path: Path = Path("data/nova.db")
    hub_version: str = "0.1.0"
    protocol_version: int = 1
    hub_host: str = "nova-hub.local"
    hub_port: int = 443
    home_timezone: str = "Asia/Kolkata"
    metrics_proc_root: Path = Path("/proc")
    metrics_sys_root: Path = Path("/sys")
    metrics_disk_root: Path = Path("/")
    metrics_interface: str | None = None

    @classmethod
    def from_env(cls) -> "Settings":
        interface = os.getenv("NOVA_METRICS_INTERFACE") or None
        return cls(
            db_path=Path(os.getenv("NOVA_DB_PATH", "data/nova.db")),
            hub_version=os.getenv("NOVA_HUB_VERSION", "0.1.0"),
            protocol_version=int(os.getenv("NOVA_PROTOCOL_VERSION", "1")),
            hub_host=os.getenv("NOVA_HUB_HOST", "nova-hub.local"),
            hub_port=int(os.getenv("NOVA_HUB_PORT", "443")),
            home_timezone=os.getenv("NOVA_HOME_TIMEZONE", "Asia/Kolkata"),
            metrics_proc_root=Path(os.getenv("NOVA_PROC_ROOT", "/proc")),
            metrics_sys_root=Path(os.getenv("NOVA_SYS_ROOT", "/sys")),
            metrics_disk_root=Path(os.getenv("NOVA_DISK_ROOT", "/")),
            metrics_interface=interface,
        )
