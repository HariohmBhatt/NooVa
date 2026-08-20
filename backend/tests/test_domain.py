import pytest

from app.domain import Metrics, Reason, ServiceStatus, StatusSnapshot


def test_snapshot_serializes_the_exact_bounded_wire_contract() -> None:
    snapshot = StatusSnapshot(
        sequence=42,
        generated_at_epoch_s=1_787_227_200,
        overall="warning",
        summary="System disk is 82% full",
        reasons=(Reason("disk_high", "warning", "System disk is 82% full"),),
        metrics=Metrics(241, 610, 821, 48_210),
        services=(ServiceStatus("hub", "Hub", "healthy"),),
    )

    assert snapshot.to_wire() == {
        "schema_version": 1,
        "sequence": 42,
        "generated_at_epoch_s": 1_787_227_200,
        "overall": "warning",
        "summary": "System disk is 82% full",
        "reasons": [
            {
                "code": "disk_high",
                "severity": "warning",
                "message": "System disk is 82% full",
            }
        ],
        "metrics": {
            "cpu_percent_tenths": 241,
            "memory_percent_tenths": 610,
            "disk_percent_tenths": 821,
            "uptime_seconds": 48_210,
        },
        "services": [{"id": "hub", "name": "Hub", "state": "healthy"}],
    }


def test_snapshot_rejects_cross_field_and_size_violations() -> None:
    with pytest.raises(ValueError, match="healthy snapshot cannot contain reasons"):
        StatusSnapshot(
            sequence=1,
            generated_at_epoch_s=1,
            overall="healthy",
            summary="All monitored systems normal",
            reasons=(Reason("cpu_high", "warning", "CPU is high"),),
            metrics=Metrics(None, None, None, None),
            services=(),
        )

    with pytest.raises(ValueError, match="protocol range"):
        Metrics(1.5, None, None, None)  # type: ignore[arg-type]

    with pytest.raises(ValueError, match="sequence"):
        StatusSnapshot(
            sequence=None,  # type: ignore[arg-type]
            generated_at_epoch_s=1,
            overall="healthy",
            summary="All monitored systems normal",
            reasons=(),
            metrics=Metrics(None, None, None, None),
            services=(),
        )

    reason = Reason("disk_critical", "critical", "Disk usage is critical")
    with pytest.raises(ValueError, match="must equal overall"):
        StatusSnapshot(
            sequence=1,
            generated_at_epoch_s=1,
            overall="warning",
            summary=reason.message,
            reasons=(reason,),
            metrics=Metrics(None, None, None, None),
            services=(),
        )

    with pytest.raises(ValueError, match="at most four services"):
        StatusSnapshot(
            sequence=1,
            generated_at_epoch_s=1,
            overall="healthy",
            summary="All monitored systems normal",
            reasons=(),
            metrics=Metrics(None, None, None, None),
            services=tuple(
                ServiceStatus(f"svc{i}", f"Service {i}", "healthy") for i in range(5)
            ),
        )
