#!/usr/bin/env bash
# Supply the actual source observer; WAST owns all functional assertions.
set -euo pipefail
REPO_ROOT="$1"
VFS_ROOT="$2"
TEST_OUTPUT="$3"
WASM_RUNNER="$4"
mkdir -p "$TEST_OUTPUT"
TEST_RUN="$(mktemp -d "$TEST_OUTPUT/run-XXXXXX")"
cat > "$TEST_RUN/commands.sh" <<'COMMANDS'
/tmp/dwarf-verifier.wasm > /tmp/dwarf-verifier.bin
printf '%s\n' "$?" > /tmp/dwarf-verifier-status.txt
/bin/wast --verbose /tmp/dwarf-verifier.wast
exit "$?"
COMMANDS
cd "$REPO_ROOT"
exec "$WASM_RUNNER" --vfs-root "$VFS_ROOT" --timeout-ms 60000 \
  --result-file "$TEST_RUN/results.json" \
  --stage-file /tmp/dwarf-verifier.wasm 755 "$TEST_OUTPUT/probe.wasm" \
  --stage-file /tmp/dwarf-verifier.wast 644 "$REPO_ROOT/src/vfs/root/test/system/clang/dwarf-verifier/dwarf-verifier.wast" \
  "$VFS_ROOT/usr/bin/bash" --norc < "$TEST_RUN/commands.sh"
