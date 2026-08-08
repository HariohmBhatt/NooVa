# NOVA Security Boundary

## Implemented boundary

- The terminal uses CA-validated HTTPS and WSS. There is no `setInsecure()` path.
- The hub automatically registers a terminal from its installation nonce and records audit events.
- The hub does not require a pairing code, device token, or authenticated WebSocket session.
- Caddy is intended to bind only to the physical LAN address.
- The terminal uses the `nova-hub.local` certificate identity after mDNS discovery.
- Production firmware disables the SSH and Arduino OTA listeners at runtime.
- The production partition table marks NVS and SPIFFS encrypted.
- SQLite audit events are pruned after 30 days and Compose logs are size-rotated with 14 retained files.

## Provisioning gate

The normal Arduino PlatformIO flow does not enable flash encryption or encrypted NVS by itself. `nova-production` is therefore a release-shaped build profile, not proof of a production-secure device. Before deploying a production terminal, the external provisioning process must:

1. Build a secure-capable bootloader and application.
2. Provision flash encryption and encrypted NVS on sacrificial hardware.
3. Verify the generated trust-anchor header and persisted device identity.
4. Confirm USB recovery behavior with signed or otherwise approved artifacts.
5. Record the per-device eFuse and recovery state outside Git.

Secure Boot v2 and signed OTA remain a later milestone. This slice does not
authenticate terminals; any client with LAN access can read the hub metrics.

## Secrets that must not enter Git

- Caddy private PKI data.
- SQLite database and backups.
- Backblaze credentials.
- Firmware trust-anchor generated header.
- Flash-encryption and signing keys.
- Wi-Fi credentials.
