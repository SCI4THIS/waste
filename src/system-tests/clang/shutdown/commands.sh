#!/bin/bash
SHUTDOWN_STATUS=0
for SHUTDOWN_BINARY in /tmp/shutdown.wasm /tmp/shutdown-legacy.wasm; do
  "${SHUTDOWN_BINARY}" return > /tmp/shutdown-return.bin
  printf '%s\n' "$?" > /tmp/shutdown-return-status.txt
  "${SHUTDOWN_BINARY}" exit > /tmp/shutdown-exit.bin
  printf '%s\n' "$?" > /tmp/shutdown-exit-status.txt
  "${SHUTDOWN_BINARY}" immediate > /tmp/shutdown-immediate.bin
  printf '%s\n' "$?" > /tmp/shutdown-immediate-status.txt
  "${SHUTDOWN_BINARY}" abort > /tmp/shutdown-abort.bin
  printf '%s\n' "$?" > /tmp/shutdown-abort-status.txt
  "${SHUTDOWN_BINARY}" exec > /tmp/shutdown-exec.bin
  printf '%s\n' "$?" > /tmp/shutdown-exec-status.txt
  "${SHUTDOWN_BINARY}" exec-fail > /tmp/shutdown-exec-fail.bin
  printf '%s\n' "$?" > /tmp/shutdown-exec-fail-status.txt
  "${SHUTDOWN_BINARY}" fork > /tmp/shutdown-fork.bin
  printf '%s\n' "$?" > /tmp/shutdown-fork-status.txt
  "${SHUTDOWN_BINARY}" cache > /tmp/shutdown-cache.bin
  printf '%s\n' "$?" > /tmp/shutdown-cache-status.txt
  "${SHUTDOWN_BINARY}" groups > /tmp/shutdown-groups.bin
  printf '%s\n' "$?" > /tmp/shutdown-groups-status.txt
  /bin/wast --verbose /tmp/shutdown.wast || SHUTDOWN_STATUS=1
done
test "${SHUTDOWN_STATUS}" -eq 0
