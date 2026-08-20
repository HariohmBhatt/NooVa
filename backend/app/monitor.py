"""Deep module that turns observations into a cached immutable snapshot."""

import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from typing import ClassVar, Protocol

from .adapters import HostObservation
from .config import Policy, ServiceDefinition
from .domain import Metrics, Reason, ServiceState, ServiceStatus, StatusSnapshot


class Observer(Protocol):
    def collect(self) -> HostObservation: ...


class ProbeClient(Protocol):
    def check(self, service: ServiceDefinition) -> bool: ...


class Clock(Protocol):
    def monotonic(self) -> float: ...

    def epoch_seconds(self) -> int: ...


class SystemClock:
    def monotonic(self) -> float:
        return time.monotonic()

    def epoch_seconds(self) -> int:
        return int(time.time())


@dataclass(slots=True)
class _Latch:
    active: bool = False
    bad: int = 0
    good: int = 0

    def update(
        self, value: int, threshold: int, clear_below: int, persist: int, clear: int
    ) -> bool:
        if value >= threshold:
            self.bad += 1
            self.good = 0
            if self.bad >= persist:
                self.active = True
        elif value < clear_below:
            self.good += 1
            self.bad = 0
            if self.good >= clear:
                self.active = False
        else:
            self.bad = 0
            self.good = 0
        return self.active


@dataclass(slots=True)
class _ServiceMemory:
    state: ServiceState = "unknown"
    failures: int = 0
    successes: int = 0

    def update(self, healthy: bool, importance: str) -> ServiceState:
        if healthy:
            self.successes += 1
            self.failures = 0
            if self.state in ("warning", "critical"):
                if self.successes >= 2:
                    self.state = "healthy"
            else:
                self.state = "healthy"
        else:
            self.failures += 1
            self.successes = 0
            if self.failures >= 2:
                self.state = "critical" if importance == "critical" else "warning"
            elif self.state == "healthy":
                self.state = "unknown"
        return self.state


class StatusMonitor:
    """Hide collection cadence, probe concurrency, policy memory, and ordering."""

    _PRIORITY: ClassVar[dict[str, int]] = {
        "monitor_collection_failed": 0,
        "disk_unavailable": 1,
        "disk_critical": 1,
        "disk_high": 1,
        "critical_service": 2,
        "memory_unavailable": 3,
        "memory_critical": 3,
        "memory_high": 3,
        "cpu_unavailable": 4,
        "cpu_critical": 4,
        "cpu_high": 4,
        "advisory_service": 5,
    }

    def __init__(
        self,
        observer: Observer,
        probes: ProbeClient,
        policy: Policy,
        services: tuple[ServiceDefinition, ...],
        clock: Clock | None = None,
        collection_interval_seconds: float = 5.0,
    ) -> None:
        if len(services) > 4:
            raise ValueError("at most four services may be monitored")
        self._observer = observer
        self._probes = probes
        self._policy = policy
        self._services = services
        self._clock = clock or SystemClock()
        self._interval = collection_interval_seconds
        self._lock = threading.Lock()
        self._snapshot: StatusSnapshot | None = None
        self._collected_at = float("-inf")
        self._sequence = 0
        self._metric_latches = {key: (_Latch(), _Latch()) for key in ("cpu", "memory")}
        self._disk_latches = (_Latch(), _Latch())
        self._service_memory = {service.id: _ServiceMemory() for service in services}

    def current(self, *, force: bool = False) -> StatusSnapshot:
        """Return one cache item, collecting at most once per configured interval."""
        with self._lock:
            now = self._clock.monotonic()
            if (
                not force
                and self._snapshot is not None
                and now - self._collected_at < self._interval
            ):
                return self._snapshot
            self._snapshot = self._collect()
            self._collected_at = now
            return self._snapshot

    def _collect(self) -> StatusSnapshot:
        try:
            observation = self._observer.collect()
            probe_results = self._run_probes()
            snapshot = self._evaluate(observation, probe_results)
        except Exception:  # noqa: BLE001 - observation boundary must fail closed.
            snapshot = self._failed_snapshot()
        self._sequence = (self._sequence + 1) & 0xFFFFFFFF
        return snapshot

    def _run_probes(self) -> dict[str, bool]:
        if not self._services:
            return {}
        with ThreadPoolExecutor(
            max_workers=min(4, len(self._services)), thread_name_prefix="probe"
        ) as pool:
            futures = {
                service.id: pool.submit(self._probes.check, service)
                for service in self._services
            }
            results: dict[str, bool] = {}
            for service in self._services:
                try:
                    results[service.id] = futures[service.id].result()
                except Exception:  # noqa: BLE001 - third-party probes are isolated.
                    # One broken target is a service failure, not a monitor failure.
                    results[service.id] = False
            return results

    def _evaluate(
        self, observation: HostObservation, probe_results: dict[str, bool]
    ) -> StatusSnapshot:
        reasons: list[tuple[Reason, str]] = []
        reasons.extend(self._metric_reasons("cpu", observation.cpu_percent_tenths))
        reasons.extend(self._metric_reasons("memory", observation.memory_percent_tenths))
        reasons.extend(self._disk_reasons(observation.disk_percent_tenths))
        service_statuses: list[ServiceStatus] = []
        for service in self._services:
            state = self._service_memory[service.id].update(
                probe_results[service.id], service.importance
            )
            service_statuses.append(ServiceStatus(service.id, service.name, state))
            if state in ("warning", "critical"):
                # `service_` plus the maximum 24-byte ID exactly fits the
                # protocol's 32-byte reason-code bound.
                code = f"service_{service.id}"
                message = f"{service.name} service check failed"
                group = (
                    "critical_service"
                    if service.importance == "critical"
                    else "advisory_service"
                )
                reasons.append((Reason(code, state, message), group))
        ordered = sorted(
            reasons,
            key=lambda item: (
                0 if item[0].severity == "critical" else 1,
                self._PRIORITY[item[1]],
            ),
        )[:3]
        selected = tuple(item[0] for item in ordered)
        overall = selected[0].severity if selected else "healthy"
        summary = selected[0].message if selected else "All monitored systems normal"
        return StatusSnapshot(
            sequence=self._sequence,
            generated_at_epoch_s=self._clock.epoch_seconds(),
            overall=overall,
            summary=summary,
            reasons=selected,
            metrics=Metrics(
                observation.cpu_percent_tenths,
                observation.memory_percent_tenths,
                observation.disk_percent_tenths,
                observation.uptime_seconds,
            ),
            services=tuple(service_statuses),
        )

    def _metric_reasons(self, name: str, value: int | None) -> list[tuple[Reason, str]]:
        if value is None:
            return [
                (
                    Reason(
                        f"{name}_unavailable", "warning", f"{name.title()} usage is unavailable"
                    ),
                    f"{name}_unavailable",
                )
            ]
        warning, critical = self._metric_latches[name]
        warning_threshold = getattr(self._policy, f"{name}_warning")
        critical_threshold = getattr(self._policy, f"{name}_critical")
        margin = self._policy.cpu_memory_clear_margin
        warning_on = warning.update(
            value,
            warning_threshold,
            warning_threshold - margin,
            self._policy.persist_collections,
            self._policy.clear_collections,
        )
        critical_on = critical.update(
            value,
            critical_threshold,
            critical_threshold - margin,
            self._policy.persist_collections,
            self._policy.clear_collections,
        )
        if critical_on:
            return [
                (
                    Reason(
                        f"{name}_critical",
                        "critical",
                        f"{name.title()} usage is critical at {value / 10:.1f}%",
                    ),
                    f"{name}_critical",
                )
            ]
        if warning_on:
            return [
                (
                    Reason(
                        f"{name}_high",
                        "warning",
                        f"{name.title()} usage is high at {value / 10:.1f}%",
                    ),
                    f"{name}_high",
                )
            ]
        return []

    def _disk_reasons(self, value: int | None) -> list[tuple[Reason, str]]:
        if value is None:
            return [
                (
                    Reason("disk_unavailable", "critical", "Disk usage is unavailable"),
                    "disk_unavailable",
                )
            ]
        warning, critical = self._disk_latches
        margin = self._policy.disk_clear_margin
        warning_on = warning.update(
            value, self._policy.disk_warning, self._policy.disk_warning - margin, 1, 1
        )
        critical_on = critical.update(
            value, self._policy.disk_critical, self._policy.disk_critical - margin, 1, 1
        )
        if critical_on:
            return [
                (
                    Reason(
                        "disk_critical",
                        "critical",
                        f"Disk usage is critical at {value / 10:.1f}%",
                    ),
                    "disk_critical",
                )
            ]
        if warning_on:
            return [
                (
                    Reason("disk_high", "warning", f"Disk usage is high at {value / 10:.1f}%"),
                    "disk_high",
                )
            ]
        return []

    def _failed_snapshot(self) -> StatusSnapshot:
        reason = Reason("monitor_collection_failed", "critical", "Status collection failed")
        return StatusSnapshot(
            sequence=self._sequence,
            generated_at_epoch_s=self._clock.epoch_seconds(),
            overall="critical",
            summary=reason.message,
            reasons=(reason,),
            metrics=Metrics(None, None, None, None),
            services=tuple(
                ServiceStatus(service.id, service.name, "unknown") for service in self._services
            ),
        )
