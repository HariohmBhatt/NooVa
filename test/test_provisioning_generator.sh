#!/bin/sh
set -eu

# Exercise a failure after the temporary header has received the token. The
# generator must remove that artifact and must not copy the secret to output.
fixture_dir=$(mktemp -d)
cleanup() {
  rm -rf "$fixture_dir"
}
trap cleanup EXIT HUP INT TERM

fixture_env="$fixture_dir/backend.env"
unreadable_ca="$fixture_dir/ca-directory"
fake_token='generator-test-secret-never-print'
mkdir "$unreadable_ca"
printf '%s\n' \
  "NOVA_DEVICE_TOKEN=$fake_token" \
  'NOVA_LAN_ADDRESS=127.0.0.1' \
  'NOVA_PUBLIC_HOST=nova-sentinel.local' \
  'NOVA_LAN_PORT=8443' >"$fixture_env"

test ! -e src/config/Provisioning.h.tmp
if output=$(scripts/generate-local-provisioning.sh "$fixture_env" \
    "$unreadable_ca" 2>&1); then
  echo 'expected provisioning generation to fail for a directory CA' >&2
  exit 1
fi

test ! -e src/config/Provisioning.h.tmp
case "$output" in
  *"$fake_token"*)
    echo 'provisioning generator exposed its bearer token' >&2
    exit 1
    ;;
esac

echo 'provisioning generator failure cleanup: PASS'
