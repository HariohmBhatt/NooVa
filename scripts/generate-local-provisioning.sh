#!/bin/sh
set -eu

# Generate the ignored firmware provisioning header without placing credentials
# on the command line or in tracked source. Expected use:
#   scripts/generate-local-provisioning.sh backend/.env /path/to/root.crt

if [ "$#" -ne 2 ]; then
  echo "usage: $0 ENV_FILE CA_CERTIFICATE" >&2
  exit 2
fi

env_file=$1
ca_file=$2
output_file=src/config/Provisioning.h
temporary_file="${output_file}.tmp"

test -r "$env_file"
test -r "$ca_file"

token=$(sed -n 's/^NOVA_DEVICE_TOKEN=//p' "$env_file" | tail -n 1)
address=$(sed -n 's/^NOVA_LAN_ADDRESS=//p' "$env_file" | tail -n 1)
tls_name=$(sed -n 's/^NOVA_PUBLIC_HOST=//p' "$env_file" | tail -n 1)
port=$(sed -n 's/^NOVA_LAN_PORT=//p' "$env_file" | tail -n 1)

case "$address" in
  ""|*[!A-Za-z0-9.:-]*) echo "NOVA_LAN_ADDRESS is missing or unsafe" >&2; exit 1 ;;
esac
case "$tls_name" in
  ""|*[!A-Za-z0-9.-]*) echo "NOVA_PUBLIC_HOST is missing or unsafe" >&2; exit 1 ;;
esac
case "$port" in
  ""|*[!0-9]*) echo "NOVA_LAN_PORT is missing or invalid" >&2; exit 1 ;;
esac
case "$token" in
  ""|*'"'*|*'\'*|*'
'*) echo "NOVA_DEVICE_TOKEN is missing or cannot be represented safely" >&2; exit 1 ;;
esac

umask 077
{
  echo '#pragma once'
  echo
  echo '#include <cstdint>'
  echo
  echo 'namespace nova::provisioning {'
  echo
  echo 'constexpr char kWifiSsid[] = "";  // Prefer archived NVS credentials.'
  echo 'constexpr char kWifiPassword[] = "";'
  printf 'constexpr char kStatusAddress[] = "%s";\n' "$address"
  printf 'constexpr char kStatusTlsName[] = "%s";\n' "$tls_name"
  printf 'constexpr uint16_t kStatusPort = %s;\n' "$port"
  echo 'constexpr char kStatusPath[] = "/v1/status";'
  printf 'constexpr char kDeviceToken[] = "%s";\n' "$token"
  printf 'constexpr char kTlsCaPem[] = R"nova_ca('
  sed 's/\r$//' "$ca_file"
  echo ')nova_ca";'
  echo
  echo '}  // namespace nova::provisioning'
} >"$temporary_file"

mv "$temporary_file" "$output_file"
echo "generated ignored $output_file"
