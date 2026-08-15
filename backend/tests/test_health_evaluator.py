import math

from app.health_evaluator import (
    HealthEvaluator,
    HealthState,
    MetricDirection,
    MetricRule,
    TransitionKind,
)


def test_default_disk_threshold_boundaries_and_transitions() -> None:
    evaluator = HealthEvaluator()

    normal = evaluator.evaluate({"disk_used_percent": 84.9})
    warning = evaluator.evaluate({"disk_used_percent": 85.0})
    critical = evaluator.evaluate(
        {"disk_used_percent": 95.0}, warning.metric_states
    )

    assert normal.grade is HealthState.NORMAL
    assert normal.metric_states["disk_used_percent"] is HealthState.NORMAL
    assert warning.metric_states["disk_used_percent"] is HealthState.WARNING
    assert warning.transitions[0].kind is TransitionKind.RAISED
    assert critical.grade is HealthState.CRITICAL
    assert critical.metric_states["disk_used_percent"] is HealthState.CRITICAL
    assert critical.transitions[0].kind is TransitionKind.ESCALATED


def test_disk_hysteresis_requires_the_recovery_boundaries() -> None:
    evaluator = HealthEvaluator()

    warning = evaluator.evaluate({"disk_used_percent": 86.0})
    still_warning = evaluator.evaluate(
        {"disk_used_percent": 80.1}, warning.metric_states
    )
    resolved = evaluator.evaluate({"disk_used_percent": 80.0}, still_warning.metric_states)
    critical = evaluator.evaluate({"disk_used_percent": 96.0})
    downgraded = evaluator.evaluate(
        {"disk_used_percent": 90.0}, critical.metric_states
    )

    assert still_warning.metric_states["disk_used_percent"] is HealthState.WARNING
    assert still_warning.transitions == ()
    assert resolved.metric_states["disk_used_percent"] is HealthState.NORMAL
    assert resolved.transitions[0].kind is TransitionKind.RESOLVED
    assert downgraded.metric_states["disk_used_percent"] is HealthState.WARNING
    assert downgraded.transitions[0].kind is TransitionKind.DOWNGRADED


def test_overall_grade_is_the_most_severe_metric_state() -> None:
    evaluator = HealthEvaluator()

    result = evaluator.evaluate(
        {
            "cpu_percent": 80.0,
            "memory_used_percent": 91.0,
            "disk_used_percent": 84.0,
        }
    )

    assert result.grade is HealthState.CRITICAL
    assert result.metric_states["cpu_percent"] is HealthState.WARNING
    assert result.metric_states["memory_used_percent"] is HealthState.CRITICAL
    assert result.metric_states["disk_used_percent"] is HealthState.NORMAL


def test_missing_and_nonfinite_values_do_not_create_alerts() -> None:
    evaluator = HealthEvaluator()
    previous_states = {"disk_used_percent": HealthState.WARNING}

    result = evaluator.evaluate(
        {"disk_used_percent": None, "cpu_percent": math.nan}, previous_states
    )

    assert result.grade is HealthState.WARNING
    assert result.metric_states["disk_used_percent"] is HealthState.WARNING
    assert result.metric_states["cpu_percent"] is HealthState.NORMAL
    assert result.transitions == ()
    assert previous_states == {"disk_used_percent": HealthState.WARNING}


def test_low_direction_uses_inverse_thresholds_and_hysteresis() -> None:
    evaluator = HealthEvaluator(
        {
            "wifi_rssi_dbm": MetricRule(
                warning_threshold=-70.0,
                critical_threshold=-85.0,
                direction=MetricDirection.LOW,
            )
        }
    )

    normal = evaluator.evaluate({"wifi_rssi_dbm": -69.0})
    warning = evaluator.evaluate({"wifi_rssi_dbm": -70.0})
    critical = evaluator.evaluate(
        {"wifi_rssi_dbm": -85.0}, warning.metric_states
    )
    downgraded = evaluator.evaluate(
        {"wifi_rssi_dbm": -80.0}, critical.metric_states
    )
    resolved = evaluator.evaluate(
        {"wifi_rssi_dbm": -65.0}, downgraded.metric_states
    )

    assert normal.grade is HealthState.NORMAL
    assert warning.metric_states["wifi_rssi_dbm"] is HealthState.WARNING
    assert critical.metric_states["wifi_rssi_dbm"] is HealthState.CRITICAL
    assert downgraded.metric_states["wifi_rssi_dbm"] is HealthState.WARNING
    assert resolved.metric_states["wifi_rssi_dbm"] is HealthState.NORMAL
