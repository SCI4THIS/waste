#!/usr/bin/env bash
# Accept a pinned Git object or the matching exported corresponding source.
set -euo pipefail
UPSTREAM="$1"
REVISION="$2"
DESTINATION="$3"
mkdir -p "$DESTINATION"
if [[ -f "$UPSTREAM/.waste-source-revision" ]]; then
  [[ "$(cat "$UPSTREAM/.waste-source-revision")" == "$REVISION" ]] || {
    echo 'error: M4 source snapshot revision mismatch' >&2; exit 1;
  }
  tar -C "$UPSTREAM" --exclude=.git -cf - . | tar -C "$DESTINATION" -xf -
else
  git -C "$UPSTREAM" cat-file -e "$REVISION^{commit}" || {
    echo 'error: initialize full M4 and Coreutils/Gnulib histories first' >&2; exit 1;
  }
  git -C "$UPSTREAM" archive "$REVISION" | tar -C "$DESTINATION" -xf -
fi
printf '%s\n' "$REVISION" > "$DESTINATION/.waste-source-revision"
