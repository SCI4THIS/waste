# Orchestration only; link.wast owns all runtime expectations.
/tmp/wasm-ld --no-entry --export=_start --export=answer --allow-undefined /tmp/link-entry.o /tmp/link-answer.a -o /tmp/linked.wasm
printf '%s\n' "$?" > /tmp/lld-link-status.txt
/usr/bin/chmod +x /tmp/linked.wasm
printf '%s\n' "$?" > /tmp/lld-chmod-status.txt
/tmp/linked.wasm
printf '%s\n' "$?" > /tmp/lld-run-status.txt
/bin/wast --verbose /tmp/lld-link.wast
