#!/usr/bin/env bash
# Import the pinned Gnulib modules and generate Autotools inputs offline.
set -euo pipefail
SOURCE="$(realpath -- "$1")"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
case "$SOURCE" in "$REPO_ROOT/build/"*) ;; *) exit 1 ;; esac
cd "$SOURCE"
autopoint --force
mapfile -t MODULES < <(python3 - <<'PY_MODULES'
from pathlib import Path
import re
s = Path('bootstrap.conf').read_text()
modules = re.search("gnulib_modules='(.*?)'", s, re.S)[1].split()
# bootstrap is a maintainer-only external submodule, unnecessary here.
print('\n'.join(module for module in modules if module not in {'bootstrap', 'execute', 'spawn-pipe', 'wait-process'}))
PY_MODULES
)
GNULIB_TOOL_IMPL=py ./gnulib/gnulib-tool --import --with-tests --lib=libm4 \
  --source-base=lib --m4-base=m4 \
  --makefile-name=gnulib.mk --tests-base=tests --no-libtool \
  --macro-prefix=M4 --local-dir=gl-mod \
  --avoid=getopt-posix-tests --avoid=lock-tests --avoid=tls-tests "${MODULES[@]}"
printf 'GNU M4 source archive; see ChangeLog-2014 and .waste-source-revision.\n' > ChangeLog
AUTOPOINT=true autoreconf --force --install --verbose
