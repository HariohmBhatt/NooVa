# Nova Firmware

PlatformIO-based Arduino firmware for the Waveshare ESP32-S3 Touch LCD 3.5.

## Toolchain

- PlatformIO Core 6.1.19 or newer
- Espressif32 platform 7.0.1
- Arduino-ESP32 core 3.2.x
- C++17

Install PlatformIO on macOS with Homebrew:

```sh
brew install platformio
pio --version
```

The project uses PlatformIO's `esp32-s3-devkitc-1` board definition as a
generic ESP32-S3 build target. The board definition is overridden in
`platformio.ini` for the Waveshare board's verified 16 MB flash and 8 MB
octal PSRAM. This is a build configuration choice, not a claim that the
Waveshare board is electrically identical to the DevKitC-1.

## CLI Workflow

Run these commands from the repository root:

```sh
# Resolve the toolchain and build the firmware
pio run

# List connected serial devices
pio device list

# Upload, selecting the port explicitly when more than one is present
pio run --target upload --upload-port /dev/cu.usbmodemXXXX

# Open the serial monitor
pio device monitor --baud 115200 --port /dev/cu.usbmodemXXXX

# Start a GDB debug session through the ESP32-S3 USB JTAG interface
pio debug --interface=gdb

# Clean generated build output
pio run --target clean

# Erase the complete flash chip when a clean device state is required
pio run --target erase --upload-port /dev/cu.usbmodemXXXX
```

The initial firmware is a hardware bring-up diagnostic. It reports chip,
flash, PSRAM, CPU, and uptime information over USB serial. It intentionally
does not configure display, touch, audio, RTC, IMU, storage, or GPIO pins;
those pins must be added from the official Waveshare schematic before use.

Only one application can own the USB serial/JTAG device at a time. Close
Arduino Serial Monitor or another terminal before uploading or starting a
debug session.

## Project Layout

- `platformio.ini`: pinned PlatformIO environment and board memory settings
- `firmware/`: Arduino application source (`src_dir` is intentionally mapped here)
- `hardware/`: board notes and verified pin mappings
- `docs/`: development documentation
- `assets/`: firmware assets for future filesystem images
- `scripts/`: host-side development utilities

## Hardware Reference

Use the official [Waveshare ESP32-S3-Touch-LCD-3.5 wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5)
for the schematic and peripheral pin assignments. Do not copy a GPIO value
into firmware until it has been checked against that reference.

See the [hardware testing plan](docs/hardware-testing.md) for the component
validation procedures and acceptance criteria.
