"""Read-only host metrics for the terminal health stream."""

from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import time


@dataclass
class _NetworkSample:
    received: int
    transmitted: int
    measured_at: float


class HostMetricsCollector:
    """Collect current host metrics without retaining a time series."""

    def __init__(
        self,
        proc_root: Path = Path("/proc"),
        sys_root: Path = Path("/sys"),
        disk_root: Path = Path("/"),
        interface: str | None = None,
    ) -> None:
        self.proc_root = proc_root
        self.sys_root = sys_root
        self.disk_root = disk_root
        self.configured_interface = interface
        self._cpu_sample: tuple[int, int] | None = None
        self._network_sample: _NetworkSample | None = None

    def collect(self) -> dict[str, object]:
        errors: list[str] = []
        metrics: dict[str, object] = {}
        self._collect_cpu(metrics, errors)
        self._collect_memory(metrics, errors)
        self._collect_disk(metrics, errors)
        self._collect_network(metrics, errors)
        self._collect_uptime(metrics, errors)
        metrics["collection_errors"] = errors
        metrics["dependency_status"] = "degraded" if errors else "healthy"
        return metrics

    def _collect_cpu(self, metrics: dict[str, object], errors: list[str]) -> None:
        try:
            fields = self._read_text(self.proc_root / "stat").splitlines()[0].split()
            values = [int(value) for value in fields[1:]
                      if value.isdigit()]
            idle = values[3] + (values[4] if len(values) > 4 else 0)
            total = sum(values)
            if self._cpu_sample is None:
                load = float(self._read_text(self.proc_root / "loadavg").split()[0])
                metrics["cpu_percent"] = round(min(100.0, load / max(os.cpu_count() or 1, 1) * 100), 1)
            else:
                previous_total, previous_idle = self._cpu_sample
                total_delta = total - previous_total
                idle_delta = idle - previous_idle
                metrics["cpu_percent"] = round(
                    max(0.0, min(100.0, (total_delta - idle_delta) / max(total_delta, 1) * 100)),
                    1,
                )
            self._cpu_sample = (total, idle)
        except (OSError, IndexError, ValueError) as error:
            errors.append(f"cpu:{type(error).__name__}")

    def _collect_memory(self, metrics: dict[str, object], errors: list[str]) -> None:
        try:
            values: dict[str, int] = {}
            for line in self._read_text(self.proc_root / "meminfo").splitlines():
                name, raw = line.split(":", 1)
                values[name] = int(raw.strip().split()[0]) * 1024
            total = values["MemTotal"]
            available = values["MemAvailable"]
            metrics["memory_total_bytes"] = total
            metrics["memory_used_bytes"] = total - available
        except (OSError, KeyError, IndexError, ValueError) as error:
            errors.append(f"memory:{type(error).__name__}")

    def _collect_disk(self, metrics: dict[str, object], errors: list[str]) -> None:
        try:
            usage = shutil.disk_usage(self.disk_root)
            metrics["disk_total_bytes"] = usage.total
            metrics["disk_used_bytes"] = usage.used
        except OSError as error:
            errors.append(f"disk:{type(error).__name__}")

    def _collect_network(self, metrics: dict[str, object], errors: list[str]) -> None:
        try:
            interface = self.configured_interface or self._default_interface()
            received, transmitted = self._interface_bytes(interface)
            now = time.monotonic()
            metrics["network_interface"] = interface
            metrics["network_rx_bytes_total"] = received
            metrics["network_tx_bytes_total"] = transmitted
            if self._network_sample is not None:
                elapsed = max(now - self._network_sample.measured_at, 0.001)
                metrics["network_rx_bytes_per_second"] = round(
                    max(0, received - self._network_sample.received) / elapsed, 1
                )
                metrics["network_tx_bytes_per_second"] = round(
                    max(0, transmitted - self._network_sample.transmitted) / elapsed, 1
                )
            else:
                metrics["network_rx_bytes_per_second"] = 0.0
                metrics["network_tx_bytes_per_second"] = 0.0
            self._network_sample = _NetworkSample(received, transmitted, now)
        except (OSError, KeyError, IndexError, ValueError) as error:
            errors.append(f"network:{type(error).__name__}")

    def _collect_uptime(self, metrics: dict[str, object], errors: list[str]) -> None:
        try:
            metrics["uptime_seconds"] = float(
                self._read_text(self.proc_root / "uptime").split()[0]
            )
        except (OSError, IndexError, ValueError) as error:
            errors.append(f"uptime:{type(error).__name__}")

    def _default_interface(self) -> str:
        routes = self._read_text(self.proc_root / "net/route").splitlines()[1:]
        candidates = []
        for line in routes:
            fields = line.split()
            if len(fields) >= 4 and fields[1] == "00000000" and int(fields[3], 16) & 2:
                candidates.append((int(fields[6]), fields[0]))
        for _, interface in sorted(candidates):
            if (self.sys_root / "class/net" / interface).exists():
                return interface
        interface_root = self.sys_root / "class/net"
        if candidates and not interface_root.exists():
            return sorted(candidates)[0][1]
        physical = []
        for path in interface_root.glob("*"):
            name = path.name
            if name == "lo" or name.startswith(("br-", "docker", "veth", "tailscale")):
                continue
            if (path / "device").exists():
                physical.append(name)
        if physical:
            return sorted(physical)[0]
        raise OSError("default route unavailable")

    def _interface_bytes(self, interface: str) -> tuple[int, int]:
        statistics = self.sys_root / "class/net" / interface / "statistics"
        rx_path = statistics / "rx_bytes"
        tx_path = statistics / "tx_bytes"
        if rx_path.exists() and tx_path.exists():
            return int(rx_path.read_text(encoding="utf-8")), int(
                tx_path.read_text(encoding="utf-8")
            )
        for line in self._read_text(self.proc_root / "net/dev").splitlines()[2:]:
            if ":" not in line:
                continue
            name, raw = line.split(":", 1)
            if name.strip() != interface:
                continue
            fields = raw.split()
            return int(fields[0]), int(fields[8])
        raise KeyError(interface)

    @staticmethod
    def _read_text(path: Path) -> str:
        return path.read_text(encoding="utf-8")
