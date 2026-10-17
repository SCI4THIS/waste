# Guest inputs only; installed.wast owns all result assertions.
/usr/bin/clang --version > /tmp/clang-version.txt
printf 'int main( {\n' > /tmp/clang-invalid.c
/usr/bin/clang --target=wasm32-unknown-unknown -fsyntax-only \
  /tmp/clang-invalid.c > /tmp/clang-invalid-output.txt 2>&1
printf '%s\n' "$?" > /tmp/clang-invalid-status.txt
cat > /tmp/clang-valid.c <<'CLANG_INPUT'
#pragma clang __debug sleep
#include <stdio.h>
#include <string.h>
int main(void) {
    char text[32];
    memcpy(text, "guest-clang", sizeof "guest-clang");
    printf("%s:%zu\n", text, strlen(text));
    printf("%s|%s|%s\n", __TIMESTAMP__, __DATE__, __TIME__);
    return 37;
}
CLANG_INPUT
SOURCE_DATE_EPOCH=946684800 /usr/bin/clang /tmp/clang-valid.c -o /tmp/clang-valid.wasm > /tmp/clang-compile-output.txt 2>&1
printf '%s\n' "$?" > /tmp/clang-compile-status.txt
/bin/wasm /tmp/clang-valid.wasm > /tmp/clang-run-output.txt 2>&1
printf '%s\n' "$?" > /tmp/clang-run-status.txt
printf 'parent-alive\n' > /tmp/clang-parent.txt
