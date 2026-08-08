# HW-002 Touch Test Report

## Status

`IN PROGRESS`

The touch firmware built and uploaded successfully. The TCA9554, ST7796, and
FT6X36-compatible touch initialization checks passed, and live single-point
press, contact, and release events were observed. Controlled corner, edge,
center, gesture, rotation, multi-touch, and five-minute idle checks are still
pending.

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

- Date: 2026-08-08
- Firmware commit: `d5d0e0a`
- Board serial: `1C:DB:D4:79:7D:AC`
- Board MAC: `1c:db:d4:79:7d:ac`
- Upload port: `/dev/cu.usbmodem2101`
- Upload result: `SUCCESS`
- Display soak status before upload: `COMPLETE` (operator-confirmed)
- Accessories and conditions: display connected, USB-Serial/JTAG connection

## Acceptance Results

- [x] TCA9554 and display feedback path initialized
- [x] FT6336-compatible controller initialized
- [x] Vendor ID and chip ID recorded
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

The following exact fragments were captured after upload. The monitor attached
after the earliest boot header, so the initialization library diagnostics and
the test-ready marker are recorded here; the controlled interaction results
will be appended as the procedure is executed.

```text
[   462][W][Wire.cpp:301] begin(): Bus already started in Master Mode.
[   470][I][TouchDrvFT6X36.hpp:362] initImpl(): Vend ID: 0x11
[   475][I][TouchDrvFT6X36.hpp:363] initImpl(): Chip ID: 0x64
[   481][I][TouchDrvFT6X36.hpp:364] initImpl(): Firm Version: 0x7
[   487][I][TouchDrvFT6X36.hpp:365] initImpl(): Point Rate Hz: 10
[   493][I][TouchDrvFT6X36.hpp:366] initImpl(): Thresh : 30
[   500][I][TouchDrvFT6X36.hpp:371] initImpl(): Chip library version : 0x300a
[   507][I][TouchDrvFT6X36.hpp:374] initImpl(): Chip period of monitor status : 0x28
[HW-002][PASS] FT6X36_INITIALIZED
[HW-002][PASS] ST7796_INITIALIZED
[HW-002][PASS] DISPLAY_DIMENSIONS_320X480
[HW-002] CHIP_ID=0x64 MODEL=FT6236U VENDOR_ID=0x11
[HW-002] LIBRARY_VERSION=0x300A ERROR_CODE=0x0F
[HW-002] TOUCH_TEST_READY
[HW-002] COMMANDS: 0-3=set rotation, r=next rotation, c=clear, s=summary, h=help
[HW-002] EVENT=PUT_DOWN COUNT=1
[HW-002] EVENT=CONTACT POINT_COUNT=1
[HW-002] POINTS=1
[HW-002] POINT_INDEX=0 X=250 Y=241
[HW-002] EVENT=PUT_UP COUNT=1
```

## Known Driver Limitation

The selected `TouchDrvFT6X36` API returns point coordinates and a point count,
but does not expose stable contact IDs. The test records point indexes and
multi-point behavior; it does not claim contact-ID validation.

## Observations

The board produced repeated live single-point contact streams with clean
put-down and put-up transitions during the initial exploratory interaction.
Those events are evidence that the controller and coordinate path are active,
but they are not treated as controlled acceptance results. The driver reports
the controller model as `FT6236U` while the board documentation identifies the
touch component as FT6336-compatible.
