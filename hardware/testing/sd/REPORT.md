# HW-006 SD Card Formatting Report

## Status

`NOT RUN`

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

- Date: pending
- Firmware commit: `fd8256f`
- Board serial or MAC: pending
- Upload port: `/dev/cu.usbmodem2101`
- Card capacity/type: pending
- Format confirmation: pending

## Acceptance Results

- [ ] Card mounted or format operation detected the inserted card
- [ ] FAT filesystem format completed after explicit confirmation
- [ ] Card capacity and type were reported
- [ ] Temporary file write/read/delete verification passed
- [ ] Card remained responsive after formatting

## Exact Serial Output

```text
PENDING HARDWARE EXECUTION
```

## Observations

Pending hardware execution.

## Defects and Follow-up

- Formatting is destructive and requires an uppercase `F` confirmation.
