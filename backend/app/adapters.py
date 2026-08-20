"""Read-only Linux and HTTP observation adapters."""

import os
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

from .config import ServiceDefinition


@dataclass(frozen=True, slots=True)
class HostObservation:
    """Raw normalized observations; percentages use integer tenths."""

    cpu_percent_tenths: int | None
    memory_percent_tenths: int | None
    disk_percent_tenths: int | None
    uptime_seconds: int | None


class LinuxObserver:
    """Read the host through explicitly mounted, read-only proc/filesystem paths."""

    def __init__(self, proc_root: Path, disk_root: Path) -> None:
        self._proc_root = proc_root
        self._disk_root = disk_root
        self._previous_cpu: tuple[int, int] | None = None

    def collect(self) -> HostObservation:
        return HostObservation(
            self._cpu(),
            self._memory(),
            self._disk(),
            self._uptime(),
        )

    def _cpu(self) -> int | None:
        try:
            fields = (
                (self._proc_root / "stat").read_text(encoding="ascii").splitlines()[0].split()
            )
            values = [int(value) for value in fields[1:]]
            total = sum(values)
            idle = values[3] + (values[4] if len(values) > 4 else 0)
            previous = self._previous_cpu
            self._previous_cpu = (total, idle)
            if previous is None:
                total_delta, idle_delta = total, idle
            else:
                total_delta, idle_delta = total - previous[0], idle - previous[1]
            if total_delta <= 0:
                return None
            return _tenths(1000 * (total_delta - idle_delta) / total_delta)
        except (OSError, ValueError, IndexError):
            return None

    def _memory(self) -> int | None:
        try:
            values: dict[str, int] = {}
            for line in (self._proc_root / "meminfo").read_text(encoding="ascii").splitlines():
                key, raw = line.split(":", 1)
                values[key] = int(raw.strip().split()[0])
            return _tenths(
                1000 * (values["MemTotal"] - values["MemAvailable"]) / values["MemTotal"]
            )
        except (OSError, ValueError, KeyError, IndexError, ZeroDivisionError):
            return None

    def _disk(self) -> int | None:
        try:
            usage = os.statvfs(self._disk_root)
            total = usage.f_blocks * usage.f_frsize
            available = usage.f_bavail * usage.f_frsize
            if total <= 0:
                return None
            return _tenths(1000 * (total - available) / total)
        except OSError:
            return None

    def _uptime(self) -> int | None:
        try:
            value = int(
                float((self._proc_root / "uptime").read_text(encoding="ascii").split()[0])
            )
            return min(max(value, 0), 0xFFFFFFFF)
        except (OSError, ValueError, IndexError):
            return None


def _tenths(value: float) -> int:
    return min(max(round(value), 0), 1000)


class HttpProbeClient:
    """Perform one bounded read-only service request without retaining a body."""

    def check(self, service: ServiceDefinition) -> bool:
        request = urllib.request.Request(
            service.url,
            method="GET",
            headers={"User-Agent": "NOVA-Sentinel/0.1", "Accept": "*/*"},
        )
        try:
            # A redirect is itself a healthy 3xx observation. Do not follow it
            # into a different endpoint and accidentally report that endpoint.
            opener = urllib.request.build_opener(_NoRedirectHandler())
            with opener.open(request, timeout=service.timeout_seconds) as response:
                return 200 <= response.status < 400
        except urllib.error.HTTPError as error:
            return 200 <= error.code < 400
        except (OSError, ValueError):
            return False


class _NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(  # type: ignore[no-untyped-def]
        self, request, file_pointer, code, message, headers, new_url
    ) -> None:
        return None
