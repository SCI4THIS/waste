# Guest binary interpreter command

`/bin/wasm FILE [ARG...]` reads a Wasm binary, supplies FILE as argv[0], and
forwards the remaining arguments. An explicit interpreter requires readable
input; direct execution continues to require executable permissions. Text input
and a missing filename are errors.

`make -C src/system-tests test-binary-handler` stages a non-executable copy of
the installed printf command and runs all functional assertions in results.wast.
Use WASM_RUNNER to select wasm-sanitize with ASan/UBSan enabled. The browser.json
scenario runs the same fixture through the packaged production worker.
