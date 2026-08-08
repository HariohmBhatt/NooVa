#!/usr/bin/env sh
set -eu

: "${RESTIC_REPOSITORY:?RESTIC_REPOSITORY is required}"
: "${RESTIC_PASSWORD_FILE:?RESTIC_PASSWORD_FILE is required}"
: "${AWS_ACCESS_KEY_ID:?AWS_ACCESS_KEY_ID is required}"
: "${AWS_SECRET_ACCESS_KEY:?AWS_SECRET_ACCESS_KEY is required}"

tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
python3 -c 'import sqlite3, sys; source=sqlite3.connect(sys.argv[1]); target=sqlite3.connect(sys.argv[2]); source.backup(target); target.close(); source.close()' \
  /var/lib/docker/volumes/backend_hub_data/_data/nova.db \
  "$tmp_dir/nova.db"

restic backup \
  "$tmp_dir/nova.db" \
  /var/lib/docker/volumes/backend_caddy_data/_data \
  --tag nova-hub
restic forget --tag nova-hub --keep-daily 30 --keep-monthly 12 --prune
