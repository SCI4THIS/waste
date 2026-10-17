# Orchestration only; sdk.wast owns every runtime expectation.
/tmp/wasm-ld --no-entry --import-memory --import-table --export=_start --export=main --export=__heap_base --export=__data_end --export=__stack_pointer --experimental-pic --unresolved-symbols=import-dynamic /tmp/sdk-crt.o /tmp/sdk.o /lib/libc.so.wasm -o /tmp/sdk.wasm 2> /tmp/sdk-link-error.txt
printf '%s\n' "$?" > /tmp/sdk-link-status.txt
/usr/bin/chmod +x /tmp/sdk.wasm
printf '%s\n' "$?" > /tmp/sdk-chmod-status.txt
/tmp/sdk.wasm > /tmp/sdk-output.txt 2>&1
printf '%s\n' "$?" > /tmp/sdk-run-status.txt
/tmp/wasm-ld --no-entry --import-memory --export=test /tmp/bad-signature.o /lib/libc.so.wasm -o /tmp/bad-signature.wasm 2> /tmp/bad-signature-error.txt
printf '%s\n' "$?" > /tmp/bad-signature-status.txt
/bin/wast --verbose /tmp/lld-sdk.wast
