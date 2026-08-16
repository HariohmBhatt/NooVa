"""Operator-only device administration commands."""

import argparse
from collections.abc import Sequence

from .config import Settings
from .store import HubStore


def main(argv: Sequence[str] | None = None) -> int:
    """Run operator-only device administration commands."""
    parser = argparse.ArgumentParser(prog="nova-hub")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("devices")
    args = parser.parse_args(argv)

    settings = Settings.from_env()
    store = HubStore(settings.db_path)
    store.initialize()
    for device in store.devices():
        print(f"{device['device_id']}\t{device['display_name']}")
    return 0
