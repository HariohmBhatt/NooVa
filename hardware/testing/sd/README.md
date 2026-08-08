# HW-006 non-destructive SD acceptance

`sd-acceptance` validates the Waveshare onboard TF/microSD interface without
formatting media. It creates files only below `/nova-hw-006`, verifies a
deterministic small file, renames and enumerates it, writes and reads a 1 MiB
pattern with FNV-1a hashes and throughput, unmounts/remounts, verifies both
files again, and removes the complete test workspace.

If no card is present or its filesystem cannot mount, the firmware prints a
controlled `BLOCKED` result and remains responsive. Formatting is not exposed
by this target under any command or failure path.

## Official configuration

The pins and 1-bit mode follow Waveshare's official
[`07_sd_card_test`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/Arduino/examples/07_sd_card_test/07_sd_card_test.ino).
The fixed `SDMMC_FREQ_DEFAULT` value is 20,000 kHz (20 MHz), matching
Waveshare's official
[`esp_sdcard_port.cpp`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/283ec84c566c096f8c30493b93dcd4b0bb608de7/ESP-IDF/01_factory/components/esp_port/esp_sdcard_port.cpp)
and Espressif's official
[`SD_MMC` API](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/SD_MMC/src/SD_MMC.h):

- SD CLK: GPIO `11`
- SD CMD: GPIO `10`
- SD D0: GPIO `9`
- mode: 1-bit
- maximum frequency: `20000` kHz
- format on mount failure: `false`

## Run

1. Insert a known-good FAT card while the board is powered off.
2. Build, upload, and capture the complete serial report.
3. Confirm `COMPLETE RESULT=PASS` and that `/nova-hw-006` was removed.
4. Power off, remove the card, boot the same binary, and confirm
   `MISSING_CARD_HANDLED` followed by `COMPLETE RESULT=BLOCKED`.

```sh
python3 -m unittest hardware/testing/sd/test_sd_acceptance_policy.py
pio run -e sd-acceptance
pio run -e sd-acceptance --target upload --upload-port <device-port>
pio device monitor --baud 115200 --port <device-port>
```
