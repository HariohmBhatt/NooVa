# NOVA Hub Protocol

Version 1 of the terminal-to-hub protocol is JSON over HTTPS and WSS.

## Transport

- HTTPS is used for automatic registration and diagnostics.
- WSS is used for the persistent terminal metrics session.
- The terminal trusts the deployment's Caddy root CA. It must never use an insecure TLS fallback.
- The canonical server identity is `nova-hub.local` on TCP port `443`. mDNS confirms
  the service is available, while HTTPS and WSS use the canonical hostname for TLS.

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

## Automatic registration

The terminal sends `POST /v1/register` after mDNS discovery, without a user-entered
code or credential:

```json
{
  "protocol_version": 1,
  "installation_nonce": "base64url-nonce",
  "firmware": "0.1.0",
  "capabilities": ["touch", "display", "microphone", "speaker"]
}
```

The server creates or refreshes a device audit record for the installation nonce:

```json
{
  "protocol_version": 1,
  "device_id": "uuid",
  "hub_host": "nova-hub.local",
  "hub_port": 443,
  "server_time": "2026-08-08T14:00:00Z"
}
```

Repeated registration for the same installation nonce returns the same device ID.
The device ID supports audit records only; it is not an authorization credential.

## Session handshake

The first WSS client message must be `device.hello`:

```json
{
  "protocol_version": 1,
  "type": "device.hello",
  "timestamp": "2026-08-08T14:00:00Z",
  "payload": {
    "device_id": "uuid",
    "installation_nonce": "base64url-nonce",
    "firmware": "0.1.0",
    "capabilities": ["touch", "display", "microphone", "speaker"]
  }
}
```

The server responds with `session.ready`. The WebSocket has no device
authentication layer.

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

`dependency_status` is `healthy` when the hub and selected metric collector work, and `degraded` when the session works but one or more metrics are unavailable. The terminal marks a snapshot stale after fifteen seconds.

`home_time` is server-synchronized wall-clock time in the configured home timezone. Envelope timestamps remain UTC for protocol tracing. B reports only `hub_api` and `metrics` service status; Home Assistant and MQTT are added in later phases.

## Client messages

- `device.ping`: optional liveness message; the server answers `server.pong`.
- Unknown messages receive `protocol.error` and do not terminate a compatible session.

## Administrative operations

Device listing is CLI-only in this slice. The HTTPS diagnostics endpoint and
WSS metrics stream are accessible without device credentials. A terminal still
cannot access Home Assistant, execute commands, or read metric history.
