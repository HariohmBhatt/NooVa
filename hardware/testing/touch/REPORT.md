# HW-002 Touch Test Report

## Status

`NOT RUN`

## Test Definition

- Test ID: `HW-002`
- Device: Waveshare ESP32-S3 Touch LCD 3.5
- Controller: FT6336, accessed through `TouchDrvFT6X36`
- Display used for feedback: ST7796, 320 x 480
- Test firmware: `hardware/testing/touch/touch_test.cpp`
- PlatformIO environment: `touch-test`
- SensorLib version: `0.3.1`

## Hardware Configuration

- I2C SDA: GPIO 8
- I2C SCL: GPIO 7
- Touch address: `0x38`
- TCA9554 address: `0x20`
- Display reset: TCA9554 output 1

## Execution Record

- Date: pending
- Firmware commit: pending
- Board serial or MAC: pending
- Upload port: `/dev/cu.usbmodem2101`
- Display soak status before upload: pending
- Accessories and conditions: pending

## Acceptance Results

- [ ] TCA9554 and display feedback path initialized
- [ ] FT6336-compatible controller initialized
- [ ] Vendor ID and chip ID recorded
- [ ] Corner and edge coordinates verified
- [ ] Center coordinate verified
- [ ] Press, contact, and release event order verified
- [ ] Horizontal, vertical, and diagonal motion verified
- [ ] Two-point coordinate stream verified
- [ ] Rotations 0 through 3 verified
- [ ] Five-minute idle phantom-event observation passed
- [ ] No stuck contact after release
- [ ] Touch counters and raw logs captured

## Exact Serial Output

The exact output will be appended after the display soak is complete and this
test is uploaded.

```text
PENDING HARDWARE EXECUTION
```

## Known Driver Limitation

The selected `TouchDrvFT6X36` API returns point coordinates and a point count,
but does not expose stable contact IDs. The test records point indexes and
multi-point behavior; it does not claim contact-ID validation.

## Observations

Pending hardware execution.
