# Repository Guidelines

## Project Structure & Module Organization

The developing C engine is in `src/`, including `src/bash.wat`; generated
reference material is under `src/gen/`. Follow `docs/c-engine-port-plan.md`: C
is the planned browser runtime, while the official OCaml interpreter in
`submodules/wasm-spec` remains the differential oracle during migration.
Read `docs/c-engine-handoff.md` before extending the C proof; it records the
supported subset, verified Firefox baseline, reproduction gate, and next steps.
Represent repository-owned OCaml changes in
`submodules/wasm-spec-i31-int32.patch`, never in submodule history. Dashboard
code is in `tools/`, the guest libc is in `libc/`, architecture notes are in
`docs/`, and project-specific probes are in `tests/diy-posix-test/` and
`tests/libc-test/`. Treat `build/` as generated output.

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
- `./start.sh --compile`: build direct and CPS OCaml-to-Wasm artifacts.
- `./start.sh --build-libc`: build the guest allocator module and test fixture.
- `./start.sh --generate-html`: generate the offline dashboard.
- `./start.sh --generate-bash-html`: generate the offline WASTE Bash page.
- `./build.sh`: extract legacy grammar fragments into `gen/`.
- `(cd src && ./m.sh)`: build the current native C prototype.
- `./start.sh --c-tail-poc`: build and benchmark the bounded C tail-call proof
  and generate its self-contained browser page.
- `tests/c-tail-poc/run.sh 5000000`: run direct and reference tail-call gates.
- `node tests/diy-posix-test/posix-{kernel,control}-runtime.cjs [threaded]`:
  run DIY POSIX probes.
- `node tests/libc-test/libc-runtime.cjs [threaded]`: run guest allocator probes.
- `node tests/libc-test/allocator-native.cjs`: stress the built allocator natively.

Before submitting shell or Python changes, run `bash -n start.sh`, Python bytecode
checks for changed tools, and `git diff --check`.

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
`tests/diy-posix-test/*-runtime.cjs`. Test direct and `threaded` artifacts when changing
scheduler, evaluator, signal, or POSIX behavior. Browser changes must continue
to work as a single offline `file://` document; do not introduce a server,
external assets, or cross-origin-isolation requirements. Treat local POSIX
fixtures as regression probes. Broader conformance work should trace tests to
The Open Group suites. Revisit LTP's `testcases/open_posix_testsuite` after a
guest C compiler works; then record upstream revisions and keep licensing and
Wasm-adaptation patches separate.
Keep libc test clients in `tests/libc-test/*.wast.inc`; generated fixtures
belong under `build/waste-libc/tests/`.
Compare new C behavior with the OCaml oracle for every supported official test.
Run C decoder/executor tests natively with warnings-as-errors, AddressSanitizer,
and UndefinedBehaviorSanitizer, then exercise the same artifact through the
offline dashboard. Do not claim a browser speedup from native OCaml timings.
Keep engine-global data immutable. Give each scheduled test an isolated host
store and kernel; model processes with private address spaces and threads with
shared process memory. Preserve explicit imported-memory aliasing within one
test sandbox.
Treat `src/c-engine/` as the new port and the older flat `src/*.c` files as
reference groundwork. Do not broaden the proof subset without a fixture and a
documented delivery-phase gate.

## Commit & Pull Request Guidelines

Use short imperative subjects such as `Fix tests`. Keep commits scoped. Pull
requests should summarize behavior, tests, patch changes, and tradeoffs; include
screenshots for UI changes and result JSON only for relevant regressions.
