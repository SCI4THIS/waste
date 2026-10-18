# Positive object/archive linking and explicit rejection of unsupported bitcode.
. /tmp/lld-link-positive.sh
LINK_CHECK=$?
/tmp/wasm-ld --no-entry --export=answer /tmp/link-answer.bc -o /tmp/bitcode-output.wasm 2> /tmp/lld-bitcode-error.txt
printf '%s\n' "$?" > /tmp/lld-bitcode-status.txt
/tmp/wasm-ld --no-entry --export=_start --allow-undefined /tmp/link-entry.o /tmp/link-answer-bc.a -o /tmp/bitcode-archive-output.wasm 2> /tmp/lld-bitcode-archive-error.txt
printf '%s\n' "$?" > /tmp/lld-bitcode-archive-status.txt
/bin/wast --verbose /tmp/lld-no-lto.wast
NO_LTO_CHECK=$?
return "$((LINK_CHECK | NO_LTO_CHECK))"
