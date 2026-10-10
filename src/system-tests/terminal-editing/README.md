# Bash line-editing display

`readline.json` types `wst --verbose`, sends Ctrl-A, Ctrl-F, then `a`, and
checks that the visible text is already `wast --verbose` before Ctrl-E. It
also checks cursor positions, forward deletion, multiple inserted characters,
and Ctrl-K. Each key group follows a real input wait. The boundary harness
feeds Bash's output into the production terminal model and checks screen cells.

Build/install libc, publish the package tests, and regenerate the page:

```sh
make -C src/aux install-libc
make -C src/html-rt vfs-tests-install bash-html
```

Run against browser exports and the actual packaged worker without a GUI:

```sh
node tests/guest-session-browser.cjs build/html-rt/waste-wast.wasm src/vfs \
  src/system-tests/terminal-editing/readline.json \
  > build/engine/readline-browser-results.json
node tests/guest-session-worker.cjs build/html-rt/bash.html \
  src/system-tests/terminal-editing/readline.json \
  > build/engine/readline-worker-results.json
```

The libc capability assertions are in `src/aux/libc/tests/termcap.wast` and
run with `make -C src/aux test-libc`. They can also run in the rebuilt page
with `/bin/wast --verbose /root/test/aux/libc/termcap.wast`.

For a manual check, reopen `build/html-rt/bash.html`, type `wst --verbose`,
press Ctrl-A, Ctrl-F, and `a`. Expect `wast --verbose` immediately, with the
cursor after `a`; Ctrl-E should move the cursor to the end without repairing
or changing the text. Clear the unsubmitted line with Ctrl-A followed by
Ctrl-K.
