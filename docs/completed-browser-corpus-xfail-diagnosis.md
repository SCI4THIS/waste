# Browser-corpus XFAIL diagnosis and remediation plan

Status: completed 2026-10-02 — all infrastructure fixes landed.
Phase 0 (evidence), Phase 1 (Family A limit matcher), Phase 2
(Family B silent-drop root cause: WAST_MAX_EXPORTS cap), the
`time-resource` sub-item of Phase 2b (libc-overlay `close` export),
Phase 3 (baseline flip to 278/0/2/0), and Phase 4 (document
closure/rename) are all done.  Two POSIX-surface conformance XFAILs
remain tracked in `tests/browser-corpus-expected-failures.txt`
(`libc-test/environment-boundaries`, `libc-test/terminal`); they
are intentional deferrals to the shared-library libc roadmap and
are not infrastructure bugs.  Follow-up pointer in Stage 6B.5 of
`docs/active-browser-vfs-layout-plan.md`.
Scope: the 13 pre-existing browser-corpus expected failures tracked in
`tests/browser-corpus-expected-failures.txt`.  These are the only tests
whose status differs from PASS in a clean Stage 6B.4 run.  All other
267 corpus tests pass.

Last evidence capture: 2026-10-02, via
`tests/c-engine-browser-runtime.cjs` with the expected-failures file
temporarily moved aside so the FAIL branch prints `message.failures`
to stderr.  Raw log saved to `/tmp/xfail-diag/all.log`.  Persist that
log into `build/engine/refactor-stage6b-xfail-diagnosis/` as part of
remediation Phase 0 so the evidence is tracked in the repo.

## The 13 failures split into two families

### Family A — Permissive memory import limit matching (3 tests)

Failing identities and failure counts:

| Identity                                       | Failures |
|------------------------------------------------|---------:|
| `core/imports.wast`                            |        7 |
| `core/memory64/memory64-imports.wast`          |        2 |
| `core/multi-memory/imports2.wast`              |        1 |

Reported errors are a mix of:

- `"module unexpectedly instantiated"` — emitted from
  `src/engine/wast/handler.c:167` when an `assert_unlinkable` module
  instantiates successfully instead of failing.
- Numeric result mismatches on a follow-up `grow` export
  (`core/imports.wast` indices 122 and 123, `actual 2 expected 1`
  and `actual -1 expected 1`) — direct consequence of accepting an
  import that should have been rejected: memory state leaks from the
  wrongly-linked instance into the next module.

Root cause.  The engine's link-stage import matcher is permissive
about memory (and by extension table) limit subtyping.  The spec
rule for an importer-vs-exporter limits pair `(importer_min,
importer_max?)` against `(exporter_min, exporter_max?)` is:

1. `exporter_min >= importer_min`.
2. If `importer_max` is present then `exporter_max` must also be
   present AND `exporter_max <= importer_max`.

The failing cases in `submodules/wasm-spec/test/core/imports.wast`
around lines 580–680 target exactly those two clauses, e.g.:

```
(assert_unlinkable
  (module (import "test-memory-2-4" "memory-2-4" (memory 2 3)))
  "incompatible import type")
```

The exporter here has limits `2 4`; the importer requires an upper
bound of 3, so clause 2 should fail (`4 > 3`).  Our engine accepts
the link anyway.  `memory64-imports.wast` and `multi-memory
imports2.wast` exercise the same clauses in their respective
dialects.

### Family B — Silent client-module drop in libc-test fixtures (10 tests)

Failing identities (all libc-test):

```
libc-test/entropy-messages.wast
libc-test/environment-boundaries.wast
libc-test/matching-sort.wast
libc-test/memory-conversion.wast
libc-test/path-runtime.wast
libc-test/select-abi.wast
libc-test/select-runtime.wast
libc-test/stat-abi.wast
libc-test/terminal.wast
libc-test/time-resource.wast
```

Reported error for every invoke in these fixtures:

```
"error": "unknown module id"
```

Source: `src/engine/wast/handler.c:109`, where
`handler_assertion_module` looks up the module id of the invoke's
target and finds no entry in the store.

All ten fixtures share the same structural shape:

```
(module $waste_libc ...)                       ;; libc overlay
(register "waste-libc" $waste_libc)            ;; name it for linking
(module $client_tests ...)                     ;; test client, imports libc
(assert_return (invoke $client_tests "func") ...)
...
```

Evidence that rules out the obvious hypotheses:

- Every POSIX import the client uses is resolved by
  `guest_posix_host_resolver` in `src/engine/guest_posix.c`
  (both `waste_kernel::*` and `env::*` names at lines 2456–2510).
- The 24 import signatures line up with corresponding exports in
  the libc overlay (type-checked by hand for
  `entropy-messages`'s 7 symbols; the others follow the same shape).
- The lexer stores `$`-prefixed IDs verbatim
  (`src/engine/wat/lexer.l:844`), so there is no name mismatch
  between the `(module $client_tests …)` declaration and the
  `(invoke $client_tests …)` lookup.
- Structurally identical libc-test fixtures (stdio, allocator,
  accounts, locale-wide) pass, so the shape "overlay + register +
  client + invoke" is not inherently broken.
- The result record shows `resultCount == failureCount == N` where
  N is the number of invokes.  No module failure is recorded,
  so `handler_module` is not reporting an error — it is silently
  completing without registering the client in the store.

Likely root cause (unconfirmed, needs instrumentation).
`handler_module` at `src/engine/wast/handler.c:118–185` takes a
silent-drop path for the client module in these ten cases.  Three
candidates stand out:

1. `EXEC_YIELD` during instantiation (if the client's start
   function or data-segment copies cross a quantum boundary) and
   the handler fails to re-enter.
2. `handler_retain` exhausting its capacity
   (`HANDLER_COMMAND_MAX = 1024`) — the libc overlay is large and
   may be pushing the retention table past the limit before the
   client is added.
3. Store reset between the overlay add and the client add (for
   example an incorrect reset tied to the `(register …)` command).

The ten failing fixtures are the ones whose client modules import
the broadest cross-section of libc symbols (dirent, resource,
terminal, select, stat, pselect, environment boundary surface),
while the passing fixtures import narrower surfaces.  That
correlation is consistent with hypothesis 2 (retention overflow)
but does not prove it.

## Remediation plan

### Phase 0 — Persist evidence (DONE 2026-10-02)

Diagnostic evidence has been copied from `/tmp/xfail-diag/` into
`build/engine/refactor-stage6b-xfail-diagnosis/`:

- `per-test-diagnostics.log` — concatenated stdout+stderr for each
  of the 13 failing identities, captured by the corpus harness with
  `tests/browser-corpus-expected-failures.txt` temporarily moved
  aside so the FAIL branch prints `message.failures` to stderr.
- `libc-needed-imports.txt` — the 24-symbol union of imports used
  by the ten failing libc-test client modules.
- `libc-resolved-imports.txt` — symbols resolved by
  `guest_posix_host_resolver` in `src/engine/guest_posix.c`.  Every
  needed import is covered, which is how Family B was ruled out as
  an unresolved-import problem.
- `summary.txt` — capture procedure, family split, and file index.

Baseline re-run after persistence: 267 PASS / 0 FAIL / 13 XFAIL /
0 XPASS (unchanged — Phase 0 is pure evidence capture, no code
changes).

### Phase 1 — Family A: tighten limit subtyping (DONE 2026-10-02)

The spec limit check in `src/engine/wasm/load.c:287-290` was already
correct; the leak came from two linker-side mutations that bypassed
it.  Both were fixed:

- `src/engine/store.c:1451-1464` — `native_load_module` previously
  called `native_store_grow_table` on *any* imported table to the
  importer's minimum before `wasm_load` ran its subtyping check,
  which masked `assert_unlinkable (table N …)` cases from the three
  Family A fixtures.  The pre-grow is now restricted to the shared
  runtime table (`env.__indirect_function_table`), the only import
  that still needs coordinated sizing across executables.
- `src/engine/process.c:610-634` — `native_process_bind_engine_memory`
  used to stamp `access_check`, `virtual_max_pages`, and
  `process_virtual_memory = 1` onto whatever memory `engine->memory`
  pointed at.  When a WAST invoke targeted a module that imports
  `spectest.memory`, this permanently promoted the shared spec
  memory to a process-virtual memory, causing later limit-matching
  assertions and the `(memory.grow)` test (`imports.wast` indices
  122–123) to observe growth that never happened natively.  The
  helper now walks `engine->owns_memories[]` and only mutates
  memories the engine actually owns.

The native `wast_run_assertion` path does not route through
`native_process_driver_invoke`, which is why native cli-rt never
exhibited the browser-only memory leak — the asymmetry was invisible
until the browser corpus harness landed.

Verification (2026-10-02):

- Native `build/cli-rt/waste-cli` on all three fixtures:
  `core/imports.wast` 144/144, `core/memory64/memory64-imports.wast`
  30/30, `core/multi-memory/imports2.wast` 14/14.
- Browser corpus (`node tests/c-engine-browser-runtime.cjs
  build/html-rt/tests/payload.json --json`) with the three Family A
  entries removed from `tests/browser-corpus-expected-failures.txt`:
  270 PASS / 0 FAIL / 10 XFAIL / 0 XPASS.  The ten remaining XFAILs
  are the Family B libc-test fixtures tracked by Phase 2.

### Phase 2 — Family B: WAT export cap silently truncating overlays (DONE 2026-10-02)

Instrumentation ruled out every hypothesis in the original plan
(`handler_module`, `EXEC_YIELD`, `handler_retain` capacity, store
reset).  Family B fixtures never route through
`wast_process_handler_step` — top-level corpus modules go through
`browser_process_module` → `native_load_module` directly.

Root cause.  `src/engine/wat/types.h:221` defined
`WAST_MAX_EXPORTS = 256`.  The waste-libc overlay module exposes
~393 symbols; the two emission sites in the WAT parser
(`src/engine/wat/parser.y:5209` and `:5273`) silently dropped
every standalone `(export …)` past the first 256, so symbols
whose declaration order placed them after offset 256 in the WAT
file disappeared from the engine's export table.  Subsequent
`native_load_module` resolution of the client module hit
`store.c:1378` ("unresolved function import …") during link,
`browser_process_module` took the status-not-OK silent-drop
branch at `browser_api.c:960–966` (no module assertion), and
later invokes against the un-registered client surfaced as
`unknown module id` at `browser_api.c:861` — the Family B
signature.

Instrumentation that pinned this down: a temporary
`(prov_exports=%u)` suffix on the import-resolution error in
`store.c:1378` reported `prov_exports=256` for every Family B
fixture, matching the cap exactly.

Fixes:

- `src/engine/wat/types.h:221` — raised `WAST_MAX_EXPORTS` from
  `256` to `1024` with a comment pointing at the waste-libc
  overlay.  Downstream engine caps (`EXEC_MAX_EXPORTS = 65536`)
  were never the bottleneck.
- `src/engine/wat/parser.y:5209,5273` — the two silent
  `if (mod->export_count < WAST_MAX_EXPORTS)` guards now call
  `report_validation_error(script, "too many exports")` on
  overflow instead of dropping entries, so any future module
  that exceeds the cap produces a parse-time error rather than
  the Family-B class of ghost failures.

Verification (2026-10-02, browser corpus
`node tests/c-engine-browser-runtime.cjs
build/html-rt/tests/payload.json --group=libc-test`):

- Before Phase 2: 4 PASS / 10 FAIL / 0 XFAIL (xfail list cleared
  for the measurement; those 10 are the plan's Family B set).
- After Phase 2: 11 PASS / 3 FAIL.  The seven libc-test fixtures
  cleared are `entropy-messages`, `matching-sort`,
  `memory-conversion`, `path-runtime`, `select-abi`,
  `select-runtime`, and `stat-abi`.

Three fixtures still fail — distinct bugs, not the Family-B
silent-drop class:

| Fixture                                | Residual failure                                        |
|----------------------------------------|---------------------------------------------------------|
| `libc-test/time-resource.wast`         | overlay does not export `close`; `(import "waste-libc" "close" …)` is unresolvable regardless of the export cap |
| `libc-test/environment-boundaries.wast`| 3 of 6 asserts return `0` instead of `1` (`boundary-execve`, `boundary-readlink`, `boundary-opendir`) — guest-visible semantics of unsupported POSIX surface |
| `libc-test/terminal.wast`              | `terminal` invoke returns `0` instead of `1` — terminal-side behavior differs from the client's expectation |

These are tracked separately as Phase 2b (below) rather than
expanded into a new family of silent-drop hypotheses.

### Phase 2b — Residual libc-test XFAILs

The three fixtures left after Phase 2 are legitimate behavioral
gaps, not infrastructure bugs.  One has been cleared; two are
deferred.

#### time-resource — CLEARED 2026-10-02

Root cause.  `src/html-rt/lib/unistd.c:14` previously declared
`close` only as `extern i32 close(i32)` with no definition or
`import_module` attribute.  The clang-wasm linker therefore left
`close` as an undefined weak reference inside the overlay rather
than emitting it as an exported symbol, so client modules such
as `libc-test/time-resource.wast` that imported
`(import "waste-libc" "close" …)` failed link.

Fix.  Introduced an engine-side import for the host primitive
and a defined wrapper that forwards to it (`src/html-rt/lib/unistd.c`):

```c
extern i32 waste_env_close(i32 descriptor)
  __attribute__((import_module("env"), import_name("close")));
...
i32 close(i32 descriptor) { return env_result(waste_env_close(descriptor)); }
```

This mirrors the `open` wrapper pattern (`waste_kernel_open_v1` →
`open`).  With the wrapper defined, `--export-all` surfaces
`close` in the overlay's export table, so client imports resolve.

Verification.  `./start.sh --build-libc` + regenerate corpus
payload with `src/html-rt/tools/generate-c-engine-tests.py
--ocaml-layout --output-dir build/html-rt/tests` +
`node tests/c-engine-browser-runtime.cjs
build/html-rt/tests/payload.json --json` now reports
`libc-test/time-resource.wast` as PASS, and the entry has been
removed from `tests/browser-corpus-expected-failures.txt`.  Final
baseline **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS**.  Artifacts in
`build/engine/refactor-stage6b-close-export/` (`summary.txt`,
`corpus-json.log`, `corpus.log`).

#### environment-boundaries, terminal — deferred

These two fixtures assert boolean compositions over multiple
POSIX stubs.  The failing booleans are:

- `environment-boundaries`: `boundary-execve`, `boundary-readlink`,
  `boundary-opendir` all return `0`.  The fixture expects each
  stub to return `-1` with `errno == ENOSYS (38)`.  Our actual
  implementations now reach real engine handlers
  (`guest_posix_execve`, `guest_posix_readlink`,
  `src/html-rt/lib/dirent.c:17 opendir`) that return other errnos
  (e.g. `ENOENT`, `EFAULT`) when the fixture passes a junk path at
  offset `180000`.  The fixture is testing an older ENOSYS-stub
  surface than what the overlay now exposes.
- `terminal`: the `terminal` invoke runs a nine-step `i32.and`
  reduction over `ttyname`, `tcflow`, `tgetent`, `tgetflag`,
  `tgetnum`, `tgetstr`, `tgoto`, `tputs`, and a put-callback
  side-effect check.  At least one sub-check differs from the
  fixture's expectation; bisecting which one requires per-call
  instrumentation.

Clearing either is out of scope for the browser-corpus-xfail
diagnosis plan — the work belongs with the shared-library libc
roadmap and/or a dedicated POSIX-surface conformance pass.  Both
entries remain on `tests/browser-corpus-expected-failures.txt`
with a comment pointing at this plan.

### Phase 3 — Flip XFAIL to PASS and re-baseline (DONE 2026-10-02)

`tests/browser-corpus-expected-failures.txt` has been reduced
from thirteen entries to two (`environment-boundaries`,
`terminal`), with the header comment pointing at Phases 1, 2,
and 2b of this plan.  After the Phase 2b `close`-export fix the
baseline is **278 PASS / 0 FAIL / 2 XFAIL / 0 XPASS**.  The
280/0/0/0 target depends on resolving the remaining Phase 2b
POSIX-surface items and is intentionally deferred.

### Phase 4 — Document closure (DONE 2026-10-02)

- This document was renamed from
  `docs/active-browser-corpus-xfail-diagnosis.md` to
  `docs/completed-browser-corpus-xfail-diagnosis.md` and the top
  Status line flipped to completed.
- `docs/active-browser-vfs-layout-plan.md` picks up the thread
  under a new "Stage 6B.5 browser-corpus XFAIL remediation"
  heading that links back here and points at the two artifact
  directories (`build/engine/refactor-stage6b-xfail-cleared/` and
  `build/engine/refactor-stage6b-close-export/`).
- `tests/browser-corpus-expected-failures.txt` already references
  this plan by name in its header comment; the reference remains
  valid under the renamed path.

## Out of scope

- Native `build/cli-rt/waste-wast` runs of the libc-test fixtures
  fail with "unresolved function import" because the native
  binary has no POSIX stub resolver.  That is a separate native
  limitation and is not part of this plan.
- True per-test cancellation (worker-thread isolation) remains
  deferred — see the Stage 6B.4 note in the main plan.
