# NOVA Sentinel backend

The backend collects read-only Linux observations every five seconds, probes up
to four configured HTTP services concurrently, applies the health policy, and
serves one immutable schema-v1 snapshot. Device requests read the cache; they do
not trigger collection. Caddy is the only published service and supplies TLS.

## Run

1. Copy `.env.example` to `.env` and replace the token, LAN address, and host.
2. Configure optional service checks as compact JSON in `NOVA_SERVICES_JSON`.
3. Run `docker compose up --build -d` from this directory.
4. Install Caddy's local root CA from the `caddy_data` volume into the ESP32
   provisioning trust anchor. Keep the device token and CA out of source.

The status request requires `Authorization: Bearer …`, `X-Nova-Schema: 1`, and
`Accept: application/json`. The API never mounts the Docker socket and exposes
no command or container-management endpoint.

## Development checks

Create a virtual environment, install `.[test]`, then run:

```sh
pytest
ruff check .
mypy app
docker compose config --quiet
```
