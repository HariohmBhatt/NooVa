"""Operator-only device and host-metric administration commands."""

import argparse
import json
from collections.abc import Sequence
from typing import Any

from .config import Settings
from .store import DEFAULT_HOST_SAMPLE_LIMIT, DEFAULT_QUERY_LIMIT, HubStore


def _limit(value: str) -> int:
    parsed = int(value)
    if not 1 <= parsed <= DEFAULT_QUERY_LIMIT:
        raise argparse.ArgumentTypeError(
            f"limit must be between 1 and {DEFAULT_QUERY_LIMIT}"
        )
    return parsed


def _value(value: Any) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.1f}"
    return str(value)


def _print_devices(devices: list[dict[str, Any]], as_json: bool) -> None:
    if as_json:
        print(json.dumps(devices, indent=2, sort_keys=True))
        return
    for device in devices:
        touch = "-"
        if device["touch_ready"] is not None:
            touch = f"ready={str(device['touch_ready']).lower()}"
            touch += f",active={str(device['touch_active']).lower()}"
        print(
            "\t".join(
                (
                    device["device_id"],
                    device["display_name"],
                    f"last_seen={_value(device['last_seen_at'])}",
                    f"online={'yes' if device['online'] else 'no'}",
                    f"telemetry_at={_value(device['latest_telemetry_at'])}",
                    f"sequence={_value(device['telemetry_sequence'])}",
                    f"rssi={_value(device['wifi_rssi_dbm'])}",
                    f"free_heap={_value(device['free_heap_bytes'])}",
                    f"touch={touch}",
                    f"audio={_value(device['audio_state'])}",
                )
            )
        )


def _print_metrics(samples: list[dict[str, Any]], as_json: bool) -> None:
    if as_json:
        print(json.dumps(samples, indent=2, sort_keys=True))
        return
    if not samples:
        print("No host metric samples recorded.")
        return
    print("sampled_at\tcpu_%\tmemory_%\tdisk_%\tnetwork_rx_Bps\tnetwork_tx_Bps")
    for sample in samples:
        print(
            "\t".join(
                (
                    sample["sampled_at"],
                    _value(sample["cpu_percent"]),
                    _value(sample["memory_used_percent"]),
                    _value(sample["disk_used_percent"]),
                    _value(sample["network_rx_bytes_per_second"]),
                    _value(sample["network_tx_bytes_per_second"]),
                )
            )
        )


def main(argv: Sequence[str] | None = None) -> int:
    """Run operator-only device and host metric commands."""
    parser = argparse.ArgumentParser(prog="nova-hub")
    commands = parser.add_subparsers(dest="command", required=True)
    devices = commands.add_parser("devices")
    devices.add_argument("--json", action="store_true", dest="as_json")
    metrics = commands.add_parser("metrics")
    metrics.add_argument(
        "--limit", type=_limit, default=DEFAULT_HOST_SAMPLE_LIMIT
    )
    metrics.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args(argv)

    settings = Settings.from_env()
    store = HubStore(settings.db_path)
    store.initialize()
    if args.command == "devices":
        _print_devices(store.devices(), args.as_json)
    else:
        _print_metrics(store.query_host_samples(limit=args.limit), args.as_json)
    return 0
