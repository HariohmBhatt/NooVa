# HW-006 SD Formatter

One-shot FAT filesystem formatter and verification firmware for the Waveshare
ESP32-S3 Touch LCD 3.5 onboard TF/microSD slot.

## Safety

Formatting is destructive. The firmware first attempts a mount with formatting
disabled. It formats only after the operator sends uppercase `F` over the
serial monitor. Do not send `F` unless the intended card is inserted.

## Official Configuration

The pins and 1-bit SD_MMC mode follow Waveshare's official
[`07_sd_card_test`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/blob/main/Arduino/examples/07_sd_card_test/07_sd_card_test.ino):

- SD CLK: GPIO `11`
- SD CMD: GPIO `10`
- SD D0: GPIO `9`
- Mode: 1-bit
- Frequency: `20000 Hz`

The formatter uses `SD_MMC.begin(..., format_if_mount_failed=true)` only after
the explicit `F` command, then writes, reads, and deletes a temporary check file.

## Procedure

1. Keep the board disconnected and insert the microSD/TF card.
2. Reconnect USB and upload the formatter.
3. Open the serial monitor at 115200 baud.
4. Confirm the output shows `FORMAT_ARMED`.
5. If the card mounts, the temporary write/read/delete check runs automatically.
6. Only if `FORMAT_ARMED` appears, send uppercase `F` once to erase and format.
7. Wait for `FORMAT_COMPLETE` or filesystem verification output.
8. Disconnect power before removing the card.

## Commands

```sh
pio run -e sd-format-test
pio run -e sd-format-test --target upload --upload-port /dev/cu.usbmodem2101
pio device monitor --baud 115200 --port /dev/cu.usbmodem2101
```

- `F`: format and verify; destructive
- `s`: print card status after completion
- `h`: print help

## Files

- `format_sd.cpp`: one-shot formatter
- `REPORT.md`: execution record
