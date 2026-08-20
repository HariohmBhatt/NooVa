# Firmware core verification record

This record covers the firmware core before the final dashboard integration.
The checks ran on 2026-08-20 in UTC.

## Revision

| Item | Value |
| --- | --- |
| Starting commit | `a18a038f80dd8d0b43293c3b767a8b4758e1db78` |
| Tested firmware commit | `cb44b8a89ed6d3ccd365eb242724715c0d941cf9` |
| Branch | `refactor/clean-foundation` |
| Upload port | `/dev/ttyACM0` |

The tracked firmware files matched `cb44b8a` during the upload and device
checks. The shared worktree also contained unrelated changes to `README.md` and
an untracked `.agents/` directory. The firmware commit did not include either
item.

## Toolchain

| Tool or package | Version |
| --- | --- |
| Host | Linux `7.0.0-29-generic` x86_64 |
| PlatformIO Core | `6.1.19` |
| PlatformIO Espressif 32 platform | `7.0.1` |
| Arduino-ESP32 framework package | `3.20017.241212+sha.dcc1105b` |
| Xtensa ESP32-S3 toolchain | `8.4.0+2021r2-patch5` |
| Python | `3.13.13` |
| ArduinoJson | `6.21.5` |
| GFX Library for Arduino | `1.5.5` |
| SensorLib | `0.3.1` |
| TCA9554 | `0.1.2` |
| Docker Engine | `29.5.3` |
| Docker Compose | `5.1.4` |

## Automated checks

| Check | Command | Result |
| --- | --- | --- |
| Host firmware tests | `pio test -e native` | Passed, 19 of 19 tests |
| Provisioning failure cleanup | `test/test_provisioning_generator.sh` | Passed; no temporary secret file or token output |
| ESP32 release build | `pio run -e waveshare-esp32-s3-touch-lcd-35` | Passed |
| Static scan | `pio check -e waveshare-esp32-s3-touch-lcd-35 --skip-packages` | Passed; reported existing library diagnostics and no project-source error |
| Whitespace check | `git diff --check` | Passed |
| Exact revision upload | `pio run -e waveshare-esp32-s3-touch-lcd-35 -t upload` | Passed on `/dev/ttyACM0` |

The release image used 78,376 of 327,680 RAM bytes, or 23.9%. It used 946,661
of 6,553,600 flash bytes, or 14.4%.

The 19 host tests include six `SentinelModel` cases and 13 public
`StatusClient` cases. The `StatusClient` suite covers 1-byte and 7-byte response
delivery, request headers, transport failures, HTTP classification, retries,
schema rules, fixed string limits, UTF-8, and exact body and header boundaries.

## Connected board

The upload tool identified an ESP32-S3 revision 0.2 with 16 MB flash and 8 MB
embedded PSRAM. Boot diagnostics reported these results:

- The ST7796 adapter initialized at 320 by 480 pixels.
- The FT6336-compatible touch adapter initialized on the shared I2C bus.
- Stored Wi-Fi credentials connected to the access point. During the healthy
  run, RSSI stayed between -24 dBm and -26 dBm.
- The HTTPS client accepted authenticated status responses through Caddy with
  CA and TLS hostname validation enabled. The device reached `healthy` without
  a reboot or an insecure fallback.

The healthy serial report ran once per second. The touch adapter ran 50 polls
per second. Free heap returned to about 255,264 bytes between TLS transactions
and fell to about 211,624 bytes during a transaction.

The HTTPS worker has a 12,288-byte static stack. Temporary FreeRTOS
instrumentation reported a minimum unused stack value of 6,904 bytes after
healthy polling and the outage drill. The observed headroom was 56.2%, and the
maximum observed stack use was 5,384 bytes. The production serial report does
not retain the stack scan and does not include the request, token, certificate,
SSID, or target address.

## Backend outage and recovery

The outage check stopped only the Caddy service for 40 seconds. The API
container remained healthy. During the outage:

- The retained snapshot changed from `healthy` to `stale` at an age between
  14,820 ms and 15,820 ms.
- The state changed from `stale` to `server_offline` at an age between 29,820
  ms and 30,820 ms.
- Serial view reports continued once per second.
- The touch count increased by 50 on each one-second report.
- Free heap held at about 255,504 bytes between failed TLS attempts.
- HTTPS worker stack headroom did not fall below 6,920 bytes during the outage.

After Caddy restarted, Docker reported both services healthy. The same firmware
process returned to `healthy`, accepted new snapshots, and reset snapshot age
without a device reboot. Touch and serial report cadences did not change during
recovery.

## Checks pending final UI

This pass did not inspect the panel by eye and did not press dashboard targets.
The `DISPLAY=ready` and `TOUCH=ready` messages prove driver initialization, not
layout quality or touch alignment. The final UI pass must check portrait
orientation, text clipping, colors, artifacts, and repeated Details and Back
touches on the physical panel.

The 24-hour soak, access-point loss cycles, wrong-certificate tests, and all
controlled warning and critical UI states also remain pending.
