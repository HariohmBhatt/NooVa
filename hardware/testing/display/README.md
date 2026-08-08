# HW-001 Display Test

Standalone validation firmware for the Waveshare ESP32-S3 Touch LCD 3.5
ST7796 panel.

## Source Of Truth

The display bus, I2C expander, reset sequence, and backlight pin are taken from
Waveshare's official `Arduino/examples/08_gfx_helloworld` example in the
[ESP32-S3-Touch-LCD-3.5 repository](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5).
The test uses the same documented `GFX Library for Arduino` 1.5.5 and TCA9554
0.1.2 dependencies.

## Test Phases

1. Verify the TCA9554 connection and ST7796 initialization.
2. Render black, white, red, green, blue, yellow, cyan, magenta, and gray.
3. Render a full-screen border, grid, diagonals, center marker, and labels.
4. Render the geometry pattern at rotations 0, 1, 2, and 3.
5. Test backlight PWM levels 0, 64, 128, and 255.
6. Run a ten-minute repeated full-screen update soak test.

The serial result separates automated checks from visual checks. The operator
must inspect the panel during the color, geometry, rotation, backlight, and
soak phases before marking the visual result as passed.

## Commands

```sh
pio run -e display-test
pio run -e display-test --target upload --upload-port /dev/cu.usbmodem2101
pio device monitor --baud 115200 --port /dev/cu.usbmodem2101
```

Do not run the baseline environment while this test is active. Close any other
serial monitor before uploading.

## Files

- `display_test.cpp`: test firmware
- `REPORT.md`: test execution record and exact serial output
