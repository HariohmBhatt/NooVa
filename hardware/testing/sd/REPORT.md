# HW-006 SD acceptance report

## Status

`BLOCKED` — the non-destructive `sd-acceptance` firmware builds, but has not
yet been uploaded and executed on the physical board. Build success is not SD
card acceptance evidence.

The earlier formatter-target run at commit `5806acd` proved that one 29,820 MB
SDHC card could mount and pass a small write/read/delete probe without being
formatted. That historical probe did not exercise deterministic binary
readback, the 1 MiB hash/throughput transfer, remount persistence, complete
workspace cleanup, or the no-card path, so it is not counted as a pass for the
current target.

## Required execution record

- Test ID: `HW-006`
- PlatformIO environment: `sd-acceptance`
- Test root: `/nova-hw-006`
- Interface: GPIO 11/10/9, 1-bit, 20,000 kHz
- Formatting: unavailable (`format_if_mount_failed=false`)

Record the firmware commit, board serial/MAC, card make and capacity, complete
serial output, large-write/read hash and throughput, remount result, cleanup
result, and a second boot with the card removed. Mark `PASS` only when the
inserted-card run passes every automated check and the no-card run reaches the
controlled `BLOCKED` result without a crash or hang.
