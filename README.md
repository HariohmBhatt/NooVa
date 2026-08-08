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

The default firmware is the first local hub-connection milestone. It initializes
the verified display and touch paths, renders a local diagnostic UI, and keeps a
bounded debug log visible on the screen. USB serial is only a development
mirror; the firmware does not wait for a connected computer during startup.

The Wi-Fi page can scan nearby networks, accept a password through the
touchscreen keyboard, persist the selected network in NVS, and reconnect after
future boots. The dashboard automatically registers with a discovered hub and
opens its metrics stream without a pairing code or device token. Connection
remains disabled until a generated Caddy CA trust anchor is compiled into the
firmware.

The server side of the vertical slice lives under `backend/`; its protocol is
defined in `docs/protocol.md`. Start it with Docker Compose on the home server.
The deployment and security boundaries are documented in `docs/deployment.md`
and `docs/security.md`.

The SSH page uses the fixed user name `nova`. Set a password on the touchscreen,
enable SSH, and then connect from the same LAN with `ssh nova@DEVICE_IP`. The
server exposes `help`, `status`, `logs`, `wifi`, and `reboot`; it is not an
arbitrary operating-system shell.

The BOOT button is GPIO0 and active-low. A debounced press turns off the
backlight and pauses SSH, Wi-Fi, and scans for the current power session without
clearing the persisted SSH setting; the services start again on the next normal
boot. The PWR button is active-high on TCA9554 EXIO6 and triggers the same
firmware pause before its hardware power-control behavior. Its six-second hold
powers the board off, and a click powers it on while charging.

## Remote Development

The ESP32 is not a general-purpose build host. Develop and compile on the Mac,
use SSH for diagnostics and controlled commands, and use OTA for development
firmware deployment. SSH enablement also enables the authenticated development
OTA endpoint on port 3232; the `nova-production` profile disables both remote
listeners.

After one USB bootstrap flash containing OTA support, deploy subsequent builds
without USB. ArduinoOTA uses UDP on port 3232:

```sh
NOVA_OTA_HOST=192.168.29.18 NOVA_OTA_PASSWORD='temporary-password' \
  sh scripts/deploy-ota.sh
```

Do not commit either environment variable or place the OTA service on an
untrusted network.

Only one application can own the USB serial/JTAG device at a time. Close
Arduino Serial Monitor or another terminal before uploading or starting a
debug session.

## Project Layout

- `platformio.ini`: pinned PlatformIO environment and board memory settings
- `firmware/`: Arduino application source (`src_dir` is intentionally mapped here)
- `hardware/`: board notes and verified pin mappings
- `docs/`: development documentation
- `backend/`: FastAPI hub, SQLite store, tests, and Compose deployment
- `infrastructure/`: host Avahi and encrypted backup configuration
- `assets/`: firmware assets for future filesystem images
- `scripts/`: host-side development utilities

## Hardware Reference

Use the official [Waveshare ESP32-S3-Touch-LCD-3.5 wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5)
for the schematic and peripheral pin assignments. Do not copy a GPIO value
into firmware until it has been checked against that reference.

See the [hardware testing plan](docs/hardware-testing.md) for the component
validation procedures and acceptance criteria.
