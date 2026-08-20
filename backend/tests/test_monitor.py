import threading
import time
from collections.abc import Iterable

from app.adapters import HostObservation
from app.config import Policy, ServiceDefinition
from app.monitor import StatusMonitor


class ManualClock:
    def __init__(self) -> None:
        self.monotonic_value = 0.0
        self.epoch_value = 1_700_000_000

    def monotonic(self) -> float:
        return self.monotonic_value

    def epoch_seconds(self) -> int:
        return self.epoch_value + int(self.monotonic_value)

    def advance(self, seconds: float = 5.0) -> None:
        self.monotonic_value += seconds


class ScriptedObserver:
    def __init__(self, values: Iterable[HostObservation | Exception]) -> None:
        self.values = iter(values)
        self.calls = 0

    def collect(self) -> HostObservation:
        self.calls += 1
        value = next(self.values)
        if isinstance(value, Exception):
            raise value
        return value


class ScriptedProbes:
    def __init__(self, results: dict[str, list[bool | None]]) -> None:
        self.results = results

    def check(self, service: ServiceDefinition) -> bool | None:
        return self.results[service.id].pop(0)


def observation(
    cpu: int | None = 100,
    memory: int | None = 200,
    disk: int | None = 300,
    uptime: int | None = 60,
) -> HostObservation:
    return HostObservation(cpu, memory, disk, uptime)


def next_snapshot(monitor: StatusMonitor, clock: ManualClock):
    snapshot = monitor.current()
    clock.advance()
    return snapshot


def test_monitor_caches_collection_and_applies_cpu_persistence_and_clear_hysteresis() -> None:
    clock = ManualClock()
    observer = ScriptedObserver([observation(cpu=value) for value in (850, 850, 850, 790, 790)])
    monitor = StatusMonitor(observer, ScriptedProbes({}), Policy(), (), clock)

    first = monitor.current()
    assert monitor.current() is first
    assert observer.calls == 1
    clock.advance()

    assert next_snapshot(monitor, clock).overall == "healthy"
    warning = next_snapshot(monitor, clock)
    assert warning.overall == "warning"
    assert warning.reasons[0].code == "cpu_high"
    assert next_snapshot(monitor, clock).overall == "warning"
    assert next_snapshot(monitor, clock).overall == "healthy"


def test_monitor_prioritizes_integrity_disk_and_services_and_limits_reasons() -> None:
    clock = ManualClock()
    services = (
        ServiceDefinition("core", "Core", "https://core/health", "critical", 0.2),
        ServiceDefinition("docs", "Docs", "https://docs/health", "advisory", 0.2),
    )
    observer = ScriptedObserver([observation(None, None, None), observation(None, None, None)])
    probes = ScriptedProbes({"core": [False, False], "docs": [False, False]})
    monitor = StatusMonitor(observer, probes, Policy(), services, clock)

    next_snapshot(monitor, clock)
    snapshot = next_snapshot(monitor, clock)

    assert snapshot.overall == "critical"
    assert [reason.code for reason in snapshot.reasons] == [
        "disk_unavailable",
        "service_core",
        "memory_unavailable",
    ]
    assert snapshot.summary == "Disk usage is unavailable"
    assert [service.state for service in snapshot.services] == ["critical", "warning"]
    assert snapshot.metrics.cpu_percent_tenths is None


def test_monitor_recovers_service_only_after_two_successes() -> None:
    clock = ManualClock()
    service = ServiceDefinition("core", "Core", "https://core/health", "critical", 0.2)
    observer = ScriptedObserver([observation()] * 4)
    probes = ScriptedProbes({"core": [False, False, True, True]})
    monitor = StatusMonitor(observer, probes, Policy(), (service,), clock)

    assert next_snapshot(monitor, clock).services[0].state == "unknown"
    assert next_snapshot(monitor, clock).services[0].state == "critical"
    assert next_snapshot(monitor, clock).services[0].state == "critical"
    assert next_snapshot(monitor, clock).services[0].state == "healthy"


def test_collection_failure_becomes_a_bounded_critical_snapshot() -> None:
    clock = ManualClock()
    monitor = StatusMonitor(
        ScriptedObserver([OSError("secret path must not leak")]),
        ScriptedProbes({}),
        Policy(),
        (),
        clock,
    )

    snapshot = monitor.current()

    assert snapshot.overall == "critical"
    assert snapshot.reasons[0].code == "monitor_collection_failed"
    assert "secret" not in snapshot.summary
    assert snapshot.metrics.to_wire() == {
        "cpu_percent_tenths": None,
        "memory_percent_tenths": None,
        "disk_percent_tenths": None,
        "uptime_seconds": None,
    }


def test_all_configured_probes_run_concurrently_with_at_most_four_workers() -> None:
    clock = ManualClock()
    services = tuple(
        ServiceDefinition(f"svc{i}", f"Service {i}", f"https://svc{i}/health", "advisory", 1.0)
        for i in range(4)
    )

    class ConcurrentProbe:
        def __init__(self) -> None:
            self.active = 0
            self.maximum = 0
            self.lock = threading.Lock()
            self.barrier = threading.Barrier(4)

        def check(self, service: ServiceDefinition) -> bool:
            with self.lock:
                self.active += 1
                self.maximum = max(self.maximum, self.active)
            self.barrier.wait(timeout=1)
            time.sleep(0.01)
            with self.lock:
                self.active -= 1
            return True

    probes = ConcurrentProbe()
    snapshot = StatusMonitor(
        ScriptedObserver([observation()]), probes, Policy(), services, clock
    ).current()

    assert probes.maximum == 4
    assert [service.state for service in snapshot.services] == ["healthy"] * 4


def test_longest_service_id_still_produces_a_valid_failure_reason() -> None:
    clock = ManualClock()
    service = ServiceDefinition(
        "abcdefghijklmnopqrstuvwx",
        "Longest service name",
        "https://long.lan/health",
        "critical",
        0.2,
    )
    monitor = StatusMonitor(
        ScriptedObserver([observation(), observation()]),
        ScriptedProbes({service.id: [False, False]}),
        Policy(),
        (service,),
        clock,
    )

    next_snapshot(monitor, clock)
    snapshot = next_snapshot(monitor, clock)

    assert snapshot.overall == "critical"
    assert snapshot.reasons[0].code == "service_abcdefghijklmnopqrstuvwx"
