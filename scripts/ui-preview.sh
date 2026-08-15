#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "$0")/.." && pwd)"

exec python3 -m http.server 4173 \
  --bind 127.0.0.1 \
  --directory "$repository_root/tools/ui-preview"
