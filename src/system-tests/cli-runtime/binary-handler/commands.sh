/bin/wasm /tmp/bin-handler-printf '%s|%s\n' alpha beta > /tmp/bin-handler-output.txt
printf '%s\n' "$?" > /tmp/bin-handler-status.txt
/bin/wasm /tmp/bin-handler-text.wat > /tmp/bin-handler-format.txt 2>&1
printf '%s\n' "$?" > /tmp/bin-handler-format-status.txt
/bin/wasm > /tmp/bin-handler-usage.txt 2>&1
printf '%s\n' "$?" > /tmp/bin-handler-usage-status.txt
/bin/wast --verbose /tmp/bin-handler-results.wast
BINARY_HANDLER_STATUS=$?
printf '__BINARY_HANDLER_%s__\n' "$BINARY_HANDLER_STATUS"
exit "$BINARY_HANDLER_STATUS"
