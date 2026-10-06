# Browser/VFS refactor inventory

This is the Stage 1 path contract and baseline for
[the active refactor plan](active-browser-vfs-layout-plan.md). No source files,
guest binaries, headers, or tests were relocated in this stage.

Stage 2 subsequently flattened authored assets and removed generated source
staging. This document preserves the Stage 1 snapshot; consult the active plan
for current paths and Stage 2 results.

Scope update (2026-10-04): OCaml is retained only for Wasm/WAT/WAST language
verification. Direct/threaded POSIX dependencies recorded in this historical
snapshot are to be accounted for during deferred kernel retirement, not
maintained as a parallel application runtime or extended with new capabilities.
See [the OCaml retirement plan](active-ocaml-language-oracle-plan.md). The collector now reads metadata from
`build/html-rt/tests` and measures the actual packaged pages. Use a new output
directory for subsequent stages rather than overwriting the initial baseline.

## Snapshot and reproduction

Recorded on 2026-09-30 at repository revision
`81fd0064f3602f7e00ff7dbee0d820d260475e2d`. The official OCaml oracle remains
the clean spec submodule at `4b29bdbced924599346ea2ffd9e975af5d28c735` with
the repository-owned `submodules/wasm-spec-i31-int32.patch` build transaction.
The existing direct and threaded OCaml artifacts remain separate oracle/probe
dependencies; this stage did not run a new differential comparison.

The inventory tool requires the existing staged guest applications, dashboard
payload, and generated libc fixtures. Build the two C artifacts, then capture
the current staging state without regenerating or silently repairing it:

```sh
make -C src/cli-rt BUILD_DIR=../../build/cli-rt wast-native
make -C src/html-rt BUILD_DIR=../../build/html-rt wast-browser
python3 src/html-rt/tools/inventory-runtime-refactor.py \
  --output build/engine/refactor-baseline/inventory.json --measure
```

Omit `--measure` for a path/hash inventory only. For a selective rerun, add
`--measure --measure-filter native-core-` (or another run-name substring).
Retaining other measurements requires unchanged revision, bootstrap artifacts,
frontend inputs, headers, corpus, payload, and supporting fixtures.

`build/engine/refactor-baseline/inventory.json` records each frontend path,
destination, role, raw symlink target, size, mode, mtime, and SHA-256; header
classifications; source-qualified test identities and hashes; the complete
packaged specifications; old-path references; import-name differences;
build/bootstrap/package dependencies; and commands, exit status, durations,
and result-log paths. It detects destination collisions, corpus/payload
differences, and stale packaged WAST lengths. Measurement logs are under
`build/engine/refactor-baseline/results/`. The collector writes stdout and
stderr separately so native assertion JSON is not contaminated by encoder
diagnostics. Initial browser/Bash measurements used a combined log; entries
with `stderr_log` identify subsequently captured separate streams.

These are generated local artifacts, not checked-in sources. The tool records
baseline failures without treating them as an inventory-generation failure;
inspect each run's status before claiming verification. It is not a replacement
for the regression gates. Source hashes identify every upstream assertion even
where the staged `assertionCount` is zero because counting was not requested.

## Frontend and guest path contract

The 35 frontend/staging entries have these owners and destinations:

| Current source under `src/html-rt/src/` | Count | Destination/owner |
| --- | ---: | --- |
| `bash/{index.html,app.js,worker.js,style.css}` | 4 | Same names in `src/html-rt/src/`, authored shell frontend |
| `bash/terminal/{model.js,renderer.js,glf.js,GLF-NOTICES.md}` | 4 | `src/html-rt/src/terminal/`, shared terminal namespace |
| `shared/loader.js` | 1 | `src/html-rt/src/loader.js`, offline bootstrap |
| `shared/tarball.js` | 1 | `src/html-rt/src/tarball.js`, vendor reference; repair relative target |
| `tests/{index.html,app.js,worker.js,style.css}` | 4 | `src/html-rt/src/tests-{index.html,app.js,worker.js,style.css}`, temporary dashboard |
| `tests/payload.json` | 1 | `build/html-rt/tests/payload.json`, generated inventory/specifications |
| `{bash,tests}/waste-wast.wasm` | 2 | One `build/html-rt/waste-wast.wasm`, host engine, outside guest VFS |
| `bash/launch.wast` | 1 | `build/html-rt/bash-runtime.wast`, generated libc/Bash startup input |
| `bash/{true,false,pwd,echo,printf,basename,dirname,cat,wc,ls,date}.wasm` | 11 | `src/vfs/usr/bin/<name>`, installed Coreutils snapshots |
| `bash/{upload,download,ldd,rogue}.wasm` | 4 | `src/vfs/usr/bin/<name>`, installed auxiliary applications |
| `bash/waste-probe.wasm` | 1 | `src/vfs/usr/bin/waste-probe`, test executable snapshot |
| `bash/libncurses.so.wasm` | 1 | `src/vfs/lib/libncurses.so.wasm`, installed guest DSO |

The two engine links resolve to identical bytes and are an explicit allowed
destination alias, not an overwrite. There are no other destination collisions.
`shared/tarball.js` currently has the dangling target
`../../submodules/tarballjs/tarball.js`. From its flattened location the correct
target is `../../../submodules/tarballjs/tarball.js`. Packaging already reads the
authoritative vendor path directly; do not carry the dangling link forward.

Freeze the following guest-view compatibility contract:

- Executable snapshots omit `.wasm` in their installed guest names. Preserve
  both `/usr/bin/<name>` and `/bin/<name>` for Coreutils and auxiliaries.
  The probe currently exposes `/bin/waste-probe`; adding its canonical
  `/usr/bin` location must not remove that test path.
- Preserve `/lib/libncurses.so.wasm` and `/usr/lib/libncurses.so.wasm`. Verify
  guest alias execution before using links rather than identical copies.
- `/bin/wat` and `/bin/wast` are engine interpreter launchers, not host-engine
  Wasm files to rename. Preserve their executable metadata and
  `/usr/share/waste/waste-interpreters.json` registration.
- Bash is embedded in `examples/bash.wat` and the generated startup WAST;
  there is no staged standalone `bash.wasm` to move. Preserve startup module
  registration, environment, current directory, and shell `PATH`.
- Preserve Coreutils provenance, corresponding-source mapping, and `COPYING`
  at `/usr/share/waste/coreutils-provenance.json`,
  `/usr/share/waste/coreutils-source-package.json`, and
  `/usr/share/licenses/coreutils/COPYING`. The first two are generated under
  `build/coreutils`; the third is maintained in the Coreutils submodule.
- `vfs-mtimes.json`, archive manifests, decompression helpers, and HTML bytes
  are packaging outputs under `build/html-rt`. Metadata must describe the
  installed files' meaningful mtimes rather than tar's normalized timestamp.
  Keep the GLF notices and upstream Rogue/ncurses licensing with their source
  and distribution provenance; moving a binary does not transfer ownership.

Stage 2 flattens authored assets and removes generated payload/bootstrap source
staging. Guest Wasm installation belongs to Stage 3, not Stage 2.

## Header inventory

There are 92 source headers across engine, CLI, and HTML runtime:

| Class | Count | Contract |
| --- | ---: | --- |
| Private implementation/backend headers | 37 | Stay at their current source paths; native build include roots remain private |
| Guest application candidates in `src/html-rt/lib/include` | 52 | Preserve their relative include names under `src/vfs/usr/include`, after Stage 4 provider/dependency/ABI audit |
| `helper.h`, `waste-gnulib-compat.h` | 2 | Split public types from private helpers; keep gnulib forced-include policy package-scoped |
| `src/engine/include/waste.h` | 1 | Keep engine API source-local; optional `/usr/include/waste/waste.h` SDK only with a usable guest-linkable provider |

This is a path/role inventory, not a claim that all candidates already form a
hermetic SDK. Follow the plan's header-selection rules: compiler-support headers
are a separate target-specific closure; ncurses headers must match its configured
DSO; generated parser/config headers remain build products. Resolve the existing
`off_t`, guest metadata layouts, helper dependencies, `sys_ioctl.h` public naming,
and missing mapping interfaces before publishing the SDK. Native freestanding
headers are not interchangeable with guest libc declarations.

## Complete dashboard corpus

Preserve the existing qualified identities verbatim at
`src/vfs/root/waste/tests/<identity>`. In particular, proposal directories already live
under `core/`; inventing a new `proposals/` prefix would change identities.

| Existing group / destination below `/root/waste/tests` | Files |
| --- | ---: |
| `core` | 97 |
| `core/bulk-memory` | 8 |
| `core/exceptions` | 4 |
| `core/gc` | 17 |
| `core/memory64` | 25 |
| `core/multi-memory` | 41 |
| `core/relaxed-simd` | 7 |
| `core/simd` | 59 |
| `custom/custom` | 1 |
| `custom/metadata.code.branch_hint` | 1 |
| `custom/name` | 1 |
| `diy-posix-test` | 5 |
| `legacy/exceptions/core` | 4 |
| `libc-test` | 14 |
| Total | 284 |

There are 280 currently supported entries and four explicitly unsupported
legacy exception entries. Payload and source inventory identities match exactly:
no additions or omissions. Preserve `expectFailure`, unsupported reasons, and
all group subdivisions, including files named `.fail.wast`.

Official and libc inputs use `wast-stream`; four DIY inputs use assembled
`browser-native` module/step specifications and `mmap.wast` uses an embedded
C-engine stream with accompanying VFS bytes. Merely copying their WAST source
does not reproduce that execution contract. The inventory retains the assembled
specifications and fingerprints non-WAST support inputs in `tests/diy-posix-test`
and `tests/libc-test`, including `.wast.inc` clients and harnesses. Official
sources stay maintained in the pinned submodule, authored clients stay in
top-level `tests/`, and libc generated sources stay under
`build/html-rt/waste-libc/tests` before snapshot installation.

Current result contracts to preserve:

- Native: per-file JSON with `file`, `assertions` (`index`, `func`, `pass`,
  `error`), `passed`, and `total`; top-level diagnostic errors remain visible.
- Dashboard worker: `type: "done"`, `file`, and individual `results` with
  `pass` and diagnostics; `type: "error"` is not a skipped or passing test.
  The Node harness aggregates exit status but the UI retains assertion details.
- Bash harness: seven lifecycle checks plus scenario-specific output/status
  assertions. A reported `7/7` is not an exhaustive guest assertion count.

## Consumer/dependency map

The JSON contains line-level old-path references. Text matching is a review aid,
not a complete call graph; use this ownership map alongside it:

| Consumer | Required migration |
| --- | --- |
| `start.sh` | Frontend staging constants, generator/packager dispatch, installed auxiliary paths, future separate test actions |
| All three source Makefiles | Keep generated parser/build roots; later adjust only real public ABI/SDK dependencies, not private native include search |
| `tools/build.sh` | Replace `$SRC_DIR/$TARGET` and fixed entry filenames; separate authored assets from payload/bootstrap inputs; Stage 3 replaces flat Wasm discovery |
| `tools/amalgamate.py` | Select shell versus temporary dashboard entry/style/app names; update exact script tags and terminal fallback; keep offline inlining |
| `tools/generate-c-engine-tests.py` | Preserve corpus collection and payload generation; staged mode writes payload only; legacy monolithic mode has duplicate inline frontend templates |
| `tools/generate-c-engine-bash-html.py` | Staged mode copies generated binaries/bootstrap only; legacy monolithic mode has duplicate inline frontend templates and guest path tables |
| `tools/test_corpus.py` and `tools/test_distribution.py` | Preserve corpus collection, immutable installed snapshots, and language-oracle source policy independently of dashboard UI |
| `tools/build-{bash-runtime,waste-libc,waste-sysroot}.py` | Preserve bootstrap/fixture outputs under build; later use canonical guest SDK instead of hardcoded header root/host compiler fallback |
| Application builders and `tools/package-coreutils-source.py` | Keep compiler outputs/provenance under build; Stage 3 installs snapshots; Stage 4 changes guest SDK consumers |
| `tools/audit-coreutils-package.py` and shell worker/loader | Maintain required guest paths, interpreter registration, provenance, byte/mode/mtime checks and archive bootstrap |
| `tests/c-engine-{browser,bash-browser}-runtime.cjs` | Change source staging paths; preserve live-source WAST loading, worker protocol, fresh sandboxes, and guest package metadata |
| Terminal model/GLF Node fixtures | Change shared terminal source paths without changing fixture assertions |
| `AGENTS.md`, architecture/techniques/build docs, active plans | Update live workflow paths when their stage lands; do not present future CLI commands as already implemented |

`build.sh` currently normalizes archive timestamps and separately emits original
file mtimes. The browser harness's HTML fallback uses staging copies rather than
actually testing archive decompression. Stage 2 must exercise packaged offline
pages separately; passing the Node worker harness alone does not prove packaging
or DOM correctness. Resolve duplicate monolithic generator templates without
breaking supported output modes or regenerating obsolete authored JS.

## Native-runtime gap and consolidation boundaries

Literal import-name comparisons plus shared path ABI macros identify four CLI
names (`select_v1`, `pselect_v1`, `path_access_v1`, `path_stat_v1`) versus 95
browser names. The JSON lists the 91 browser-only names. This is not signature,
module-namespace, or provider validation; shared kernel support alone does not
make those imports usable from the native runner.

Stage 6 needs shared guest ABI decoding and native adapters for descriptor I/O,
startup/environment, clocks, signals/TTY, fork/exec/wait and continuations,
dynamic libraries, memory mapping, and simulated upload/download completion.
Keep platform behavior outside the engine; run the same guest bytes and metadata
rather than substituting Linux utilities.

Consolidation candidates are the standalone Coreutils flag scenarios versus the
aggregate utility matrix, common Bash startup/prompt/exit assertions across the
eleven sessions, and duplicated monolithic/staged frontend templates. Retain
diagnostic selectors; consolidate setup, not unique assertions. The clock,
installed-file mtime, launcher/directory mtime, completion, arrows, heredoc/pipe,
and two-child Rogue/terminal-restoration cases cover distinct behavior. Repeated
WAST waits, shared-file-page/imported-memory tests, sanitizer gates, and
direct/threaded OCaml probes were intentional boundary coverage at inventory.
Their useful kernel assertions now require C coverage accounting before
retirement, not ongoing OCaml kernel parity.
An exact old-to-new assertion mapping is required before Stage 6 deletes any
scenario; none were deleted during inventory.

## Measured starting baseline

Fresh C native and browser builds succeeded. Measurements below are local
sequential wall-clock observations, not projected speedups or native/browser
equivalence claims:

| Selection | Outcome | Elapsed | Startup count |
| --- | --- | ---: | ---: |
| Native top-level core corpus | 96/97 files pass; 20,063/20,066 assertions pass | 35.283 s | 97 native invocations |
| Full supported dashboard corpus in Node | 263/280 files pass | 204.554 s | 280 independent worker VM/engine test contexts |
| Bash integration selections | All 11 pass | 23.334 s | 11 engine/Bash sessions |
| Terminal model | Pass | 0.168 s | One Node fixture process |
| Terminal GLF | Pass | 0.474 s | One Node fixture process |

Bash selections are baseline delayed input/exit, command-not-found,
readline echo, completion, arrows, heredoc/pipe, Coreutils matrix, shared-library
Rogue, UTC clock, executable mtime, and build/launcher-directory mtime.
Their commands and time-dependent expectations are recorded in the generated
report. Repeated commands/children remain in one session where intentional.

Known baseline failures, not refactor regressions:

- Native `core/imports.wast`: assertions 74, 76, and 78 unexpectedly instantiate
  a module; 141/144 pass.
- Browser `core/imports.wast` (seven failures),
  `core/memory64/memory64-imports.wast` (two), and
  `core/multi-memory/imports2.wast` (one) fail import assertions. Native/browser
  failure sets differ; do not normalize that discrepancy away.
- All fourteen libc entries reject stale packaged `sourceBytes` lengths before
  execution. Source IDs match, but the previously staged payload describes
  older generated fixtures. The inventory explicitly records all fourteen
  mismatches. This result is not evidence of fourteen guest-libc semantic bugs.

This baseline intentionally records the staging state as found. Refresh the
payload through the existing generator before claiming current libc execution
coverage, saving the old baseline and recording a new measurement separately.
Known import failures need separate triage; a path-only stage must introduce
no new failures or silently remove failing inputs.

All measurements here use native processes or Node VM workers, not a real
browser. Actual `file://` boot/decompression, DOM, pixel rendering, resize/zoom,
and picker/download interactions were not newly verified in Stage 1. The prior
manual glyph-rendering observation does not establish those other boundaries.
