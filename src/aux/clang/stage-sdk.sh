#!/usr/bin/env bash
# Stage upstream-owned portable/Wasm headers and the existing guest CRT.
set -euo pipefail
CLANG_SDK_CMAKE="$1"
CLANG_SDK_NATIVE="$(realpath -- "$2")"
CLANG_SDK_CRT="$(realpath -- "$3")"
CLANG_SDK_TEMPLATE="$(realpath -- "$4")"
CLANG_SDK_OUTPUT="$(realpath -m -- "$5")"
CLANG_SDK_LICENSE="$(realpath -- "$6")"
CLANG_SDK_REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
case "$CLANG_SDK_OUTPUT" in
  "$CLANG_SDK_REPO"/build/*/sdk) ;;
  *) printf 'error: Clang SDK staging must be under build/ and named sdk\n' >&2; exit 1 ;;
esac
CLANG_SDK_MAJOR="$(sed -n 's/^#define CLANG_VERSION_MAJOR \([0-9][0-9]*\)$/\1/p' \
  "$CLANG_SDK_NATIVE/tools/clang/include/clang/Basic/Version.inc")"
[[ "$CLANG_SDK_MAJOR" =~ ^[0-9]+$ ]] || { printf 'error: missing Clang resource version\n' >&2; exit 1; }
mkdir -p "$(dirname -- "$CLANG_SDK_OUTPUT")"
CLANG_SDK_TEMP="$(mktemp -d "${CLANG_SDK_OUTPUT}-stage-XXXXXX")"
trap 'rm -rf -- "$CLANG_SDK_TEMP"' EXIT
for CLANG_SDK_COMPONENT in core-resource-headers webassembly-resource-headers; do
  "$CLANG_SDK_CMAKE" --install "$CLANG_SDK_NATIVE" \
    --prefix "$CLANG_SDK_TEMP/usr" --component "$CLANG_SDK_COMPONENT"
done
CLANG_SDK_RESOURCE="/usr/lib/clang/$CLANG_SDK_MAJOR"
test -f "$CLANG_SDK_TEMP$CLANG_SDK_RESOURCE/include/stddef.h"
test -f "$CLANG_SDK_TEMP$CLANG_SDK_RESOURCE/include/stdarg.h"
test -f "$CLANG_SDK_TEMP$CLANG_SDK_RESOURCE/include/wasm_simd128.h"
mkdir -p "$CLANG_SDK_TEMP/usr/bin" "$CLANG_SDK_TEMP/usr/lib/waste/cc"
sed "s|@RESOURCE_DIR@|$CLANG_SDK_RESOURCE|g" "$CLANG_SDK_TEMPLATE" > "$CLANG_SDK_TEMP/usr/bin/clang.cfg"
chmod 644 "$CLANG_SDK_TEMP/usr/bin/clang.cfg"
install -m 644 "$CLANG_SDK_CRT" "$CLANG_SDK_TEMP/usr/lib/waste/cc/crt1.o"
mkdir -p "$CLANG_SDK_TEMP/usr/share/licenses/clang"
install -m 644 "$CLANG_SDK_LICENSE" "$CLANG_SDK_TEMP/usr/share/licenses/clang/LICENSE.TXT"
rm -rf -- "$CLANG_SDK_OUTPUT"
mv -- "$CLANG_SDK_TEMP" "$CLANG_SDK_OUTPUT"
