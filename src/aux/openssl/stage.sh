#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
OUTPUT="$1"
case "$OUTPUT" in "$REPO_ROOT"/build/*) ;; *) echo 'OpenSSL staging must stay under build/' >&2; exit 1 ;; esac
mkdir -p "$OUTPUT/source"
cp -a "$REPO_ROOT/submodules/openssl/." "$OUTPUT/source/"
cp "$SCRIPT_DIR/waste.conf" "$OUTPUT/source/Configurations/99-waste.conf"
patch --batch --forward --fuzz=0 -d "$OUTPUT/source" -p1 -i "$SCRIPT_DIR/waste.patch"
