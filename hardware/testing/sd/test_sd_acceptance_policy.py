"""Host-side safety contract for the non-destructive HW-006 target."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).with_name("sd_acceptance.cpp")
PLATFORMIO = ROOT / "platformio.ini"


class SdAcceptancePolicyTest(unittest.TestCase):
    """Verify the acceptance target cannot format or escape its workspace."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_mount_is_explicitly_non_destructive(self) -> None:
        self.assertIn("kFormatIfMountFails = false", self.source)
        self.assertNotIn("format_sd", self.source)
        self.assertFalse(Path(__file__).with_name("format_sd.cpp").exists())

    def test_uses_official_one_bit_20_mhz_configuration(self) -> None:
        self.assertIn("kOneBitMode = true", self.source)
        self.assertIn("kSdClockPin = 11", self.source)
        self.assertIn("kSdCommandPin = 10", self.source)
        self.assertIn("kSdData0Pin = 9", self.source)
        self.assertIn("kSdFrequencyKHz = SDMMC_FREQ_DEFAULT", self.source)
        self.assertIn("FREQUENCY_KHZ", self.source)

    def test_integrity_remount_cleanup_and_missing_card_are_covered(self) -> None:
        for behavior in (
            "DETERMINISTIC_READBACK",
            "FNV1A32",
            "THROUGHPUT",
            "REMOUNT_READBACK",
            "WORKSPACE_CLEANUP",
            "MISSING_CARD_HANDLED",
        ):
            self.assertIn(behavior, self.source)
        self.assertIn("SD_MMC.end()", self.source)

    def test_all_card_files_stay_below_the_dedicated_root(self) -> None:
        self.assertIn('kTestRoot[] = "/nova-hw-006"', self.source)
        for forbidden_path in ("/hello.txt", "/foo.txt", "/test.txt"):
            self.assertNotIn(forbidden_path, self.source)

    def test_has_dedicated_acceptance_environment(self) -> None:
        config = PLATFORMIO.read_text(encoding="utf-8")
        self.assertIn("[env:sd-acceptance]", config)
        self.assertIn("hardware/testing/sd/sd_acceptance.cpp", config)
        self.assertNotIn("[env:sd-format-test]", config)


if __name__ == "__main__":
    unittest.main()
