#!/bin/bash
WASTE_STARTUP=constructor-env /tmp/startup.wasm alpha beta < /tmp/startup-input.txt > /tmp/startup-out.txt 2> /tmp/startup-err.txt
printf '%s\n' "$?" > /tmp/startup-status.txt
/bin/wast --verbose /tmp/startup.wast
STARTUP_FIRST_STATUS=$?
WASTE_STARTUP=constructor-env /tmp/startup-two.wasm alpha beta < /tmp/startup-input.txt > /tmp/startup-out.txt 2> /tmp/startup-err.txt
printf '%s\n' "$?" > /tmp/startup-status.txt
/bin/wast --verbose /tmp/startup.wast
STARTUP_SECOND_STATUS=$?
test "$STARTUP_FIRST_STATUS" -eq 0 && test "$STARTUP_SECOND_STATUS" -eq 0
