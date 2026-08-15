"""Pure threshold-based health evaluation for the hub health monitor."""

from collections.abc import Mapping
from dataclasses import dataclass
from enum import Enum
import math
from types import MappingProxyType


class HealthState(str, Enum):
    """Severity state assigned to a metric and to the overall evaluation."""

    NORMAL = "normal"
    WARNING = "warning"
    CRITICAL = "critical"


# ``MetricState`` is the domain name used by callers that only deal with
# individual metrics.  Both names intentionally share the same enum values.
MetricState = HealthState


class MetricDirection(str, Enum):
    """Direction in which a metric becomes unhealthy."""

    HIGH = "high"
    LOW = "low"


class TransitionKind(str, Enum):
    """Meaning of a state transition returned by the evaluator."""

    RAISED = "raised"
    ESCALATED = "escalated"
    DOWNGRADED = "downgraded"
    RESOLVED = "resolved"
    CHANGED = "changed"


DEFAULT_HYSTERESIS = 5.0

CPU_WARNING_THRESHOLD_PERCENT = 80.0
CPU_CRITICAL_THRESHOLD_PERCENT = 95.0
MEMORY_WARNING_THRESHOLD_PERCENT = 80.0
MEMORY_CRITICAL_THRESHOLD_PERCENT = 90.0
DISK_WARNING_THRESHOLD_PERCENT = 85.0
DISK_CRITICAL_THRESHOLD_PERCENT = 95.0
DISK_WARNING_RECOVERY_PERCENT = 80.0
DISK_CRITICAL_RECOVERY_PERCENT = 90.0


def _finite_number(value: object, name: str) -> float:
    try:
        if isinstance(value, bool):
            raise TypeError
        number = float(value)
    except (TypeError, ValueError, OverflowError) as error:
        raise ValueError(f"{name} must be a finite number") from error
    if not math.isfinite(number):
        raise ValueError(f"{name} must be a finite number")
    return number


@dataclass(frozen=True)
class MetricRule:
    """Thresholds and hysteresis policy for one numeric metric.

    ``warning_threshold`` and ``critical_threshold`` are entry thresholds.
    Recovery thresholds default to five percentage points toward the healthy
    side, or to ``hysteresis`` when a different margin is requested.
    """

    warning_threshold: float
    critical_threshold: float
    direction: MetricDirection = MetricDirection.HIGH
    hysteresis: float = DEFAULT_HYSTERESIS
    warning_recovery_threshold: float | None = None
    critical_recovery_threshold: float | None = None

    def __post_init__(self) -> None:
        warning = _finite_number(self.warning_threshold, "warning_threshold")
        critical = _finite_number(self.critical_threshold, "critical_threshold")
        hysteresis = _finite_number(self.hysteresis, "hysteresis")
        direction = MetricDirection(self.direction)
        if hysteresis < 0:
            raise ValueError("hysteresis must not be negative")

        if direction is MetricDirection.HIGH:
            if warning >= critical:
                raise ValueError("high-direction critical threshold must exceed warning")
            default_warning_recovery = warning - hysteresis
            default_critical_recovery = critical - hysteresis
        else:
            if warning <= critical:
                raise ValueError("low-direction critical threshold must be below warning")
            default_warning_recovery = warning + hysteresis
            default_critical_recovery = critical + hysteresis

        warning_recovery = (
            default_warning_recovery
            if self.warning_recovery_threshold is None
            else _finite_number(
                self.warning_recovery_threshold, "warning_recovery_threshold"
            )
        )
        critical_recovery = (
            default_critical_recovery
            if self.critical_recovery_threshold is None
            else _finite_number(
                self.critical_recovery_threshold, "critical_recovery_threshold"
            )
        )
        self._validate_recovery_thresholds(
            direction,
            warning,
            critical,
            warning_recovery,
            critical_recovery,
        )

        object.__setattr__(self, "warning_threshold", warning)
        object.__setattr__(self, "critical_threshold", critical)
        object.__setattr__(self, "direction", direction)
        object.__setattr__(self, "hysteresis", hysteresis)
        object.__setattr__(self, "warning_recovery_threshold", warning_recovery)
        object.__setattr__(self, "critical_recovery_threshold", critical_recovery)

    @staticmethod
    def _validate_recovery_thresholds(
        direction: MetricDirection,
        warning: float,
        critical: float,
        warning_recovery: float,
        critical_recovery: float,
    ) -> None:
        if direction is MetricDirection.HIGH:
            valid = (
                warning_recovery <= warning
                and critical_recovery <= critical
                and warning_recovery <= critical_recovery
            )
        else:
            valid = (
                warning_recovery >= warning
                and critical_recovery >= critical
                and warning_recovery >= critical_recovery
            )
        if not valid:
            raise ValueError("recovery thresholds must leave a hysteresis band")


DEFAULT_HEALTH_RULES: Mapping[str, MetricRule] = MappingProxyType(
    {
        "cpu_percent": MetricRule(
            CPU_WARNING_THRESHOLD_PERCENT,
            CPU_CRITICAL_THRESHOLD_PERCENT,
        ),
        "memory_used_percent": MetricRule(
            MEMORY_WARNING_THRESHOLD_PERCENT,
            MEMORY_CRITICAL_THRESHOLD_PERCENT,
        ),
        "disk_used_percent": MetricRule(
            DISK_WARNING_THRESHOLD_PERCENT,
            DISK_CRITICAL_THRESHOLD_PERCENT,
            warning_recovery_threshold=DISK_WARNING_RECOVERY_PERCENT,
            critical_recovery_threshold=DISK_CRITICAL_RECOVERY_PERCENT,
        ),
    }
)
# Immutable default rules for the metrics emitted by ``HostMetricsCollector``.


DEFAULT_RULES = DEFAULT_HEALTH_RULES


@dataclass(frozen=True)
class HealthTransition:
    """A state change for one available metric value."""

    metric: str
    previous_state: HealthState
    state: HealthState
    value: float
    kind: TransitionKind

    @property
    def current_state(self) -> HealthState:
        """Return the state after this transition."""

        return self.state

    @property
    def next_state(self) -> HealthState:
        """Return the state after this transition for monitor integrations."""

        return self.state


@dataclass(frozen=True)
class HealthEvaluation:
    """Result returned by :meth:`HealthEvaluator.evaluate`."""

    grade: HealthState
    metric_states: Mapping[str, HealthState]
    transitions: tuple[HealthTransition, ...]

    @property
    def health_grade(self) -> HealthState:
        """Compatibility alias for the wire-level ``health_grade`` name."""

        return self.grade

    @property
    def overall_grade(self) -> HealthState:
        """Compatibility alias for callers using the older plan terminology."""

        return self.grade


class HealthEvaluator:
    """Evaluate metric values without I/O, clocks, or mutable global state."""

    def __init__(
        self, rules: Mapping[str, MetricRule] | None = None
    ) -> None:
        configured_rules = rules if rules is not None else DEFAULT_HEALTH_RULES
        self._rules = dict(configured_rules)
        if any(not isinstance(rule, MetricRule) for rule in self._rules.values()):
            raise TypeError("rules must map metric names to MetricRule instances")

    @property
    def rules(self) -> Mapping[str, MetricRule]:
        """Return the evaluator's configured metric rules."""

        return MappingProxyType(self._rules)

    def evaluate(
        self,
        values: Mapping[str, float | int | None],
        previous_states: Mapping[str, HealthState | str] | None = None,
    ) -> HealthEvaluation:
        """Evaluate values against rules and return state transitions.

        Missing, non-finite, and warm-up values preserve their previous state
        and produce no transition.  With no previous state they remain normal.
        The input mappings are never modified and this method does not read
        time or perform any I/O.
        """

        previous = previous_states or {}
        metric_states: dict[str, HealthState] = {}
        transitions: list[HealthTransition] = []

        for metric, rule in self._rules.items():
            previous_state = self._coerce_state(previous.get(metric))
            value = self._coerce_value(values.get(metric))
            state = previous_state if value is None else self._evaluate_value(
                rule, value, previous_state
            )
            metric_states[metric] = state
            if value is not None and state is not previous_state:
                transitions.append(
                    HealthTransition(
                        metric=metric,
                        previous_state=previous_state,
                        state=state,
                        value=value,
                        kind=self._transition_kind(previous_state, state),
                    )
                )

        grade = max(
            metric_states.values(),
            key=self._severity,
            default=HealthState.NORMAL,
        )
        return HealthEvaluation(grade, metric_states, tuple(transitions))

    @staticmethod
    def _coerce_value(value: object) -> float | None:
        if value is None or isinstance(value, bool):
            return None
        try:
            numeric = float(value)
        except (TypeError, ValueError, OverflowError):
            return None
        return numeric if math.isfinite(numeric) else None

    @staticmethod
    def _coerce_state(value: HealthState | str | None) -> HealthState:
        if value is None:
            return HealthState.NORMAL
        try:
            return HealthState(value)
        except (TypeError, ValueError):
            return HealthState.NORMAL

    @classmethod
    def _evaluate_value(
        cls, rule: MetricRule, value: float, previous_state: HealthState
    ) -> HealthState:
        if previous_state is HealthState.CRITICAL:
            if cls._at_bad_threshold(value, rule.critical_threshold, rule.direction):
                return HealthState.CRITICAL
            if cls._at_recovery_threshold(
                value, rule.warning_recovery_threshold, rule.direction
            ):
                return HealthState.NORMAL
            if cls._at_recovery_threshold(
                value, rule.critical_recovery_threshold, rule.direction
            ):
                return HealthState.WARNING
            return HealthState.CRITICAL

        if previous_state is HealthState.WARNING:
            if cls._at_bad_threshold(value, rule.critical_threshold, rule.direction):
                return HealthState.CRITICAL
            if cls._at_recovery_threshold(
                value, rule.warning_recovery_threshold, rule.direction
            ):
                return HealthState.NORMAL
            return HealthState.WARNING

        if cls._at_bad_threshold(value, rule.critical_threshold, rule.direction):
            return HealthState.CRITICAL
        if cls._at_bad_threshold(value, rule.warning_threshold, rule.direction):
            return HealthState.WARNING
        return HealthState.NORMAL

    @staticmethod
    def _at_bad_threshold(
        value: float, threshold: float, direction: MetricDirection
    ) -> bool:
        if direction is MetricDirection.HIGH:
            return value >= threshold
        return value <= threshold

    @staticmethod
    def _at_recovery_threshold(
        value: float, threshold: float | None, direction: MetricDirection
    ) -> bool:
        if threshold is None:
            return False
        if direction is MetricDirection.HIGH:
            return value <= threshold
        return value >= threshold

    @staticmethod
    def _severity(state: HealthState) -> int:
        return {
            HealthState.NORMAL: 0,
            HealthState.WARNING: 1,
            HealthState.CRITICAL: 2,
        }[state]

    @classmethod
    def _transition_kind(
        cls, previous_state: HealthState, state: HealthState
    ) -> TransitionKind:
        if state is HealthState.NORMAL:
            return TransitionKind.RESOLVED
        if previous_state is HealthState.NORMAL:
            return TransitionKind.RAISED
        if state is HealthState.CRITICAL:
            return TransitionKind.ESCALATED
        if state is HealthState.WARNING:
            return TransitionKind.DOWNGRADED
        return TransitionKind.CHANGED


__all__ = [
    "CPU_CRITICAL_THRESHOLD_PERCENT",
    "CPU_WARNING_THRESHOLD_PERCENT",
    "DEFAULT_HEALTH_RULES",
    "DEFAULT_HYSTERESIS",
    "DEFAULT_RULES",
    "DISK_CRITICAL_RECOVERY_PERCENT",
    "DISK_CRITICAL_THRESHOLD_PERCENT",
    "DISK_WARNING_RECOVERY_PERCENT",
    "DISK_WARNING_THRESHOLD_PERCENT",
    "HealthEvaluation",
    "HealthEvaluator",
    "HealthState",
    "HealthTransition",
    "MEMORY_CRITICAL_THRESHOLD_PERCENT",
    "MEMORY_WARNING_THRESHOLD_PERCENT",
    "MetricDirection",
    "MetricRule",
    "MetricState",
    "TransitionKind",
]
