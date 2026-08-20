import json

import pytest

from app.config import Settings


def test_environment_configuration_accepts_four_valid_services(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("NOVA_DEVICE_TOKEN", "a" * 32)
    monkeypatch.delenv("NOVA_LAN_PORT", raising=False)
    monkeypatch.setenv(
        "NOVA_SERVICES_JSON",
        json.dumps(
            [
                {
                    "id": f"svc{i}",
                    "name": f"Service {i}",
                    "url": f"https://svc{i}.lan/health",
                    "importance": "critical" if i == 0 else "advisory",
                    "timeout_seconds": 1.5,
                }
                for i in range(4)
            ]
        ),
    )

    settings = Settings.from_env()

    assert len(settings.services) == 4
    assert settings.services[0].importance == "critical"
    assert settings.lan_port == 443


def test_environment_configuration_accepts_an_alternate_lan_port(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("NOVA_DEVICE_TOKEN", "a" * 32)
    monkeypatch.setenv("NOVA_LAN_PORT", "8443")

    assert Settings.from_env().lan_port == 8443


@pytest.mark.parametrize(
    ("name", "value", "message"),
    [
        ("NOVA_DEVICE_TOKEN", "short", "at least 32"),
        ("NOVA_CPU_WARNING_TENTHS", "nope", "must be an integer"),
        ("NOVA_LAN_PORT", "0", "LAN port must be 1..65535"),
        ("NOVA_LAN_PORT", "65536", "LAN port must be 1..65535"),
        ("NOVA_LAN_PORT", "not-a-port", "must be an integer"),
        ("NOVA_SERVICES_JSON", "{}", "must be an array"),
    ],
)
def test_invalid_environment_configuration_fails_closed(
    monkeypatch: pytest.MonkeyPatch, name: str, value: str, message: str
) -> None:
    monkeypatch.setenv("NOVA_DEVICE_TOKEN", "a" * 32)
    monkeypatch.setenv(name, value)

    with pytest.raises(ValueError, match=message):
        Settings.from_env()
