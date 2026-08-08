# HW-001 Display Test Report

## Status

`INCOMPLETE`

The firmware was built and uploaded successfully. The TCA9554, ST7796, and
320x480 initialization checks passed, and the color, geometry, rotation,
backlight, and soak phases emitted their test markers. The ten-minute soak
completion was not captured: the serial capture was aborted, and a later
serial-state read restarted the test before completion. Visual acceptance was
not confirmed by the operator in the captured record.

## Test Definition

- Test ID: `HW-001`
- Device: Waveshare ESP32-S3 Touch LCD 3.5
- Panel: ST7796
- Resolution: 320 x 480
- Test firmware: `hardware/testing/display/display_test.cpp`
- PlatformIO environment: `display-test`
- Planned soak duration: 10 minutes
- Build result: `SUCCESS`
- Display firmware image size: 331453 bytes
- Application partition size: 6553600 bytes

## Hardware Configuration

The configuration below is copied from the official Waveshare Arduino display
example and is also printed by the test firmware:

- SPI MISO: GPIO 2
- SPI MOSI: GPIO 1
- SPI SCLK: GPIO 5
- LCD CS: not connected in the official configuration
- LCD DC: GPIO 3
- LCD reset: controlled by TCA9554 output 1
- I2C SDA: GPIO 8
- I2C SCL: GPIO 7
- TCA9554 address: `0x20`
- Backlight: GPIO 6

## Execution Record

- Date: 2026-08-08
- Firmware revision: `ca74c8f58ba7f49dd48827a6e5a49dc44b98eb2f` plus uncommitted working-tree changes
- Board serial: `1C:DB:D4:79:7D:AC`
- Board MAC: `1c:db:d4:79:7d:ac`
- Upload port: `/dev/cu.usbmodem2101`
- Upload result: `SUCCESS`
- Accessories and conditions: display connected, USB-Serial/JTAG connection

## Acceptance Results

- [x] TCA9554 connection and LCD reset passed in serial output
- [x] ST7796 initialization passed in serial output
- [ ] Solid color sequence visually passed
- [ ] Geometry pattern visually passed
- [ ] Rotation 0 visually passed
- [ ] Rotation 1 visually passed
- [ ] Rotation 2 visually passed
- [ ] Rotation 3 visually passed
- [ ] Backlight off, low, medium, and maximum visually passed
- [ ] Ten-minute soak completed without artifacts or reset
- [x] Automated initialization and rendering markers passed
- [ ] Visual review passed

## Exact Serial Output

The following are exact fragments captured from the hardware. They are kept as
separate captures because the serial session was reset/interrupted between
phases; they must not be interpreted as one uninterrupted completed run.

```text
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0x29 (SPI_FAST_FLASH_BOOT)
Saved PC:0x4202ac96
SPIWP:0xee
mode:DIO, clock div:1
load:0x3fce3808,len:0x4bc
load:0x403c9700,len:0xbd8
load:0x403cc700,len:0x2a0c
entry 0x403c98d0
[    95][I][esp32-hal-psram.c:96] psramInit(): PSRAM enabled

[HW-001] DISPLAY_TEST_START
[HW-001] PANEL=ST7796
[HW-001] GEOMETRY=320x480
[HW-001] SPI_MISO=2 SPI_MOSI=1 SPI_SCLK=5
[HW-001] LCD_CS=-1 LCD_DC=3 LCD_RST=-1
[HW-001] I2C_SDA=8 I2C_SCL=7 TCA9554=0x20 RESET_PIN=1
[HW-001] BACKLIGHT_GPIO=6
[   117][I][esp32-hal-i2c.c:75] i2cInit(): Initialising I2C Master: sda=8 scl=7 freq=100000
[HW-001][PASS] TCA9554_CONNECTED
[HW-001][PASS] ST7796_INITIALIZED
[HW-001][PASS] DISPLAY_DIMENSIONS=320x480
[HW-001] COLOR_TEST_START visual_review_required=true
[HW-001] COLOR=BLACK RGB565=0x0000 HOLD_MS=1500
[HW-001] COLOR=WHITE RGB565=0xFFFF HOLD_MS=1500
[HW-001] COLOR=RED RGB565=0xF800 HOLD_MS=1500
[HW-001] COLOR=GREEN RGB565=0x07E0 HOLD_MS=1500
[HW-001] COLOR=BLUE RGB565=0x001F HOLD_MS=1500

[HW-001][PASS] GEOMETRY_PATTERN_RENDERED
[HW-001] ROTATION_TEST_START visual_review_required=true
[HW-001] ROTATION=0 WIDTH=320 HEIGHT=480 HOLD_MS=4000
[HW-001] ROTATION=1 WIDTH=480 HEIGHT=320 HOLD_MS=4000
[HW-001] ROTATION=2 WIDTH=320 HEIGHT=480 HOLD_MS=4000
[HW-001] ROTATION=3 WIDTH=480 HEIGHT=320 HOLD_MS=4000
[HW-001][PASS] ROTATION_SEQUENCE_RENDERED
[HW-001] BACKLIGHT_TEST_START visual_review_required=true
[HW-001] BACKLIGHT_PWM=0 HOLD_MS=2000
[HW-001] BACKLIGHT_PWM=64 HOLD_MS=2000
[HW-001] BACKLIGHT_PWM=128 HOLD_MS=2000
[HW-001] BACKLIGHT_PWM=255 HOLD_MS=2000
[HW-001][PASS] BACKLIGHT_SEQUENCE_RENDERED

[HW-001] SOAK_PROGRESS ELAPSED_MS=10304 FRAMES=32
[HW-001] SOAK_PROGRESS ELAPSED_MS=20608 FRAMES=64
[HW-001] SOAK_PROGRESS ELAPSED_MS=30912 FRAMES=96
[HW-001] SOAK_PROGRESS ELAPSED_MS=41216 FRAMES=128
```

## Observations

The ST7796 interface in the official example does not expose a readable
display ID or pixel readback path, so visual inspection is required for color,
geometry, orientation, backlight, and artifact checks. The report remains
incomplete until the operator confirms those checks and a complete soak run
ends with `TEN_MINUTE_SOAK_COMPLETE` and `DISPLAY_TEST_COMPLETE`.

## Defects and Follow-up

- The display soak was not completed in the captured run.
- Exact output for yellow, cyan, magenta, gray, geometry start, soak completion,
  and final summary was not captured and is intentionally not inferred.
