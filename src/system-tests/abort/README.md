# Guest abort and process survival

`make -C src/system-tests test-abort` compiles `probe.c` against the mounted
guest SDK and shared libc, stages it with `abort.wast`, and runs both from
the installed Bash. Results are saved under `build/system-tests/abort/run-*/`.
`WASM_RUNNER=build/cli-rt/wasm-sanitize` selects the sanitizer frontend.

The C fixture supplies scenarios and captures raw observations. WAST owns
all assertions: default, ignored, blocked, caught/returning, recursively
caught SIGABRT and legacy `env.abort` calls must produce waitpid status 6,
with the expected handler byte and a surviving parent. The caught handler
reblocks and ignores SIGABRT before returning, to check that abort still
terminates. Compiled function
pointers use the actual shared table. Abort never invokes host `abort()`.

After building the fixture and offline page, run the same WAST assertions
through the production browser worker without launching a GUI:

```sh
make -C src/system-tests abort-fixture
make -C src/html-rt bash-html
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/abort/browser.json \
  > build/engine/logs/clang-browser-abort-results.json
```

For a manual check, run these commands in the rebuilt page. Select
`build/system-tests/abort/probe.wasm` when the upload picker opens:

```sh
upload /tmp/probe.wasm
/usr/bin/chmod 755 /tmp/probe.wasm
/tmp/probe.wasm > /tmp/abort-observations.txt
/bin/wast --verbose /root/test/system/abort/abort.wast > /tmp/abort-results.txt
echo "$?"
download /tmp/abort-results.txt
```

Expect eight passing assertions and exit status zero. This fixture exercises
forks as a normal guest executable; it does not fork inside a WAST command
handler, which has a different execution boundary. Signal handlers run
synchronously and cannot block/yield. Asyncify is not used.
