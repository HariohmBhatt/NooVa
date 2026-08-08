#!/usr/bin/env sh
set -eu

: "${NOVA_OTA_HOST:?Set NOVA_OTA_HOST to the device IP address}"
: "${NOVA_OTA_PASSWORD:?Set NOVA_OTA_PASSWORD to the SSH password}"

pio run -e nova-ota --target upload
