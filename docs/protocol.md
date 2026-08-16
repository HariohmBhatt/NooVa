# NOVA Hub Protocol

Version 1 of the terminal-to-hub protocol is JSON over HTTPS and WSS.

## Transport

- HTTPS is used for pairing and diagnostics.
- WSS is used for the persistent authenticated terminal session.
- The terminal trusts the deployment's Caddy root CA. It must never use an insecure TLS fallback.
- The canonical server identity is `nova-hub.local` on TCP port `443`; the deployment certificate also covers the configured LAN IP for mDNS/manual-IP fallback.
- The terminal token is sent only inside the first TLS-protected `device.hello` message.

## Envelope

Every message uses this shape:

```json
{
  "protocol_version": 1,
  "type": "health.snapshot",
  "timestamp": "2026-08-08T14:00:00Z",
  "payload": {}
}
```

`protocol_version` is an integer. A major version mismatch is fatal. Unknown payload fields may be ignored by a compatible peer.

## Pairing

The terminal sends `POST /v1/pair`:

```json
{
  "protocol_version": 1,
  "pairing_code": "12345678",
  "installation_nonce": "base64url-nonce",
  "firmware": "0.1.0",
  "capabilities": ["touch", "display", "microphone", "speaker"]
}
```

The server consumes the eight-digit code and returns the token once:

```json
{
  "protocol_version": 1,
  "device_id": "uuid",
  "token": "opaque-256-bit-token",
  "hub_host": "nova-hub.local",
  "hub_port": 443,
  "server_time": "2026-08-08T14:00:00Z"
}
```

Pairing codes are single-use, expire after ten minutes, and are rate-limited. The server stores only a keyed digest of the code and token.

## Session handshake

The first WSS client message must be `device.hello`:

```json
{
  "protocol_version": 1,
  "type": "device.hello",
  "timestamp": "2026-08-08T14:00:00Z",
  "payload": {
    "device_id": "uuid",
    "token": "opaque-256-bit-token",
    "installation_nonce": "base64url-nonce",
    "firmware": "0.1.0",
    "capabilities": ["touch", "display", "microphone", "speaker"]
  }
}
```

The server responds with `session.ready`. Authentication failures close the socket with policy-violation code `1008`.

## Health snapshots

The server sends one `health.snapshot` every five seconds:

```json
{
  "protocol_version": 1,
  "type": "health.snapshot",
  "timestamp": "2026-08-08T14:00:05Z",
  "payload": {
    "dependency_status": "healthy",
    "server_version": "0.1.0",
    "home_time": "2026-08-08T19:30:05+05:30",
    "uptime_seconds": 12345.6,
    "cpu_percent": 14.2,
    "memory_used_bytes": 4294967296,
    "memory_total_bytes": 16777216000,
    "disk_used_bytes": 107374182400,
    "disk_total_bytes": 500107862016,
    "network_interface": "enp3s0",
    "network_rx_bytes_per_second": 1200.0,
    "network_tx_bytes_per_second": 512.0,
    "network_rx_bytes_total": 123456789,
    "network_tx_bytes_total": 987654321,
    "collection_errors": [],
    "service_status": {
      "hub_api": "healthy",
      "metrics": "healthy"
    }
  }
}
```

`GET /v1/telemetry` returns the same one-shot response as `/v1/health` for simple diagnostics clients. The persistent WebSocket stream uses the same payload shape.

`dependency_status` is `healthy` when the hub and selected metric collectors work, and `degraded` when the session works but one or more metrics are unavailable. The terminal marks a snapshot stale after fifteen seconds.

When NVIDIA NVML is available, the payload also includes `gpu_state`, `gpu_count`, `gpu_name`, `gpu_utilization_percent`, `gpu_temperature_c`, `gpu_vram_used_bytes`, `gpu_vram_total_bytes`, and a `gpus` array containing the same fields for every visible GPU. Unavailable GPU metrics are returned as `null` and the state is `unavailable`; the API never reports fabricated zeroes.

`home_time` is server-synchronized wall-clock time in the configured home timezone. Envelope timestamps remain UTC for protocol tracing. B reports only `hub_api` and `metrics` service status; Home Assistant and MQTT are added in later phases.

## Compact telemetry stream

`GET /v1/telemetry/stream` is the low-overhead transport used by the board. It
returns a continuous `application/x-nova-telemetry` body with one fixed-width,
little-endian item every five seconds. The item is 64 bytes, so the ESP32 can
decode it with a bounded buffer and resynchronize on the `NVT1` magic after a
partial read or transport framing byte.

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `magic` | ASCII `NVT1` |
| 4 | 1 | `version` | Stream version, currently `1` |
| 5 | 1 | `kind` | `1` server health, `2` ESP health |
| 6 | 2 | `frame_size` | Always `64` |
| 8 | 4 | `sequence` | Monotonic item sequence for one stream |
| 12 | 8 | `timestamp_seconds` | Server wall-clock timestamp when known |
| 20 | 2 | `cpu_tenths` | CPU percentage multiplied by ten, signed |
| 22 | 2 | `gpu_utilization_tenths` | GPU percentage multiplied by ten, signed |
| 24 | 2 | `gpu_temperature_tenths` | GPU temperature in °C multiplied by ten, signed |
| 26 | 2 | `flags` | Availability and degraded-state bits |
| 28 | 8 | `metric_a` | Server: GPU VRAM used; ESP: journal bytes available |
| 36 | 8 | `metric_b` | Server: GPU VRAM total; ESP: journal quota |
| 44 | 4 | `uptime_seconds` | Uptime of the item producer |
| 48 | 4 | `error_count` | Number of collection errors in the snapshot |
| 52 | 8 | `reserved` | Zero-filled for forward compatibility |
| 60 | 4 | `crc32` | IEEE CRC-32 over offsets `0..59` |

The server item carries the current CPU/GPU state, including GPU utilisation,
temperature, and VRAM. Bit 0 means a GPU is available and bit 1 means the
server snapshot is degraded. The ESP health item carries board CPU and the
bounded journal accounting; bit 2 means the TF card is mounted.

The board persists both item kinds as fixed records at `/nova/telemetry.jrn`
inside the SD_MMC `/sdcard` mount. A dual-slot index at
`/nova/telemetry.idx` makes the write cursor and record count recoverable after
an interrupted write. The journal has a logical ceiling of 2 GiB and grows only
as records arrive; the remainder of the card is not preallocated or formatted
by the firmware. Once full, it wraps as a bounded circular journal.

## Client messages

- `device.ping`: optional liveness message; the server answers `server.pong`.
- Unknown messages receive `protocol.error` and do not terminate a compatible session.

## Administrative operations

Pairing-code creation, device listing, and revocation are CLI-only in this slice. A paired terminal cannot enumerate devices, create codes, access Home Assistant, execute commands, or read metric history.

The optional HTTPS diagnostics endpoint uses `Authorization: Bearer <token>` together with `X-Nova-Device-Id: <device-id>`. The WSS handshake carries both values inside `device.hello`.
