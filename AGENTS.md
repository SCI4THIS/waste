# Repository Guidelines

## Project Structure & Module Organization

The production C implementation is split by responsibility. Platform-neutral
parser, encoder, validator, linker, runner, and executor code lives in
`src/engine/`. The native command-line runtime, mmap harness, syscall-backed C
library, and native build rules live in `src/cli-rt/`. The browser/Wasm API,
browser POSIX adapters, guest libc implementation, HTML generators, and browser
build rules live in `src/html-rt/`. Keep shared semantics in `src/engine/`; add
platform behavior only to the matching runtime directory. Compiled Bash input
is in `examples/bash.wat`.

Follow `docs/architecture.md`: C is the browser runtime, while the official
OCaml interpreter in `submodules/wasm-spec` is the differential oracle only
for Wasm/WAT/WAST language semantics. Do not add OCaml kernel or application
runtime capabilities. Its existing kernel is scheduled for removal in deferred
cleanup; see `docs/active-ocaml-language-oracle-plan.md`.
Read `docs/techniques.md` before extending the engine; it records the parser,
validation, execution, linking, browser, and verification practices established
during the port. Current staged work is tracked in the `docs/active-*.md`
plans.
Represent repository-owned OCaml changes in
`submodules/wasm-spec-i31-int32.patch`, never in submodule history. Dashboard
and packaging tools are in `src/html-rt/tools/`; guest libc sources are in
`src/html-rt/lib/`. Architecture notes are in `docs/`, and project probes
are in `tests/`, especially `tests/diy-posix-test/` and `tests/libc-test/`.
Treat all of `build/` as generated output. Shared generated engine sources and
logs go under `build/engine/`, native executables under `build/cli-rt/`, browser
artifacts under `build/html-rt/`, and OCaml intermediates under `build/ocaml/`.
Guest distribution snapshots are explicitly installed into `src/vfs` using
`src/html-rt/tools/vfs.py`; its `.inventory.json` is the mounted path/metadata
contract. Compile under `build/`, then install; HTML packaging must not compile
or discover guest binaries from the frontend source tree.
Authored guest public headers live in `src/vfs/usr/include`; compiler support
snapshots live in `src/vfs/usr/lib/waste/cc/include`. Keep engine/native headers
and libc `helper.h` private. After public-header edits, run the explicit
`guest-sdk-install` and `guest-sdk-check` targets; see `docs/guest-sdk.md` for
the audited SDK's provider/signature capability ledger and build profiles.
Test files in `src/vfs/tests` are installed distribution snapshots, not authored
sources. Refresh with `vfs-tests-install` and audit with `vfs-tests-check`;
the single `bash.html` page runs browser corpus diagnostics from these mounted
snapshots. See `docs/test-corpus.md`.
Authored portable executor regressions live in `tests/engine-regressions/*.wast`;
`docs/test-coverage.md` records their C assertion mappings and retained private
checks. The `i32-smoke`, `caller-instance` and `continuation` gates require their
installed regression snapshots to match authored inputs.

## POSIX Capability Policy

Use browser primitives when faithful and emulate practical OS semantics in the
C engine-owned kernel. Verify kernel/POSIX behavior across the native and
browser C runtimes; OCaml kernel parity is not required. Route unavailable
capabilities such as raw sockets
through an optional WebSocket POSIX broker. Keep POSIX state in the engine—not
the broker—and make the protocol versioned, asynchronous, capability-scoped,
and explicit about `errno`, cancellation, and readiness. Without it, return an
unsupported error. The browser page must remain a self-contained `file://`
document; broker use is optional.

The C engine must not use Asyncify. Blocking imports save explicit
engine-owned evaluator/process state, return `EXEC_YIELD` through ordinary C
returns, and later resume through the exported engine API. Do not add an
Asyncify transform, Asyncify runtime hooks, or documentation that describes
this explicit return path as stack unwinding/rewinding.

## Build, Test, and Development Commands

- `./start.sh`: open the dependency/build/test wizard.
- `./start.sh --check`: inspect dependencies and submodule state.
- `./start.sh --cli-compile`: build the native C WAST runner.
- `./start.sh --cli-test`: run the top-level core spec files with the native C
  runner.
- `./start.sh --html-bash`: build the self-contained C-engine Bash page only.
- `./start.sh --html-check`: run focused Node worker and terminal-model checks
  against the generated page without launching Chromium.
- `./start.sh --html-browser-full`: run the full offline Chromium/browser suite
  against `bash.html`.
- `./start.sh --compile`: build direct and CPS OCaml-to-Wasm artifacts.
- `./start.sh --build-libc`: build the guest libc module and generated test
  fixtures.
- `./start.sh --generate-bash-html`: generate the offline WASTE Bash page.
- `make -C submodules SWITCH_NAME=waste-wasm wasm`: temporarily apply the
  repository-owned OCaml patch, build sequential and CPS OCaml-to-Wasm
  artifacts, and restore the spec submodule even if the build fails.
- `make -C submodules SWITCH_NAME=waste-wasm native`: use the same patch
  transaction to build the native OCaml differential oracle.
- `make -C src/cli-rt BUILD_DIR=../../build/cli-rt wast-native`: build the
  native CLI runner.
- `make -C src/cli-rt corpus-native`: build the native batch companion and
  execute the installed manifest/tree without Node; write assertion-aware
  results to `build/cli-rt/corpus-results.json`.
- `build/cli-rt/waste-test --vfs-root=src/vfs --list`: enumerate the installed
  corpus, including unsupported entries with skip reasons. Use `--group`,
  `--jobs`, `--expected-failures` and `--results` for batch execution.
- `make -C src/cli-rt BUILD_DIR=../../build/cli-rt i32-smoke`: run the native
  warnings-as-errors ASan/UBSan executor smoke gate.
- `make -C src/cli-rt process-groups`: run the shared process/foreground-terminal
  session and native sanitizer/wait guards without Node or HTML.
- `make -C src/cli-rt terminal-descriptors`: run the shared terminal alias/close
  session and native sanitizer/minimum-input guard without Node or HTML.
- `make -C src/html-rt BUILD_DIR=../../build/html-rt wast-browser`: compile the
  same engine semantics for the browser.
- `build/cli-rt/waste-cli --parse-only FILE...`: rapidly parse selected WAST
  files without Node or a browser.
- `node tests/c-engine-browser-runtime.cjs`: exercise focused WAST payloads
  through the worker harness without generating an HTML dashboard.
- `node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html`: verify
  delayed input, command execution, a second prompt, and exit in C-engine Bash.
- `node tests/c-engine-frontend-packaging.cjs`: check flattened frontend paths,
  embedded package bytes, offline references, corpus metadata, and guest mtimes.
- `node tests/c-engine-offline-browser.cjs`: use headless Chromium to verify the
  actual `bash.html` `file://` page, DOM/worker boot, Bash input/output, and no
  external runtime requests. Override the executable with `WASTE_CHROMIUM`.
- Open **Diagnostics → Installed tests** in `build/html-rt/bash.html` to list,
  select, run or cancel the mounted corpus and download assertion JSON without
  Node. `node tests/c-engine-offline-browser.cjs --suite-full` is the focused
  automation gate for full production-browser batch execution.
- `node tests/diy-posix-test/posix-{kernel,control}-runtime.cjs [threaded]`:
  run legacy OCaml-runtime DIY POSIX probes.
- `node tests/libc-test/libc-runtime.cjs [threaded]`: run legacy OCaml-runtime
  guest libc probes pending coverage accounting and kernel retirement.
- `node tests/libc-test/allocator-native.cjs`: stress the built allocator natively.

Retired dashboard options (`--html-test`, `--c-engine-html`, and
`--generate-html`) report their replacements. Use `--cli-test` for native
tests, `--html-bash` to build the single shell/test page, and
`--html-browser-full` for full browser acceptance.
Before submitting shell or Python changes, run `bash -n start.sh`, Python
bytecode checks for changed `src/html-rt/tools/*.py` files, and
`git diff --check`.

## Coding Style & Naming Conventions

Match surrounding indentation. Use `snake_case` for functions, uppercase shell
constants, and kebab-case dashboard/log names. Quote shell expansions. Do not
edit generated Flex/Bison files, build artifacts, or upstream submodule history
directly. New C code must use bounded readers, structured errors, explicit
ownership, and integer handles across the JavaScript boundary. Keep hot
execution paths allocation-free and place optional counters behind
`WASTE_PROFILE`.

## Testing Guidelines

Name DIY fixtures `tests/diy-posix-test/*.wast` and Node harnesses
`tests/diy-posix-test/*-runtime.cjs`. Test native and browser C artifacts when
changing shared engine semantics. Use OCaml only to compare Wasm/WAT/WAST
language behavior. Existing direct/threaded OCaml POSIX probes are legacy
coverage to account for during kernel retirement, not gates for C kernel work.
Browser changes must continue to work as a single offline `file://` document;
do not introduce a server, external assets, or cross-origin-isolation
requirements. Treat local POSIX fixtures as regression probes. Broader
conformance work should trace tests to The Open Group suites. Revisit LTP's
`testcases/open_posix_testsuite` after a guest C compiler works; then record
upstream revisions and keep licensing and Wasm-adaptation patches separate.
Keep libc test clients in `tests/libc-test/*.wast.inc`; generated fixtures
belong under `build/html-rt/waste-libc/tests/`.
Compare C Wasm/WAT/WAST language behavior with the OCaml oracle for every
supported official language test. Missing OCaml POSIX imports are outside
oracle scope; do not develop providers or block C kernel acceptance on them.
Run C decoder/executor tests natively with warnings-as-errors, AddressSanitizer,
and UndefinedBehaviorSanitizer, then exercise the same artifact through the
offline Bash/test page. Do not claim a browser speedup from native OCaml timings.
Keep engine-global data immutable. Give each scheduled test an isolated host
store and kernel; model processes with private address spaces and threads with
shared process memory. Preserve explicit imported-memory aliasing within one
test sandbox.
Do not put browser imports, native syscalls, or platform-specific allocation in
`src/engine/`. The runtime Makefiles should compose the shared engine with one
platform backend; generated Flex/Bison output belongs under `build/engine/gen/`
and must not be edited directly.

## Commit & Pull Request Guidelines

Use short imperative subjects such as `Fix tests`. Keep commits scoped. Pull
requests should summarize behavior, tests, patch changes, and tradeoffs; include
screenshots for UI changes and result JSON only for relevant regressions.
