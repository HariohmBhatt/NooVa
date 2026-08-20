# NOVA Sentinel 0.1 verification plan

Every coherent firmware change must pass its relevant host checks and compile.
Changes affecting hardware are then flashed to the connected Waveshare board.
Record tool versions, commit/worktree revision, serial port, board identity,
commands, exact automated results, operator visual results, and exceptions.

## Staged implementation plan

1. Pin PlatformIO/backend tools and re-establish verified display, touch, and
   Wi-Fi adapters under `/src`; build and run focused hardware smoke tests.
2. Test-drive the pure `SentinelModel` and bounded status decoder on the host.
3. Test-drive a Dockerized fixture endpoint, Caddy TLS, authentication, and the
   server `StatusMonitor` interface.
4. Flash a walking skeleton: fixed healthy server snapshot to real ESP32,
   showing state, age, and CPU. Verify the complete TLS/poll/render path.
5. Add real host observations, policy, and up to four configured service checks;
   exercise policy through server tests before reflashing.
6. Integrate the separately reviewed visual design and its host preview, then
   verify touch and all device states on the physical display.
7. Run destructive failure drills and the 24-hour soak. Fix and repeat failed
   rows before calling 0.1 complete.

Each stage should be one coherent commit after all applicable checks pass, then
pushed to the working branch as required by `AGENTS.md`.

## Verification matrix

| ID | Layer | Procedure | Required result |
| --- | --- | --- | --- |
| H-01 | Host firmware | Native tests for state model and protocol fixtures, including rollover and bounds | All pass; sanitizer/static checks report no defects where supported |
| H-02 | Firmware build | Clean PlatformIO build with pinned dependencies | Success; flash/RAM sizes recorded and no unexpected dynamic allocation introduced |
| B-01 | Backend | Unit/integration tests with fixture `/proc`, disk, clocks, and scripted probes | Policy, hysteresis, order, nulls, cache cadence, auth, and errors pass |
| B-02 | Compose | Build and start the single Compose instance; inspect config/health/log bounds | API and Caddy healthy; only configured TLS port exposed; no Docker socket/privilege/writeable host mount |
| B-03 | Contract | Request maximum valid snapshot and all error fixtures with `curl` | Headers/status/body/bounds match `status-protocol.md` |
| D-01 | Display | Render calibration plus every home/details state on the real ST7796 | Correct portrait geometry/colors; no clipping at max strings; legible at arm's length; no artifacts |
| D-02 | Touch | Exercise Details/Back repeatedly, corners, press/release, and idle input | Reliable targets; no stuck/phantom action; UI remains responsive while polling |
| D-03 | Wi-Fi | Boot, associate, DHCP/DNS, remove AP, restore AP, repeat 5 cycles | Correct Wi-Fi states and automatic recovery without reboot |
| I-01 | Healthy path | Run real backend with normal host/probes | Fresh healthy view; metrics agree with backend response within rounding |
| I-02 | Policy | Feed threshold/service fixtures or controlled failures | Warning/critical and primary reasons exactly match server output |
| I-03 | Backend stop | Stop API container for >30 seconds, then start it | Stale at 15 s, server offline at 30 s, then fresh state without device reboot |
| I-04 | Wi-Fi loss | Disable AP/network while backend stays healthy, then restore | Wi-Fi offline is distinct from server offline; last-known is labeled; automatic recovery |
| I-05 | Monitor errors | Wrong token, wrong CA/hostname, schema 2, malformed/oversized body | Immediate monitor error; no old snapshot shown as current; no secret in logs |
| I-06 | Missing data | Make each observation unavailable independently | UI says Unavailable; server policy and reason follow spec; never fabricated zero |
| I-07 | Reboot order | Reboot device first and server first in separate runs | Both orders converge to a fresh correct state without manual intervention |
| I-08 | Soak | Run normal polling/UI for 24 hours with periodic service and network interruptions | No crash/watchdog reset; recovery works; heap has no sustained downward trend; backend load/log size stay bounded |

Visual acceptance requires a human because this panel has no reliable pixel
readback. Automated serial markers do not substitute for checking orientation,
text clipping, color, touch alignment, and artifacts on the connected display.

