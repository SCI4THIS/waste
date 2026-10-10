# Native application, WAST and startup checks

This directory contains the private startup/ownership checks for the native
CLI migration. The same C probe runs against native and browser engine builds.
The `.wat` executables exercise constructor/runtime initialization, imported
resource aliases and failed-start cleanup. The probe also loads the installed
Bash, echo and production `libc.so.wasm`, verifies argument/environment/output
and exit status, then forks and replaces the parent image with `exec`.
It also covers explicit input formats, readable interpreter input, alternate
entries, memoryless modules, and standalone libc context startup/rollback.
Context startup attaches terminal descriptors while preserving mounted paths;
failed setup restores the original empty descriptors and library namespace.

Run from the repository root:

```sh
make -C src/cli-rt direct-start-check
make -C src/html-rt direct-start-probe
node src/system-tests/cli-runtime/browser-check.mjs
```

The native gate uses warnings-as-errors, ASan and UBSan. To include leak
checking, run this in an environment where LeakSanitizer can inspect the process:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/cli-rt/direct-start-sanitize
```

Build and run the public application frontends:

```sh
make -C src/cli-rt applications-native
build/cli-rt/wasm src/vfs/usr/bin/echo 'two words'
build/cli-rt/wasm src/vfs/usr/bin/bash -c 'echo "$HOME"; exit 7'
build/cli-rt/wat --entry probe src/system-tests/cli-runtime/resources.wat
make -C src/cli-rt application-check application-check-sanitize
```

`entrypoints/check.py` performs host CLI/filesystem checks against authored WAT
fixtures and installed production binaries. It verifies status/stdout/stderr,
guest argv/environment, WAT/Wasm parity, root discovery/mapping, confined
external staging, failure diagnostics, opt-in reports and timeout. The fixture
encoder is `wasm-as`; assertions about the host boundary remain in this private
driver. Package assertion logic remains in package WAST tests.

Frontend gates pass 47 checks per native profile; the shared C startup probe
passes 366 checks per native/browser build, including stream direction,
queue/EOF, alias/close, clone ownership and caught input-wait signals.
`read-signals.wat` exercises compiled table-slot handlers, READ restart, masks,
cross-library pselect callbacks, action-buffer aliasing and callback failure
cleanup. The probe also verifies installed Bash resize handling, default signal
termination, masked/ignored signals, notifications and disposition reset at exec.
Generated Wasm fixtures, alternate
roots, JSON and temporary test inputs live under
`build/system-tests/cli-runtime/entrypoints`. The ordinary gate does not need
Node or a browser; the shared browser probe uses a Node Wasm host without a GUI.

Run standalone package and system WAST files:

```sh
build/cli-rt/wast --verbose src/vfs/root/test/aux/libc/allocator.wast
build/cli-rt/wast --suite --group=aux/libc --group=system/libc --jobs=2 --results=build/cli-rt/standalone-libc-results.json
make -C src/cli-rt wast-check wast-check-sanitize
```

`wast/check.py` verifies 38 host CLI/report/isolation boundaries against its
adjacent authored WAST fixtures and installed package tests. Guest assertions
stay in `.wast`; the Python checks inspect host status/streams, namespace
selection, isolation, bounded file input, deadlines, diagnostic compatibility
and batch reports. Evidence goes under `build/system-tests/cli-runtime/wast`.
Native batches run all 14 libc package/system files without Bash; the same
59 assertions also run through guest `/bin/wast` inside native/browser Bash.
The `wast/package-session.json` production-worker contract enables a one
millisecond pump quantum, verifying repeated assertion resume during CPU work.
Browser isolated batches still skip these library tests; their Bash execution
is the browser acceptance route:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html src/system-tests/cli-runtime/wast/package-session.json
```

Auto context supplies installed production libc and C-owned resources for
ordinary `libc`/`waste-runtime` imports. Script-owned providers or module
assertions using those names keep the entire file in language context.
`--context language` suppresses production providers and terminal descriptors;
`--context runtime` supplies them and rejects conflicting scripts. Terminal
READ tests need runtime context; finite SELECT waits also work in language
context. Nested host batches are unavailable in either standalone context.
`--json` sends guest writes to stderr, preserving stdout for assertion reports.
Every input file has a fresh store/kernel. Successful default execution is quiet;
`--verbose` prints dots, totals and the first failure. Empty scripts pass and
setup/truncation failures return nonzero while retaining assertion evidence.

Private compatibility harness examples:

```sh
build/cli-rt/private/guest-session --vfs-root src/vfs --exec /usr/bin/echo -- 'two words'
build/cli-rt/private/guest-session --vfs-root src/vfs --exec /usr/bin/bash -- --norc -c 'echo "$HOME"; exit 7'
build/cli-rt/private/guest-session --vfs-root src/vfs --exec /usr/bin/bash -- --norc -i
```

The harness still applies its existing deadlines, input adapter and JSON
reporting. Its direct-start mode reports zero bootstrap assertions/execs;
subsequent guest fork/exec transitions contribute their actual counts.
Public applications have no default deadline or harness report and inherit
actual host stdio types and directions. Run their private host-boundary gates:

```sh
make -C src/cli-rt terminal-check terminal-check-sanitize
```

`terminal/check.py` and its adjacent WAT fixture verify 39 native boundaries
(including a 31-second idle Bash) and 38 sanitizer boundaries. They inspect
binary pipes, regular-file streams, aliases after closing original descriptors,
read-only TTY capabilities, mixed TTY/pipe identity, guest redirection/pipelines,
Readline editing/EOF, initial dimensions, SIGWINCH ioctl state, Bash
LINES/COLUMNS updates and exact host
termios restoration after exit, failure, timeout and terminating host signals.
Only private PTYs and child processes are affected. No Node or GUI is needed;
results go under `build/system-tests/cli-runtime/terminal`.

Inherited regular files are streams without seek/full host metadata. Host
SIGINT/TERM/HUP/QUIT route to the guest foreground group. Default termination
returns 128 + signal; caught handlers and shell/child interruption retain the
shell when appropriate. Canonical erase, kill, echo and EOF also have PTY
coverage. Default stop/job-control scheduling is explicitly unsupported.
Caught READ handlers honor
SA_RESTART; SELECT/pselect return EINTR after dispatch. Compiled handlers use
process table slots, while table-free legacy probes retain function indices.
Blocking operations inside synchronous handlers are explicitly unsupported.
The PTY resize check synchronizes with actual `--trace-waits` records before
sending SIGWINCH, then verifies Bash's variables and continued input handling.

Verify resize and foreground interruption in the production offline worker:

```sh
node tests/guest-session-worker.cjs build/html-rt/bash.html src/system-tests/cli-runtime/terminal/resize.json
node tests/guest-session-worker.cjs build/html-rt/bash.html src/system-tests/cli-runtime/terminal/interruptions.json
```

The guest SDK's `signal.h` must declare SIGWINCH so Bash compiles its resize
handlers. After changing guest declarations, rebuild and explicitly install
Bash before generating the page:

```sh
make -C src/aux -j4 bash
make -C src/aux install-bash
make -C src/html-rt bash-html
```

Save stdout/stderr and compatibility-check evidence under
`build/engine/logs/cli-direct-start-*.log`, `cli-entrypoints-*.log` or
`cli-wast-*.log`, `cli-stdio-*.log`, `cli-read-signals-*.log` or `cli-closeout-*.log`.
Private binaries are generated in
`build/cli-rt` and `build/html-rt`; no generated input belongs in a submodule.
