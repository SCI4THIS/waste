#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/c-tail-poc"
ITERATIONS="${1:-5000000}"

mkdir -p -- "$BUILD_DIR"
wasm-as "$REPO_ROOT/tests/c-tail-poc/tail-call.wat" \
  --enable-tail-call --enable-reference-types --enable-gc \
  -o "$BUILD_DIR/tail-call.wasm"
make -C "$REPO_ROOT/src/c-engine" BUILD_DIR="$BUILD_DIR" all
"$BUILD_DIR/waste-tail-poc" "$BUILD_DIR/tail-call.wasm" direct "$ITERATIONS"
"$BUILD_DIR/waste-tail-poc" "$BUILD_DIR/tail-call.wasm" reference "$ITERATIONS"

dd if="$BUILD_DIR/tail-call.wasm" of="$BUILD_DIR/truncated.wasm" \
  bs=1 count=7 status=none
if "$BUILD_DIR/waste-tail-poc" "$BUILD_DIR/truncated.wasm" direct 1 \
    >"$BUILD_DIR/truncated.log" 2>&1; then
  printf 'error: truncated module was accepted\n' >&2
  exit 1
fi
printf 'truncated_module=rejected\n'
