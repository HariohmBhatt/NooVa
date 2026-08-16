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

## Client messages

- `device.ping`: optional liveness message; the server answers `server.pong`.
- Unknown messages receive `protocol.error` and do not terminate a compatible session.

## Administrative operations

Pairing-code creation, device listing, and revocation are CLI-only in this slice. A paired terminal cannot enumerate devices, create codes, access Home Assistant, execute commands, or read metric history.

The optional HTTPS diagnostics endpoint uses `Authorization: Bearer <token>` together with `X-Nova-Device-Id: <device-id>`. The WSS handshake carries both values inside `device.hello`.
