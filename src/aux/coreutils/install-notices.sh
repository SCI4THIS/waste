#!/usr/bin/env bash
# Called under the same publication lock as command installation.
set -euo pipefail
VFS_ROOT="$1"
COREUTILS_BUILD="$2"
install -D -m 644 "$COREUTILS_BUILD/source/COPYING" "$VFS_ROOT/usr/share/licenses/coreutils/COPYING"
install -D -m 644 "$COREUTILS_BUILD/coreutils-provenance.json" "$VFS_ROOT/usr/share/waste/coreutils-provenance.json"
# A prior release bundle describes prior binaries. A new explicit source-package
# target produces the mapping for this build; ordinary installs don't claim it.
rm -f -- "$VFS_ROOT/usr/share/waste/coreutils-source-package.json"
