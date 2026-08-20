# NOVA Sentinel 0.1

## Product contract

NOVA Sentinel is an always-on, read-only server status display. Its one job is
to answer, from across a room: **does the server need attention, why, and is
this information current?** The ESP32 is a thin terminal. The Dockerized
server gathers observations, applies health policy, and sends a small status
snapshot; the device owns only connectivity/freshness state and presentation.

Version 0.1 is complete when the connected Waveshare board can display live
CPU, memory, disk, uptime, and at most four configured service checks; explain
the highest-priority problem; distinguish Wi-Fi, server, and monitoring
failures; and recover without a reboot.

### User-visible behavior

- The home screen gives the overall state, primary reason, three host metrics,
  compact service states, and age of the last accepted snapshot.
- `Details` opens all available reasons, service states, server uptime, and
  connection information. `Back` returns home. These are the only 0.1 touch
  actions.
- Missing values render as `Unavailable`, never as zero.
- A lost connection never leaves an old green screen looking current. The
  last snapshot may remain visible, but it is visibly labeled as last-known.
- The device reconnects to Wi-Fi and resumes polling automatically.
- Status does not rely on color alone: every state has text and an icon/shape.

### Non-goals

Version 0.1 does not include remote commands, container restarts, Docker socket
access, graphs/history, backups, GPU metrics, audio alerts, alert
acknowledgement, IMU behavior, SD journaling, OTA, multi-server selection, or
touchscreen credential provisioning. Wi-Fi, server address, bearer token, and
TLS trust anchor are installed during development/provisioning and are never
committed.

## Domain vocabulary

- **Observation**: one raw value or probe result collected by the server.
- **Health policy**: server-owned rules that turn observations into severity
  and reasons.
- **Snapshot**: one bounded, immutable evaluation of current server health.
- **Reason**: stable machine code plus short human explanation for a non-healthy
  condition.
- **Service check**: a configured, read-only HTTP health probe. It is not a
  Docker container inspection.
- **Accepted snapshot**: a protocol-compatible, authenticated HTTP 200 body
  that passes all bounds and validation.
- **Freshness**: time since the device accepted a snapshot, measured with the
  ESP32 monotonic clock rather than wall-clock time.
- **Reported severity**: `healthy`, `warning`, or `critical`, calculated by the
  server.
- **Device state**: what the display presents after local connectivity and
  freshness take precedence over reported severity.
- **Last-known snapshot**: retained data that may aid diagnosis but is not
  current and cannot produce a healthy presentation.

## Exact device states and transitions

The model starts in `starting`. It consumes Wi-Fi events, poll outcomes, and a
wrap-safe `uint32_t` monotonic millisecond value. Rendering never derives state
independently.

| Device state | Entry condition | Exit condition |
| --- | --- | --- |
| `starting` | Boot before configuration/hardware initialization completes | Immediately to the matching configuration/Wi-Fi state |
| `setup_required` | Required Wi-Fi/server/token/CA configuration is absent | Reboot after valid provisioning |
| `wifi_connecting` | Wi-Fi has credentials and is attempting association | To `server_connecting` on association; `wifi_offline` on timeout/loss |
| `wifi_offline` | Wi-Fi is known disconnected | To `wifi_connecting` on an automatic retry, then `server_connecting` on association |
| `server_connecting` | Wi-Fi is connected but no snapshot has ever been accepted; lasts through the first 10 seconds | To reported severity on acceptance; `server_offline` after 10 seconds without acceptance; `monitor_error` on a non-transient contract/authentication error |
| `healthy` | Accepted snapshot age is under 15 seconds and reported severity is healthy | To warning/critical on a newer snapshot; `stale` at 15 seconds; Wi-Fi state immediately on Wi-Fi loss |
| `warning` | Accepted snapshot age is under 15 seconds and reported severity is warning | Same transition rules as healthy |
| `critical` | Accepted snapshot age is under 15 seconds and reported severity is critical | Same transition rules as healthy |
| `stale` | Last accepted snapshot age is at least 15 but under 30 seconds | To reported severity on acceptance; `server_offline` at 30 seconds; Wi-Fi state immediately on Wi-Fi loss |
| `server_offline` | No accepted snapshot for 30 seconds, or no first snapshot within 10 seconds | To reported severity on acceptance; Wi-Fi state immediately on Wi-Fi loss |
| `monitor_error` | HTTP 401/403, unsupported schema, invalid/oversized body, or TLS validation failure | To reported severity on a later accepted snapshot; Wi-Fi state immediately on Wi-Fi loss |

Transient timeout, DNS, connect, HTTP 429, and HTTP 5xx outcomes do not erase
the last accepted snapshot or immediately become `monitor_error`; freshness
drives `stale` and `server_offline`. A contract/authentication error is shown
immediately because retries alone are unlikely to fix it. Wi-Fi state has
highest precedence, then local monitor error, then freshness, then server
severity. Millisecond comparisons use unsigned subtraction so rollover is
safe.

## Server-owned health policy

The backend collects and caches a new snapshot every five seconds, independent
of device requests. Devices only read the cache, keeping work and timing off
the ESP32.

Default policy is configuration-backed and tested:

| Observation | Warning | Critical | Notes |
| --- | ---: | ---: | --- |
| CPU used | >= 85% | >= 95% | Must persist for 3 collections; clears after 2 collections below threshold minus 5 points |
| Memory used | >= 85% | >= 95% | Same persistence/clear rule as CPU |
| Disk used | >= 80% | >= 90% | Immediate; clears below threshold minus 2 points |
| Critical service check | n/a | 2 consecutive failures | Recovers after 2 successes |
| Advisory service check | 2 consecutive failures | n/a | Recovers after 2 successes |

An unavailable CPU or memory observation is a warning; unavailable disk data
or a failed snapshot scheduler is critical because capacity/currentness cannot
be trusted. The highest reason severity becomes the snapshot severity. Within
one severity, reason priority is: monitoring integrity, disk, critical service,
memory, CPU, advisory service. The backend returns at most three reasons in
that order. Thresholds can change through validated environment configuration
without firmware changes.

Each configured service check has a stable ID, display name, HTTP(S) URL,
`critical` or `advisory` importance, and a timeout no greater than 1500 ms. A
2xx or 3xx response is healthy. Checks run concurrently and are limited to four.
The backend itself is covered by collection integrity rather than recursively
probing its public endpoint.

## UI information and interaction requirements

The native layout target is portrait 320 x 480. The home screen gives at least
40% of its visual weight to the state and reason, keeps CPU/memory/disk
secondary, and places service state and freshness below them. Healthy should
feel calm; warning and critical must attract progressively more attention
without animation or constant repainting. Offline views must name the failed
layer (`Wi-Fi`, `Server`, or `Monitor`) and show last-known age when available.

Text must remain legible at arm's length, touch targets must be at least 44 x 44
pixels, and the design must tolerate maximum contract string lengths without
overlap. A visual design agent may refine typography, spacing, colors, and
iconography, but may not hide freshness, remove text labels, add 0.1 actions,
or change the state precedence. UI refreshes only when the view model changes
or once per second for visible age/uptime, avoiding needless full-screen work.

## Acceptance criteria

1. A fresh valid snapshot presents the exact server severity and all available
   required metrics; null values say `Unavailable`.
2. Warning/critical screens show the server's highest-priority reason.
3. The device presents stale at 15 seconds and server offline at 30 seconds,
   while retaining clearly marked last-known values.
4. Wi-Fi loss is distinguishable from server loss and recovers without reboot.
5. Authentication, TLS, incompatible schema, malformed JSON, and oversized
   payloads cannot be presented as healthy.
6. Backend policy and firmware state behavior pass host tests through their
   public interfaces, including clock rollover.
7. The backend runs from the single `backend/compose.yaml` instance, exposes
   only the TLS proxy on the configured LAN address, and has no Docker socket.
8. Firmware builds with pinned dependencies, is flashed to the connected board,
   and passes the integration and soak matrix in `hardware-test-plan.md`.
9. During a 24-hour run, polling continues, reconnects recover, UI remains
   responsive, and free heap has no sustained downward trend.

