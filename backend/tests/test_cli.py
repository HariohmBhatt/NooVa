from pathlib import Path

from app.cli import main
from app.store import HubStore


def test_operator_can_list_a_registered_device(monkeypatch, capsys, tmp_path: Path) -> None:
    database = tmp_path / "hub.db"
    store = HubStore(database)
    store.initialize()
    device_id = store.register_device("nonce-0123456789", "0.1.0", "hall")
    monkeypatch.setenv("NOVA_DB_PATH", str(database))

    assert main(["devices"]) == 0

    output = capsys.readouterr().out
    assert f"{device_id}\thall" in output
