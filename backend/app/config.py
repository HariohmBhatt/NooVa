"""Environment-backed hub configuration."""

from dataclasses import dataclass
import os
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    """Runtime settings for the hub API and monitoring modules."""

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
    health_sample_period_seconds: float = 5.0
    metrics_history_period_seconds: int = 60
    metrics_history_retention_days: int = 30
    health_trend_points: int = 12
    device_telemetry_period_seconds: float = 15.0
    device_offline_after_seconds: int = 45

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
            health_sample_period_seconds=float(
                os.getenv("NOVA_HEALTH_SAMPLE_SECONDS", "5")
            ),
            metrics_history_period_seconds=int(
                os.getenv("NOVA_METRICS_HISTORY_PERIOD_SECONDS", "60")
            ),
            metrics_history_retention_days=int(
                os.getenv("NOVA_METRICS_HISTORY_RETENTION_DAYS", "30")
            ),
            health_trend_points=int(os.getenv("NOVA_HEALTH_TREND_POINTS", "12")),
            device_telemetry_period_seconds=float(
                os.getenv("NOVA_DEVICE_TELEMETRY_SECONDS", "15")
            ),
            device_offline_after_seconds=int(
                os.getenv("NOVA_DEVICE_OFFLINE_SECONDS", "45")
            ),
        )
