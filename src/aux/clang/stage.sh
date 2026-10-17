#!/usr/bin/env bash
# Stage the LLVM components needed by Clang, LLD and the guest C++ runtime.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
CLANG_BUILD="$(realpath -m -- "$1")"
case "$CLANG_BUILD" in
  "$REPO_ROOT/build/"*) ;;
  *) printf 'error: Clang outputs must stay under build/\n' >&2; exit 1 ;;
esac
mkdir -p "$CLANG_BUILD"
STAGE="$(mktemp -d "$CLANG_BUILD/source-stage-XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
# LLVM's configure step also reads libc's common-utility headers. This does not
# enable or build LLVM libc; the guest continues to use WASTE's shared libc.
for component in llvm clang lld libc libcxx libcxxabi libunwind runtimes cmake third-party; do
  cp -a --reflink=auto "$REPO_ROOT/submodules/llvm-project/$component" "$STAGE/"
done
cp "$REPO_ROOT/submodules/llvm-project/LICENSE.TXT" "$STAGE/LICENSE.TXT"
chmod -R u+w "$STAGE"
if [[ -f "$SCRIPT_DIR/clang-waste.patch" ]]; then
  patch --batch --forward --fuzz=0 -d "$STAGE" -p1 < "$SCRIPT_DIR/clang-waste.patch"
  # Preserve dependency timestamps for identical patched files. Changing one
  # adaptation must not rebuild all users of unrelated, unchanged headers.
  while IFS= read -r changed_path; do
    if [[ -f "$CLANG_BUILD/source/$changed_path" ]] &&
       cmp -s -- "$CLANG_BUILD/source/$changed_path" "$STAGE/$changed_path"; then
      touch -r "$CLANG_BUILD/source/$changed_path" "$STAGE/$changed_path"
    fi
  done < <(sed -n 's|^+++ b/||p' "$SCRIPT_DIR/clang-waste.patch")
fi
rm -rf -- "$CLANG_BUILD/source"
mv -- "$STAGE" "$CLANG_BUILD/source"
