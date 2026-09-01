# Repository Guidelines

## Project Structure & Module Organization

The C interpreter is in `src/`, including `src/bash.wat`; generated reference
material is under `src/gen/`. The official OCaml interpreter is the
`submodules/wasm-spec` submodule. Represent repository-owned OCaml changes in
`submodules/wasm-spec-i31-int32.patch`, never in submodule history. Dashboard
code is in `tools/`, the guest libc is in `libc/`, architecture notes are in
`docs/`, and project-specific probes are in `tests/diy-posix-test/` and
`tests/libc-test/`. Treat `build/` as generated output.

## POSIX Capability Policy

Use browser primitives when faithful and emulate practical OS semantics in
OCaml. Route unavailable capabilities such as raw sockets through an optional
WebSocket POSIX broker. Keep POSIX state in OCaml and make the broker versioned,
asynchronous, capability-scoped, and explicit about `errno`, cancellation, and
readiness. Without it, return an unsupported error. The dashboard must remain a
self-contained `file://` document; broker use is optional.

## Build, Test, and Development Commands

- `./start.sh`: open the dependency/build/dashboard wizard.
- `./start.sh --check`: inspect dependencies and submodule state.
- `./start.sh --compile`: build direct and CPS OCaml-to-Wasm artifacts.
- `./start.sh --build-libc`: build the guest allocator module and test fixture.
- `./start.sh --generate-html`: generate the offline dashboard.
- `./build.sh`: build the C implementation.
- `node tests/diy-posix-test/posix-{kernel,control}-runtime.cjs [threaded]`:
  run DIY POSIX probes.
- `node tests/libc-test/libc-runtime.cjs [threaded]`: run guest allocator probes.
- `node tests/libc-test/allocator-native.cjs`: stress the built allocator natively.

Before submitting shell or Python changes, run `bash -n start.sh`,
`python3 -m py_compile tools/generate-browser-tests.py`, and `git diff --check`.

## Coding Style & Naming Conventions

Match surrounding indentation. Use `snake_case` for functions, uppercase shell
constants, and kebab-case dashboard/log names. Quote shell expansions. Do not
edit generated files or upstream submodule history directly.

## Testing Guidelines

Name DIY fixtures `tests/diy-posix-test/*.wast` and Node harnesses
`tests/diy-posix-test/*-runtime.cjs`. Test direct and `threaded` artifacts when changing
scheduler, evaluator, signal, or POSIX behavior. Browser changes must continue
to work as a single offline `file://` document; do not introduce a server,
external assets, or cross-origin-isolation requirements. Treat local POSIX
fixtures as regression probes. Broader conformance work should trace tests to
The Open Group suites. Revisit LTP's `testcases/open_posix_testsuite` after a
guest C compiler works; then record upstream revisions and keep licensing and
Wasm-adaptation patches separate.
Generate `tests/libc-test/allocator.wast` from the allocator source; do not edit
the generated fixture directly.

## Commit & Pull Request Guidelines

Use short imperative subjects such as `Fix tests`. Keep commits scoped. Pull
requests should summarize behavior, tests, patch changes, and tradeoffs; include
screenshots for UI changes and result JSON only for relevant regressions.
