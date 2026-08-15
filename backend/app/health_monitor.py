"""One app-wide host health sampler and snapshot publisher."""

from __future__ import annotations

import asyncio
from collections import deque
from collections.abc import Callable, Mapping
from datetime import datetime
from typing import Any
from zoneinfo import ZoneInfo

from .config import Settings
from .health_evaluator import HealthEvaluator
from .metrics import HostMetricsCollector
from .protocol import utc_now
from .store import HubStore


class HealthMonitor:
    """Coordinate collection, evaluation, persistence, and bounded snapshots."""

    def __init__(
        self,
        settings: Settings,
        store: HubStore,
        collector: HostMetricsCollector,
        evaluator: HealthEvaluator | None = None,
        clock: Callable[[], datetime] = utc_now,
    ) -> None:
        """Create a monitor whose sampling state is shared by all sessions."""
        self._settings = settings
        self._store = store
        self._collector = collector
        self._evaluator = evaluator or HealthEvaluator()
        self._clock = clock
        self._previous_states = self._load_alert_states()
        self._trend_history: deque[dict[str, Any]] = deque(
            self._load_history(), maxlen=settings.health_trend_points
        )
        self._last_history_bucket: int | None = None
        self._snapshot: dict[str, Any] = {
            "health_grade": "warning",
            "metric_health": {},
            "active_alerts": [],
            "trends": self._trend_payload(),
        }
        self._sequence = 0

    def sample(self) -> dict[str, Any]:
        """Collect and publish one host snapshot, returning its payload."""
        metrics = self._collector.collect()
        now = self._clock()
        values = self._health_values(metrics)
        evaluation = self._evaluator.evaluate(values, self._previous_states)
        self._apply_evaluation(evaluation, now)
        self._record_history(metrics, now)
        self._snapshot = self._build_snapshot(metrics, evaluation, now)
        self._sequence += 1
        return self._snapshot

    async def run(self) -> None:
        """Sample until the application lifespan cancels this coroutine."""
        while True:
            await asyncio.sleep(self._settings.health_sample_period_seconds)
            self.sample()

    def snapshot(self, device_id: str | None = None) -> dict[str, Any]:
        """Return the latest snapshot, optionally annotated with device telemetry."""
        snapshot = dict(self._snapshot)
        if device_id is not None:
            telemetry = self._store.latest_device_telemetry(device_id)
            if telemetry is not None:
                snapshot["device_telemetry"] = telemetry
        return snapshot

    @property
    def sequence(self) -> int:
        """Return the monotonically increasing snapshot sequence."""
        return self._sequence

    def record_device_metrics(self, device_id: str, telemetry: Mapping[str, Any]) -> None:
        """Persist one validated device telemetry payload for a session."""
        self._store.record_device_telemetry(device_id, dict(telemetry))

    def _health_values(self, metrics: Mapping[str, Any]) -> dict[str, float | None]:
        values: dict[str, float | None] = {
            "cpu_percent": self._number(metrics.get("cpu_percent")),
            "memory_used_percent": self._percentage(
                metrics.get("memory_used_bytes"), metrics.get("memory_total_bytes")
            ),
            "disk_used_percent": self._percentage(
                metrics.get("disk_used_bytes"), metrics.get("disk_total_bytes")
            ),
        }
        if metrics.get("cpu_warmup"):
            values["cpu_percent"] = None
        return values

    def _apply_evaluation(self, evaluation: Any, now: datetime) -> None:
        self._previous_states = dict(evaluation.metric_states)
        for transition in evaluation.transitions:
            self._store.apply_alert_transition(
                scope="host",
                metric=transition.metric,
                state=self._state_name(transition.state),
                value=transition.value,
                details={"evaluated_at": now.isoformat()},
            )

    def _record_history(self, metrics: Mapping[str, Any], now: datetime) -> None:
        period = self._settings.metrics_history_period_seconds
        bucket = int(now.timestamp()) // period
        if bucket == self._last_history_bucket:
            return
        self._last_history_bucket = bucket
        values = self._health_values(metrics)
        sample = {
            "cpu_percent": metrics.get("cpu_percent"),
            "memory_used_percent": values["memory_used_percent"],
            "disk_used_percent": values["disk_used_percent"],
            "network_rx_bytes_per_second": metrics.get(
                "network_rx_bytes_per_second"
            ),
            "network_tx_bytes_per_second": metrics.get(
                "network_tx_bytes_per_second"
            ),
        }
        stored = self._store.record_host_sample(sample, sampled_at=now)
        self._trend_history.append(stored)

    def _build_snapshot(
        self, metrics: Mapping[str, Any], evaluation: Any, now: datetime
    ) -> dict[str, Any]:
        payload = dict(metrics)
        values = self._health_values(metrics)
        grade = self._state_name(evaluation.grade)
        if payload["collection_errors"] and grade == "normal":
            grade = "warning"
        active_alerts = [
            alert
            for alert in self._store.active_alert_states()
            if alert["scope"] == "host"
        ]
        payload.update(
            {
                "health_grade": grade,
                "metric_health": {
                    metric: {
                        "state": self._state_name(state),
                        "value": values.get(metric),
                    }
                    for metric, state in evaluation.metric_states.items()
                },
                "active_alerts": [
                    {
                        "metric": alert["metric"],
                        "state": alert["state"],
                        "value": alert["value"],
                    }
                    for alert in active_alerts
                ],
                "server_version": self._settings.hub_version,
                "home_time": now.astimezone(
                    ZoneInfo(self._settings.home_timezone)
                ).replace(microsecond=0).isoformat(),
                "service_status": {
                    "hub_api": "healthy",
                    "metrics": "healthy"
                    if not payload["collection_errors"]
                    else "degraded",
                },
                "dependency_status": "healthy"
                if grade == "normal" and not payload["collection_errors"]
                else "degraded",
                "trends": self._trend_payload(),
            }
        )
        return payload

    def _load_alert_states(self) -> dict[str, Any]:
        return {
            alert["metric"]: alert["state"]
            for alert in self._store.active_alert_states()
            if alert["scope"] == "host"
        }

    def _load_history(self) -> list[dict[str, Any]]:
        rows = self._store.query_host_samples(self._settings.health_trend_points)
        return [dict(row) for row in rows]

    def _trend_payload(self) -> dict[str, Any]:
        return {
            "period_seconds": self._settings.metrics_history_period_seconds,
            "cpu_percent": [row.get("cpu_percent") for row in self._trend_history],
            "memory_used_percent": [
                row.get("memory_used_percent") for row in self._trend_history
            ],
            "disk_used_percent": [
                row.get("disk_used_percent") for row in self._trend_history
            ],
        }

    @staticmethod
    def _number(value: Any) -> float | None:
        return float(value) if isinstance(value, (int, float)) else None

    @staticmethod
    def _percentage(used: Any, total: Any) -> float | None:
        if not isinstance(used, (int, float)) or not isinstance(total, (int, float)):
            return None
        if total <= 0:
            return None
        return round(float(used) / float(total) * 100.0, 1)

    @staticmethod
    def _state_name(state: Any) -> str:
        return str(getattr(state, "value", state))
