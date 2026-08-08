"""Host-side safety contract for the read-only board diagnostics target."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_name("board_diagnostics.cpp")
PLATFORMIO = ROOT / "platformio.ini"


class BoardDiagnosticsPolicyTest(unittest.TestCase):
    """Verify observable diagnostics and forbid hardware-mutating APIs."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_reports_and_exercises_exact_board_memory(self) -> None:
        self.assertIn("ESP.getFlashChipSize()", self.source)
        self.assertIn("ESP.getPsramSize()", self.source)
        self.assertIn("kExpectedFlashBytes", self.source)
        self.assertIn("kExpectedPsramBytes", self.source)
        self.assertIn("MALLOC_CAP_SPIRAM", self.source)
        self.assertIn("PSRAM_PATTERN", self.source)

    def test_scans_all_official_onboard_i2c_devices(self) -> None:
        for device in (
            "AXP2101",
            "PCF85063",
            "ES8311",
            "TCA9554",
            "FT6336",
            "QMI8658",
        ):
            self.assertIn(device, self.source)
        self.assertIn("I2C_EXPECTED_DEVICES", self.source)

    def test_rtc_and_power_checks_are_read_only(self) -> None:
        forbidden_calls = re.compile(
            r"\b(?:setDateTime|set[A-Z]\w*Voltage|enable[A-Z]\w*|"
            r"disable[A-Z]\w*|shutdown|powerOff|esp_efuse\w*)\s*\("
        )
        self.assertIsNone(forbidden_calls.search(self.source))
        self.assertIn("RTC_MONOTONIC", self.source)
        self.assertIn("AXP2101_READ_ONLY_TELEMETRY", self.source)

    def test_has_dedicated_platformio_target(self) -> None:
        config = PLATFORMIO.read_text(encoding="utf-8")
        self.assertIn("[env:board-diagnostics]", config)
        self.assertIn("hardware/testing/board/board_diagnostics.cpp", config)


if __name__ == "__main__":
    unittest.main()
