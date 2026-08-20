# NOVA status protocol v1

## Transport interface

The device performs `GET /v1/status` every five seconds over HTTPS. Polling is
chosen for 0.1 because request lifetime, retry behavior, diagnostics with
`curl`, and bounded memory are obvious. The client may reuse a keep-alive TLS
connection, but correctness cannot depend on reuse.

Required request headers:

```http
GET /v1/status HTTP/1.1
Host: nova-sentinel.local
Accept: application/json
Authorization: Bearer <device-token>
X-Nova-Schema: 1
```

The response uses `Content-Type: application/json`, `Cache-Control: no-store`,
and an exact `Content-Length`. Chunked responses are not part of v1. The device
rejects a missing/invalid length or a body larger than 4096 bytes before JSON
parsing. It uses fixed-capacity storage and reads no more than a bounded amount
per main-loop iteration.

## Success contract

```json
{
  "schema_version": 1,
  "sequence": 42,
  "generated_at_epoch_s": 1787227200,
  "overall": "warning",
  "summary": "System disk is 82% full",
  "reasons": [
    {
      "code": "disk_high",
      "severity": "warning",
      "message": "System disk is 82% full"
    }
  ],
  "metrics": {
    "cpu_percent_tenths": 241,
    "memory_percent_tenths": 610,
    "disk_percent_tenths": 821,
    "uptime_seconds": 48210
  },
  "services": [
    {"id": "hub", "name": "Hub", "state": "healthy"},
    {"id": "backup", "name": "Backup", "state": "warning"}
  ]
}
```

### Bounds and invariants

| Field | Contract |
| --- | --- |
| `schema_version` | integer, exactly `1` |
| `sequence` | unsigned 32-bit, increments per generated snapshot and may reset when the backend restarts; it is diagnostic, not a freshness clock |
| `generated_at_epoch_s` | unsigned 64-bit UTC server time; display/debug only |
| `overall` | `healthy`, `warning`, or `critical` |
| `summary` | UTF-8, 1–64 bytes, must describe the first reason or be `All monitored systems normal` |
| `reasons` | array of 0–3 objects, sorted by health-policy priority |
| reason `code` | lowercase ASCII identifier, 1–32 bytes |
| reason `severity` | `warning` or `critical` |
| reason `message` | UTF-8, 1–96 bytes |
| metric percentages | integer tenths in 0–1000, or JSON `null` when unavailable |
| `uptime_seconds` | unsigned 32-bit, or JSON `null` |
| `services` | array of 0–4 objects in configured display order |
| service `id` | lowercase ASCII identifier, 1–24 bytes, unique in snapshot |
| service `name` | UTF-8, 1–24 bytes |
| service `state` | `healthy`, `warning`, `critical`, or `unknown` |
| complete body | 4096 bytes maximum, including unknown fields |

When `overall` is healthy, `reasons` is empty. Warning/critical has at least one
reason and `summary` equals its first message. Unknown object fields may be
ignored only after the whole-body limit is enforced. Missing required fields,
wrong types, out-of-range numbers, too many entries, overlong strings, invalid
UTF-8, or violated cross-field invariants reject the entire body. A rejected
body does not update last-accepted time or overwrite the last-known snapshot.

`null` distinguishes unavailable metrics from a real zero. The device computes
freshness only from its local monotonic receipt time; server timestamps and
sequence numbers never make an old screen appear fresh.

## Error contract

Errors also fit within 512 bytes:

```json
{
  "schema_version": 1,
  "error": {
    "code": "unauthorized",
    "message": "Device credentials were rejected"
  }
}
```

| HTTP status | Meaning | Device behavior |
| --- | --- | --- |
| 401/403 | Token missing, invalid, or unauthorized | Immediate `monitor_error`; retry on normal interval without logging the token |
| 426 | `X-Nova-Schema` unsupported | Immediate `monitor_error` |
| 429 | Rate limited; optional integer `Retry-After` | Transient; cap retry delay at 60 seconds and let freshness advance |
| 500/503 | Unexpected backend or snapshot scheduler failure | Transient; let freshness advance |
| other 4xx | Endpoint/request contract is wrong | Immediate `monitor_error` |

DNS/connect/read timeouts and HTTP 5xx responses are transient transport
failures. Certificate/hostname validation failures and malformed, oversized,
wrong-content-type, or otherwise invalid success bodies are immediate monitor
errors. No error response can refresh snapshot freshness.

## Compatibility

Version 0.1 supports only schema 1. Additive object fields are allowed within
the total bound. Changing meanings, enum values, required fields, or bounds
requires a new schema. The server selects behavior using `X-Nova-Schema` and
must never silently send a different major schema.
