# NOVA Hub Deployment

The hub is deployed on the home server with Docker Compose. The terminal only connects to the Caddy listener on the LAN address and never receives a privileged Home Assistant credential.

## Server setup

1. Copy `backend/.env.example` to `backend/.env`.
2. Set `NOVA_LAN_ADDRESS` to the server's physical LAN address.
3. Start the stack from `backend/`:

```sh
docker compose up -d --build
```

4. Install `infrastructure/avahi/nova-hub.service` at `/etc/avahi/services/nova-hub.service`, merge `infrastructure/avahi/avahi-daemon.conf.fragment` into `/etc/avahi/avahi-daemon.conf`, and restart Avahi.
5. Connect the terminal to Wi-Fi. It discovers the hub, registers its installation
   automatically, and starts the metrics stream.

## Trust anchor

Caddy stores its internal CA in the persistent `caddy_data` volume. Export only the public root certificate, then generate the firmware header:

```sh
docker compose cp caddy:/data/caddy/pki/authorities/local/root.crt /secure/nova/root.crt
python3 scripts/generate-hub-trust-anchor.py \
  /secure/nova/root.crt \
  firmware/network/HubTrustAnchor.generated.h
```

`HubTrustAnchor.generated.h` is intentionally ignored by Git. Never export or commit Caddy's private CA key.

## Backups

Use `infrastructure/backup/restic-backup.sh` with credentials supplied through the service environment. The repository target is the encrypted Backblaze B2 Restic repository named `nova-hub-backups`. Run it daily and before database migrations or CA changes. Keep 30 daily and 12 monthly snapshots.

Install `infrastructure/backup/nova-hub-backup.service` and
`infrastructure/backup/nova-hub-backup.timer` under `/etc/systemd/system/`,
create a mode-600 `/etc/nova-hub/backup.env`, then enable the timer:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now nova-hub-backup.timer
```

The backup includes the SQLite volume and Caddy PKI data. Device identities and
private CA material are never printed by the application.

## Device administration

List automatically registered terminals:

```sh
docker compose exec hub-api nova-hub devices
```
