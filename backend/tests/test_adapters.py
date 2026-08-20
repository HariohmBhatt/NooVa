import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from app.adapters import HttpProbeClient, LinuxObserver
from app.config import ServiceDefinition


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="ascii")


def test_linux_observer_reads_normalized_values_from_fixture_mounts(tmp_path: Path) -> None:
    proc = tmp_path / "proc"
    disk = tmp_path / "disk"
    disk.mkdir()
    # Guest counters are already included in user/nice and must not inflate the
    # total used to calculate CPU utilization.
    _write(proc / "stat", "cpu  100 0 100 800 0 0 0 0 500 500\n")
    _write(proc / "meminfo", "MemTotal: 1000 kB\nMemAvailable: 400 kB\n")
    _write(proc / "uptime", "48210.75 100.0\n")
    observer = LinuxObserver(proc, disk)

    first = observer.collect()
    _write(proc / "stat", "cpu  130 0 120 850 0 0 0 0 900 900\n")
    second = observer.collect()

    assert first.cpu_percent_tenths == 200
    assert second.cpu_percent_tenths == 500
    assert first.memory_percent_tenths == 600
    assert first.uptime_seconds == 48_210
    assert first.disk_percent_tenths is not None


def test_linux_observer_marks_each_missing_observation_unavailable(tmp_path: Path) -> None:
    observation = LinuxObserver(tmp_path / "missing-proc", tmp_path / "missing-disk").collect()

    assert observation.cpu_percent_tenths is None
    assert observation.memory_percent_tenths is None
    assert observation.disk_percent_tenths is None
    assert observation.uptime_seconds is None


def test_http_probe_treats_redirect_as_healthy_without_following_it() -> None:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == "/redirect":
                self.send_response(302)
                self.send_header("Location", "/failure")
            else:
                self.send_response(500)
            self.end_headers()

        def log_message(self, format: str, *args: object) -> None:
            return

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        service = ServiceDefinition(
            "redirect",
            "Redirect",
            f"http://127.0.0.1:{server.server_port}/redirect",
            "advisory",
            1.0,
        )
        assert HttpProbeClient().check(service) is True
    finally:
        server.shutdown()
        thread.join(timeout=1)
        server.server_close()
