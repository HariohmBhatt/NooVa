from pathlib import Path

import app.metrics as metrics_module
from app.metrics import HostMetricsCollector


def test_collector_reads_selected_host_filesystem(tmp_path: Path) -> None:
    proc = tmp_path / "proc"
    (proc / "net").mkdir(parents=True)
    (proc / "stat").write_text("cpu  100 0 50 850 0 0 0 0 0 0\n")
    (proc / "meminfo").write_text("MemTotal:       2048 kB\nMemAvailable:   1024 kB\n")
    (proc / "uptime").write_text("42.5 10.0\n")
    (proc / "net/route").write_text(
        "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\n"
        "enp3s0\t00000000\t00000000\t0003\t0\t0\t100\n"
    )
    (proc / "net/dev").write_text(
        "Inter-| Receive | Transmit\n"
        " face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\n"
        " enp3s0: 1000 1 0 0 0 0 0 0 2000 1 0 0 0 0 0 0\n"
    )

    metrics = HostMetricsCollector(
        proc_root=proc, sys_root=proc / "missing-sys", disk_root=tmp_path
    ).collect()

    assert metrics["dependency_status"] == "healthy"
    assert metrics["network_interface"] == "enp3s0"
    assert metrics["network_rx_bytes_total"] == 1000
    assert metrics["network_tx_bytes_total"] == 2000
    assert metrics["uptime_seconds"] == 42.5
    assert metrics["cpu_percent"] is None
    assert metrics["cpu_warmup"] is True
    assert metrics["network_warmup"] is True
    assert metrics["memory_used_percent"] == 50.0


def test_cpu_percent_uses_jiffy_delta_after_warmup(tmp_path: Path) -> None:
    proc = tmp_path / "proc"
    (proc / "net").mkdir(parents=True)
    (proc / "stat").write_text("cpu  100 0 50 850 0 0 0 0 0 0\n")
    (proc / "meminfo").write_text("MemTotal: 2048 kB\nMemAvailable: 1024 kB\n")
    (proc / "uptime").write_text("42.5 10.0\n")
    (proc / "net/dev").write_text(
        "Inter-| Receive | Transmit\n"
        " face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\n"
        " enp3s0: 1000 1 0 0 0 0 0 0 2000 1 0 0 0 0 0 0\n"
    )

    collector = HostMetricsCollector(
        proc_root=proc,
        sys_root=proc / "missing-sys",
        disk_root=tmp_path,
        interface="enp3s0",
    )

    first = collector.collect()
    (proc / "stat").write_text("cpu  120 0 60 900 0 0 0 0 0 0\n")
    second = collector.collect()

    assert first["cpu_warmup"] is True
    assert second["cpu_warmup"] is False
    assert second["cpu_percent"] == 37.5


def test_network_rate_window_is_capped_after_long_gap(
    monkeypatch, tmp_path: Path
) -> None:
    proc = tmp_path / "proc"
    (proc / "net").mkdir(parents=True)
    (proc / "stat").write_text("cpu  100 0 50 850 0 0 0 0 0 0\n")
    (proc / "meminfo").write_text("MemTotal: 2048 kB\nMemAvailable: 1024 kB\n")
    (proc / "uptime").write_text("42.5 10.0\n")
    net_dev = proc / "net/dev"

    def write_network_bytes(received: int, transmitted: int) -> None:
        net_dev.write_text(
            "Inter-| Receive | Transmit\n"
            " face |bytes packets errs drop fifo frame compressed multicast|bytes packets errs drop fifo colls carrier compressed\n"
            f" enp3s0: {received} 1 0 0 0 0 0 0 {transmitted} 1 0 0 0 0 0 0\n"
        )

    write_network_bytes(1000, 2000)
    clock = iter((0.0, 90.0, 95.0))
    monkeypatch.setattr(metrics_module.time, "monotonic", lambda: next(clock))
    collector = HostMetricsCollector(
        proc_root=proc,
        sys_root=proc / "missing-sys",
        disk_root=tmp_path,
        interface="enp3s0",
        max_network_window_seconds=10.0,
    )

    first = collector.collect()
    write_network_bytes(1300, 2600)
    second = collector.collect()
    write_network_bytes(1400, 2900)
    third = collector.collect()

    assert first["network_warmup"] is True
    assert second["network_window_capped"] is True
    assert second["network_warmup"] is True
    assert second["network_rx_bytes_per_second"] == 30.0
    assert second["network_tx_bytes_per_second"] == 60.0
    assert third["network_window_capped"] is False
    assert third["network_warmup"] is False
    assert third["network_rx_bytes_per_second"] == 20.0
    assert third["network_tx_bytes_per_second"] == 60.0
