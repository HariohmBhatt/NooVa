# HW-002 Touch Test

Standalone validation firmware for the FT6336 capacitive touch controller on
the Waveshare ESP32-S3 Touch LCD 3.5.

## Source Of Truth

The I2C bus, controller address, reset sequence, and display setup follow the
official Waveshare LVGL Arduino example. The touch driver is the documented
`SensorLib` 0.3.1 package and exposes the FT6X36-compatible controller API.

## Test Behavior

The firmware:

- Verifies the TCA9554 and ST7796 display path needed for visual touch feedback.
- Verifies FT6336-compatible vendor/chip initialization and diagnostic IDs.
- Polls touch coordinates at 50 Hz.
- Logs put-down, contact, put-up, idle, coordinate, and two-point events.
- Draws a crosshair and coordinate label for each reported point.
- Supports display rotation commands for coordinate mapping checks.

The `TouchDrvFT6X36` API exposes coordinates but not stable contact IDs. The
report must record this limitation instead of treating point-array indexes as
hardware contact identities.

## Commands

```sh
pio run -e touch-test
pio run -e touch-test --target upload --upload-port /dev/cu.usbmodem2101
pio device monitor --baud 115200 --port /dev/cu.usbmodem2101
```

Monitor commands are single characters:

- `0` through `3`: select display rotation
- `r`: advance to the next rotation
- `c`: redraw the idle grid
- `s`: print counters
- `h`: print help

Do not upload this environment until the display soak report is complete, as
uploading replaces the running display test firmware.
