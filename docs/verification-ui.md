# Production dashboard verification record

Date: 2026-08-20 UTC

## Exact firmware revision

The final code commit flashed to `/dev/ttyACM0` was
`f8a91d8` (`fix(ui): retain metrics in diagnostic states`), on top of
`bf330e9` (`feat(firmware): add LVGL sentinel dashboard`). Both were pushed to
`origin/refactor/clean-foundation` before upload. Unrelated local `README.md`
and `.agents/` changes were not committed.

## Automated evidence

- `pio test -e native`: 24/24 passed, including the new public
  `DashboardPresenter` tests.
- `pio run -e waveshare-esp32-s3-touch-lcd-35`: passed.
- `git diff --check`: passed before the code commit.
- Release size: 158,376 / 327,680 bytes RAM (48.3%); 1,215,777 /
  6,553,600 bytes flash (18.5%).
- Upload identified ESP32-S3 revision 0.2, 16 MB flash and 8 MB embedded PSRAM;
  image hashes verified successfully.

## Connected-device evidence

The exact code commit uploaded successfully. A subsequent serial capture showed
the real backend repeatedly producing `Healthy` with authenticated snapshots,
RSSI between -24 and -25 dBm, UI processing near its intended 200 Hz cadence,
and free heap returning to 176,384 bytes between TLS transactions. During TLS
work, observed heap remained above 132,520 bytes. No reset, panic, watchdog, or
LVGL allocation assertion appeared in the capture.

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
