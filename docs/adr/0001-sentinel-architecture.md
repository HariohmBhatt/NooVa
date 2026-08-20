# ADR 0001: Thin Sentinel terminal with server-owned health policy

- Status: accepted for NOVA Sentinel 0.1
- Date: 2026-08-20

## Context

The target is a Waveshare ESP32-S3 Touch LCD 3.5 with constrained memory and an
always-visible display. The current branch is a clean foundation. The archived
`codex/simple-device-stats-wifi` branch proves board display, touch, Wi-Fi, and
reconnect paths, but its large telemetry/SSH/OTA feature set and GPU-oriented
64-byte stream do not match the focused first version.

We need a complete server sentinel that remains easy to reason about, host-test,
and validate on hardware. Policy changes should not require reflashing the
device, and a server outage must be detectable by the device itself.

## Decision

Use a Dockerized Python/FastAPI backend behind Caddy TLS and a C++17/Arduino
ESP32 firmware using LVGL. The backend caches a policy-evaluated snapshot every
five seconds. The device polls bounded JSON over authenticated HTTPS, applies
only connectivity/freshness precedence, and renders an immutable view model.

The backend is read-only: host `/proc`, `/sys` when needed, and the configured
disk mount are mounted read-only. Configured HTTP probes replace Docker socket
inspection. The Compose deployment is unprivileged, drops Linux capabilities,
uses `no-new-privileges`, a read-only root filesystem where practical, bounded
logs, and exposes only Caddy on an explicitly configured LAN address.

### Deep modules and dependency direction

```text
host files + HTTP targets
          |
          v
  [StatusMonitor] ---> immutable StatusSnapshot ---> FastAPI route adapter
          |                                               |
          |                                         Caddy/TLS
          |                                               |
          +-----------------------------------------------+
                                                          v
Wi-Fi adapter ---> [StatusClient] ---> PollOutcome ---> [SentinelModel]
                                                          |
                                                          v
                                                       DeviceView
                                                          |
                                       LVGL dashboard ---> board display/touch
```

Square brackets mark the three deep modules:

1. **StatusMonitor** interface: `current() -> StatusSnapshot`. It hides metric
   collection, concurrent probes, threshold persistence/hysteresis, reason
   priority, snapshot caching, and serialization invariants. The HTTP route
   learns none of those details.
2. **StatusClient** interface: `poll(now_ms) -> PollOutcome`. It hides HTTPS,
   authentication headers, response bounds, JSON validation, and retry/error
   classification. It does not decide displayed state.
3. **SentinelModel** interface: `apply(event, now_ms)` and `view(now_ms) ->
   DeviceView`. It hides last-known retention, exact state transitions,
   freshness, precedence, selection of primary reason, and rollover-safe time.
   Callers and host tests use this same interface.

Hardware access points remain narrow adapters. `BoardDisplay` implements the
pixel flush expected by the LVGL dashboard; `BoardTouch` supplies normalized
touch samples; the Arduino Wi-Fi/TLS implementation supplies the production
network behavior. UI code only sees `DeviceView` and emits `Details`/`Back`
events. `main.cpp` constructs modules and calls non-blocking `update`; it owns
no policy, parsing, or layout logic.

Dependency direction is inward: hardware/network and framework adapters depend
on domain types; the status/domain modules never include Arduino, LVGL,
FastAPI, or host filesystem details.

### Real seams and adapters

- Server observation seam: Linux proc/filesystem and real HTTP-probe adapters
  in production; fixture-filesystem and scripted-probe adapters in tests.
- Firmware polling seam: Arduino HTTPS adapter on device; scripted poll adapter
  in host state tests.
- Time seam: monotonic system adapters in production; manually advanced clocks
  in server and firmware tests.
- Display and touch are hardware seams justified by board adapters and
  host-side UI preview/test adapters. Pixel timing is tested only on hardware.

All other helpers stay inside a deep module. We will not create one-method
interfaces merely to mock implementation details. Tests replace adapters at a
seam and assert through the module interface; they do not layer tests over
private helpers.

### Proposed repository layout

```text
platformio.ini
src/
  main.cpp
  config/BoardConfig.h
  config/Provisioning.example.h
  hardware/BoardDisplay.{h,cpp}
  hardware/BoardTouch.{h,cpp}
  network/WifiManager.{h,cpp}
  network/StatusClient.{h,cpp}
  status/StatusSnapshot.h
  status/SentinelModel.{h,cpp}
  ui/Dashboard.{h,cpp}
  ui/Theme.h
test/
  test_sentinel_model.cpp
  test_status_contract.cpp
backend/
  app/__init__.py
  app/config.py
  app/domain.py
  app/monitor.py
  app/adapters.py
  app/main.py
  tests/fixtures/
  tests/test_monitor.py
  tests/test_api.py
  deploy/Caddyfile
  Dockerfile
  compose.yaml
  pyproject.toml
docs/
```

Files may be combined when a proposed file would be only pass-through code;
the interfaces above, not the file count, are the architectural commitment.

## Hardware assumptions

The following was verified in the archived branch against Waveshare examples
and physical tests dated 2026-08-08. Implementation must re-check the official
Waveshare documentation and then test the currently connected board.

- ESP32-S3, 16 MB flash, 8 MB octal PSRAM; generic PlatformIO
  `esp32-s3-devkitc-1` target with explicit memory overrides.
- ST7796 portrait LCD, 320 x 480; SPI MISO GPIO 2, MOSI GPIO 1, clock GPIO 5,
  DC GPIO 3, no LCD CS, reset through TCA9554 address `0x20` output 1, and
  backlight PWM GPIO 6.
- Shared I2C uses SDA GPIO 8 and SCL GPIO 7.
- FT6336-compatible touch at `0x38`, reported as FT6236U by the verified
  SensorLib driver; single-point input is sufficient for 0.1.
- ESP32-S3 station mode supports only 2.4 GHz Wi-Fi. Prior hardware testing
  passed DHCP, DNS, HTTP, and five reconnect cycles.
- Audio, IMU, RTC, and SD are intentionally unused in 0.1.

Use named `constexpr` values, fixed-capacity records and parser storage, no
application-level dynamic allocation without justification, bounded work per
loop, and no delays in normal update paths.

## Security decisions

- TLS certificate and hostname validation are mandatory; there is no insecure
  fallback. A private Caddy CA may be pinned in provisioned firmware.
- A random per-device bearer token is supplied as a secret and compared by the
  backend without logging. Source contains only an example provisioning file.
- The endpoint is read-only, LAN-bound, rate-limited, and returns no process
  lists, command surfaces, environment values, or arbitrary file contents.
- Compose does not use host networking, privileged mode, writeable host mounts,
  or `/var/run/docker.sock`. Secrets live in an ignored `.env`/provisioning
  file, not image layers or git.
- Error and serial logs redact SSIDs, tokens, headers, and target URLs that may
  contain credentials.

## Pre-agreed TDD seams

Implementation begins with failing tests at these interfaces:

- `StatusMonitor.current`: metric thresholds, persistence/clear behavior,
  missing observations, service importance, reason ordering/limit, nullable
  metrics, and cached collection cadence using fixture/scripted adapters.
- HTTP route around `StatusMonitor`: auth, schema negotiation, exact JSON,
  error codes, headers, and maximum encoded response.
- `SentinelModel`: every transition in the product table, precedence, retained
  last-known values, boundary times 14,999/15,000/29,999/30,000 ms, recovery,
  and `uint32_t` rollover.
- Status contract decoder through `StatusClient`'s parsing seam: valid fixture,
  nulls, enum/type/range/count/string/body violations, unknown fields, and an
  input delivered in small chunks.
- Dashboard view tests/preview: every device state, longest strings, null
  metrics, four services, details/back, and no color-only meaning.

## Consequences

Health policy changes remain local to the server and device work stays small.
JSON costs more bytes than the archived fixed-width stream, but a five-second,
4096-byte maximum is negligible on LAN and materially improves inspectability
and evolution. Polling has more request overhead than WebSocket/SSE; keep-alive
may reduce it, and the simpler failure model is worth it for 0.1. TLS and manual
provisioning add setup work, accepted to avoid an insecure first release.

Rejected for 0.1: the archived GPU-specific binary stream (not expressive
enough for reasons/services), WebSockets/SSE (more lifecycle state), Docker
socket inspection (excess privilege), device-owned thresholds (duplicated
policy), and remote actions (turns a monitor into an administration surface).

