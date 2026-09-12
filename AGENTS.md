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

Follow `docs/c-engine-port-plan.md`: C is the browser runtime, while the
official OCaml interpreter in `submodules/wasm-spec` remains the differential
oracle. Read `docs/c-engine-handoff.md` before extending the engine; it records
the supported suites, browser baseline, reproduction gates, and implementation
history. Some historical command examples in that document predate the runtime
directory split, so use the commands below for current builds.
Represent repository-owned OCaml changes in
`submodules/wasm-spec-i31-int32.patch`, never in submodule history. Dashboard
and packaging tools are in `src/html-rt/tools/`; guest libc sources are in
`src/html-rt/lib/`. Architecture notes are in `docs/`, and project probes
are in `tests/`, especially `tests/diy-posix-test/` and `tests/libc-test/`.
Treat all of `build/` as generated output. Shared generated engine sources and
logs go under `build/engine/`, native executables under `build/cli-rt/`, browser
artifacts under `build/html-rt/`, and OCaml intermediates under `build/ocaml/`.

## POSIX Capability Policy

Use browser primitives when faithful and emulate practical OS semantics in the
engine-owned kernel. During migration, keep the OCaml and C implementations
behaviorally aligned. Route unavailable capabilities such as raw sockets
through an optional WebSocket POSIX broker. Keep POSIX state in the engine—not
the broker—and make the protocol versioned, asynchronous, capability-scoped,
and explicit about `errno`, cancellation, and readiness. Without it, return an
unsupported error. The dashboard must remain a self-contained `file://`
document; broker use is optional.

## Build, Test, and Development Commands

- `./start.sh`: open the dependency/build/dashboard wizard.
- `./start.sh --check`: inspect dependencies and submodule state.
- `./start.sh --cli-compile`: build the native C WAST runner.
- `./start.sh --cli-test`: run the top-level core spec files with the native C
  runner.
- `./start.sh --html-test`: build the browser C engine and generate the full
  self-contained OCaml-layout test dashboard.
- `./start.sh --html-bash`: build and verify the self-contained C-engine Bash
  page.
- `./start.sh --compile`: build direct and CPS OCaml-to-Wasm artifacts.
- `./start.sh --build-libc`: build the guest libc module and generated test
  fixtures.
- `./start.sh --generate-html`: generate the offline dashboard.
- `./start.sh --generate-bash-html`: generate the offline WASTE Bash page.
- `make -C submodules SWITCH_NAME=waste-wasm wasm`: temporarily apply the
  repository-owned OCaml patch, build sequential and CPS OCaml-to-Wasm
  artifacts, and restore the spec submodule even if the build fails.
- `make -C submodules SWITCH_NAME=waste-wasm native`: use the same patch
  transaction to build the native OCaml differential oracle.
- `make -C src/cli-rt BUILD_DIR=../../build/cli-rt wast-native`: build the
  native CLI runner.
- `make -C src/cli-rt BUILD_DIR=../../build/cli-rt i32-smoke`: run the native
  warnings-as-errors ASan/UBSan executor smoke gate.
- `make -C src/html-rt BUILD_DIR=../../build/html-rt wast-browser`: compile the
  same engine semantics for the browser.
- `build/cli-rt/waste-cli --parse-only FILE...`: rapidly parse selected WAST
  files without Node or a browser.
- `node tests/c-engine-browser-runtime.cjs build/html-rt/test.html`: exercise a
  generated offline C-engine dashboard through its worker harness.
- `node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html`: verify
  delayed input, command execution, a second prompt, and exit in C-engine Bash.
- `node tests/diy-posix-test/posix-{kernel,control}-runtime.cjs [threaded]`:
  run legacy OCaml-runtime DIY POSIX probes.
- `node tests/libc-test/libc-runtime.cjs [threaded]`: run guest libc probes.
- `node tests/libc-test/allocator-native.cjs`: stress the built allocator natively.

The old `--c-engine-html`, `--c-engine-bash-html`, and
`--c-engine-core-tests` flags remain compatibility aliases or focused gates;
prefer the sectioned `--cli-*` and `--html-*` commands for new documentation.
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
changing shared engine semantics. Test direct and `threaded` OCaml artifacts
when changing their scheduler, signal, or POSIX compatibility behavior.
Browser changes must continue to work as a single offline `file://` document;
do not introduce a server, external assets, or cross-origin-isolation
requirements. Treat local POSIX fixtures as regression probes. Broader
conformance work should trace tests to The Open Group suites. Revisit LTP's
`testcases/open_posix_testsuite` after a guest C compiler works; then record
upstream revisions and keep licensing and Wasm-adaptation patches separate.
Keep libc test clients in `tests/libc-test/*.wast.inc`; generated fixtures
belong under `build/html-rt/waste-libc/tests/`.
Compare new C behavior with the OCaml oracle for every supported official test.
Run C decoder/executor tests natively with warnings-as-errors, AddressSanitizer,
and UndefinedBehaviorSanitizer, then exercise the same artifact through the
offline dashboard. Do not claim a browser speedup from native OCaml timings.
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
