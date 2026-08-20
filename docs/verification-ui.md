# Production dashboard verification record

Date: 2026-08-20 UTC

## Exact firmware revision

The latest code commit flashed to `/dev/ttyACM0` was `f84268e`
(`fix(ui): harden dashboard status semantics`). It is the focused cleanup of
the production dashboard added in `bf330e9` and corrected in `f8a91d8`.
`f84268e` was pushed to `origin/refactor/clean-foundation` before upload.
Unrelated local `README.md` and `.agents/` changes were not committed.

## Automated evidence

- `pio test -e native`: 24/24 passed, including freshness, nullable metric,
  uptime, connection-copy, and service presentation assertions.
- Clean `pio run -e waveshare-esp32-s3-touch-lcd-35`: passed.
- `git diff --check`: passed before the code commit.
- `pio check -e waveshare-esp32-s3-touch-lcd-35 --fail-on-defect high`:
  passed its high-severity threshold. Reported medium/low findings were in
  pinned third-party display/touch libraries, plus false unused-function
  reports at hardware adapter boundaries; none were in the changed UI files.
- The Variant D web symbol audit passed all 11 fixtures on Home and Details
  (22 combinations). This revalidates the design reference, not LVGL pixels.
- Release size: 159,176 / 327,680 bytes RAM (48.6%); 1,219,017 /
  6,553,600 bytes flash (18.6%). Relative to `f8a91d8`, the cleanup adds 800
  bytes of static RAM and 3,240 bytes of flash.
- Upload identified ESP32-S3 revision 0.2, 16 MB flash and 8 MB embedded PSRAM;
  image hashes verified successfully.

## Connected-device evidence

The exact `f84268e` image uploaded successfully with verified image hashes to
the ESP32-S3 revision 0.2, 16 MB flash, 8 MB PSRAM board. A subsequent serial
capture showed device state ID 5 (`healthy`) with authenticated snapshots,
RSSI between -24 and -26 dBm, and UI processing near its intended 200 Hz
cadence. Free heap returned to 175,328 bytes between TLS transactions; the
lowest observed transient value was 131,620 bytes. No reset, panic, watchdog,
or LVGL allocation assertion appeared in the capture.

## Honest pending checks

No camera or operator feedback was available for this pass, so panel
orientation, visual clipping, color fidelity, stale arc appearance, and
physical Details/Back touch alignment remain pending. Driver initialization
and serial/UI cadence are not substitutes for visual acceptance.

The connected board was verified on the real healthy path only. Compile-time
fixture cycling for every `DeviceState` was not left in release firmware, and
destructive Wi-Fi/server/authentication drills were not repeated during this
UI pass. Every state remains covered by the separately reviewed 320 x 480 host
prototype; real-panel state cycling and the 24-hour soak remain required before
declaring Sentinel 0.1 fully accepted.
