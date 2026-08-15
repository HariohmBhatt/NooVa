#!/usr/bin/env sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preview_port=${NOVA_PREVIEW_PORT:-4173}

cd "$repo_root/tools/ui-preview"
exec python3 -m http.server "$preview_port" --bind 127.0.0.1
