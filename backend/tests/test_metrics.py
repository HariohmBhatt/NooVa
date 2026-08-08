from pathlib import Path

from app.metrics import HostMetricsCollector


def test_collector_reads_selected_host_filesystem(tmp_path: Path) -> None:
    proc = tmp_path / "proc"
    (proc / "net").mkdir(parents=True)
    (proc / "stat").write_text("cpu  100 0 50 850 0 0 0 0 0 0\n")
    (proc / "loadavg").write_text("0.20 0.10 0.05 1/100 1\n")
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
