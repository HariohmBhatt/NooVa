# HW-006 SD Card Formatting Report

## Status

`PASS` (format not needed)

The inserted card mounted successfully with formatting disabled. It is already
usable by the board, so no destructive format command was sent.

## Test Definition

- Test ID: `HW-006`
- Device: Waveshare ESP32-S3 Touch LCD 3.5
- Device under test: onboard TF/microSD interface
- Test firmware: `hardware/testing/sd/format_sd.cpp`
- PlatformIO environment: `sd-format-test`
- Filesystem operation: FAT format-if-mount-failed, followed by verification

## Hardware Configuration

- SD CLK: GPIO `11`
- SD CMD: GPIO `10`
- SD D0: GPIO `9`
- Mode: 1-bit
- Frequency: `20000 Hz`

## Execution Record

- Date: 2026-08-08
- Firmware commit: `5806acd`
- Board serial: `1C:DB:D4:79:7D:AC`
- Board MAC: `1c:db:d4:79:7d:ac`
- Upload port: `/dev/cu.usbmodem2101`
- Upload result: `SUCCESS`
- Card type: `3` (SDHC)
- Card capacity: `29820 MB`
- Filesystem total: `29804 MB`
- Filesystem used: `0 MB`
- Format confirmation: not issued; existing filesystem mounted

## Acceptance Results

- [x] Card mounted without formatting
- [x] Existing filesystem accepted; FAT format not needed
- [x] Card capacity and type were reported
- [x] Temporary file write/read/delete verification passed
- [x] Card remained mounted and responsive after probe

## Exact Serial Output

```text
[HW-006] SD_FORMAT_TEST_START
[HW-006] SD_CLK=11 SD_CMD=10 SD_D0=9 MODE=1BIT FREQUENCY_HZ=20000
[HW-006] FORMAT_IS_DESTRUCTIVE=true
[HW-006][PASS] PINS_CONFIGURED
[HW-006] MOUNT_ATTEMPT FORMAT_IF_FAILED=NO RESULT=SUCCESS
[HW-006][PASS] CARD_MOUNTED_WITHOUT_FORMAT
[HW-006] CARD_TYPE=3 CARD_SIZE_MB=29820 TOTAL_MB=29804 USED_MB=0
[HW-006][PASS] FILESYSTEM_WRITE_READ_DELETE
[HW-006] FORMAT_NOT_NEEDED FILESYSTEM_VERIFIED=true
```

## Observations

The card was detected and mounted successfully without formatting. The
temporary verification file was written, read back, and deleted successfully;
no test file remains on the card.

## Defects and Follow-up

- Formatting is destructive and requires an uppercase `F` confirmation.
