# Repository Guidelines

## Project Structure & Module Organization

The production C implementation is split by responsibility. Platform-neutral
parser, encoder, validator, linker, runner, and executor code lives in
`src/engine/`. The native command-line runtime, mmap harness, syscall-backed C
library, and native build rules live in `src/cli-rt/`. The browser/Wasm API,
browser POSIX adapters, HTML generators, and browser
build rules live in `src/html-rt/`. Keep shared semantics in `src/engine/`; add
platform behavior only to the matching runtime directory. Compiled Bash input
is in `examples/bash.wat`.

Follow `docs/architecture.md`: C is the browser runtime. The OCaml reference
interpreter in `submodules/wasm-spec/interpreter` was used as a language
reference while implementing the WAT/WAST portions of the C engine. See
[OCaml reference interpreter build](docs/techniques.md#ocaml-reference-interpreter-build)
for the minimal build/test instructions.
Read `docs/techniques.md` before extending the engine; it records the parser,
validation, execution, linking, browser, and verification practices established
during the port. Current staged work is tracked in the `docs/active-*.md`
plans.
Packaging tools are in `src/html-rt/tools/`; guest libc sources are in
`src/libc/`. Build/install the shared guest library with `make -C src/libc install`.
Auxiliary commands build through `src/aux/Makefile`; Rogue's package profile and
patches live in `src/aux/rogue`. Use `make -C src/aux rogue` and `install-rogue`;
stage upstream sources under `build/aux/rogue` and leave the submodule untouched.
Portable support needed by the interpreter itself lives in `src/libc/runtime/`
and is statically linked into each runtime; kernel semantics stay in `src/engine/`.
Architecture notes are in `docs/`, and project probes
are in `tests/`, especially `tests/diy-posix-test/` and `tests/libc-test/`.
Treat all of `build/` as generated output. Shared generated engine sources and
logs go under `build/engine/`, native executables under `build/cli-rt/`, and
browser artifacts under `build/html-rt/`.
Treat every checked-out submodule as a read-only source dependency. Never apply
patches, generate files, bootstrap, configure, or build inside a submodule
checkout. Copy or stage inputs under `build/` first, apply repository-owned
patches to that staging copy, and direct every generated output into `build/`.
See `docs/submodule-policy.md`.
The current `src/vfs` directory is the guest filesystem source of truth.
Native mounting discovers its files directly; HTML packaging discovers the same
tree and generates its transport metadata automatically. No stored inventory or
source-hash approval is required for edits or additions. Compile under `build/`,
then copy or explicitly install with `src/html-rt/tools/vfs.py`; HTML packaging
must not compile guest binaries or reinstall snapshots over local edits.
Authored guest public headers live in `src/vfs/usr/include`; compiler support
snapshots live in `src/vfs/usr/lib/waste/cc/include`. Keep engine/native headers
and libc `helper.h` private. Use `guest-sdk-check` for explicit header/ABI/provider
checks; `guest-sdk-install` refreshes selected dependency headers when requested.
See `docs/guest-sdk.md` for informational SDK metadata and build profiles.
Test files in `src/vfs/root/waste/tests` are installed distribution snapshots,
not authored sources. They mount at `/root/waste/tests`. Refresh with
`vfs-tests-install` and audit with `vfs-tests-check`; the single `bash.html`
page runs browser corpus diagnostics from these mounted snapshots. See
`docs/techniques.md` (Installed Corpus Workflow).
Authored portable executor regressions live in `tests/engine-regressions/*.wast`;
`docs/techniques.md` (Test Boundary Selection) explains the retained private
checks. The `i32-smoke`, `caller-instance` and `continuation` gates require their
installed regression snapshots to match authored inputs.

## POSIX Capability Policy

Use browser primitives when faithful and emulate practical OS semantics in the
C engine-owned kernel. Verify kernel/POSIX behavior across the native and
browser C runtimes. Route unavailable capabilities such as raw sockets
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
- `./start.sh --build-libc`: build the guest libc module and generated test
  fixtures.
- `./start.sh --generate-bash-html`: generate the offline WASTE Bash page.
- `./start.sh --ocaml-reference`: stage and run the OCaml reference interpreter
  under `build/ocaml-interpreter/`; see
  [OCaml reference interpreter build](docs/techniques.md#ocaml-reference-interpreter-build).
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

Name DIY fixtures `tests/diy-posix-test/*.wast`. Test native and browser C
artifacts when changing shared engine semantics. Browser changes must continue
to work as a single offline `file://` document; do not introduce a server,
external assets, or cross-origin-isolation requirements. Treat local POSIX
fixtures as regression probes. Broader conformance work should trace tests to
The Open Group suites. Revisit LTP's `testcases/open_posix_testsuite` after a
guest C compiler works; then record upstream revisions and keep licensing and
Wasm-adaptation patches separate.
Keep libc test clients in `tests/libc-test/*.wast.inc`; generated fixtures
belong under `build/html-rt/waste-libc/tests/`.
The OCaml reference interpreter (`./start.sh --ocaml-reference`) may be used
to cross-check WAT/WAST language behavior on supported official tests; see
[OCaml reference interpreter build](docs/techniques.md#ocaml-reference-interpreter-build).
Run C decoder/executor tests natively with warnings-as-errors, AddressSanitizer,
and UndefinedBehaviorSanitizer, then exercise the same artifact through the
offline Bash/test page.
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
