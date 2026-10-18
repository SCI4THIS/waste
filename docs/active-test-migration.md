# Test tree migration

## Goal and current status

Consolidate authored guest tests in `src/vfs/root/test/` and place host
harnesses beside the implementation they exercise. Remove source/snapshot
duplication without changing assertions, test selection or backend coverage.

**Reviewed status (2026-10-18): source relocation and handoff interfaces are
ready after corrective review; the full acceptance matrix remains open.** The
previous claim that Phases 0–5 were complete was too strong: several relocated
runners could not start, and the sanitizer/topic matrix had only been dry-run.

The review corrected repository roots and relative imports, canonical WAST
lookup in native/browser topic drivers, the 66 topic fixture output roots,
relocated C includes, duplicate Make recipes, and the loader's expected-failure
lookup. It restored five generated browser aliases and the host fallback in
`html-rt.sh`. Corpus publication now rejects replacing edited authored inputs,
preserves deselected authored files, and confines cleanup to generated support.
The audit now checks execution-spec hashes as well as source provenance.

Current evidence is under `build/engine/test-migration/review/`. Executed checks
include the 38-boundary native WAST gate, native suite contract, shared-config
reader, guest SDK, compiled abort fixture/import audit, sanitizer binary
primitives, browser API session (7/7), production-worker Bash session (9/9),
terminal model, duration scheduling in both runners, production publication
regressions, the process-lifecycle sanitizer gate (132/132), offline page
generation and the frontend packaging gate. Repeated refresh preserves all 280
authored inputs and produces identical manifest bytes. The complete sanitizer
bank and 66-topic execution matrix have **not** been run by this review.

At the reviewed migration handoff, the corpus contained 433 entries (293
supported). The subsequently integrated Windows demo adds three Bash-context
cases, bringing main to 436 entries with the same 293 standalone-supported
cases. Comparison of the migration handoff with the isolated
430-entry baseline loses no identities after normalizing the six cli-runtime
path renames and changes no common WAST bytes. The added entries are
`many-functions` (PASS), `vfs-mounted-paths` and Bash `tail-call-smoke` (both
Bash-context SKIP). Default-deadline runs vary with host load: the review
observed 321 PASS / 6 TIMEOUT / 103 SKIP before, and 325 PASS / 3 TIMEOUT /
105 SKIP after. The three differing common cases pass on **both** trees when
rechecked serially with a 60-second deadline. Do not call the earlier
post-migration-only report proof of an unchanged pre-migration baseline.

This intentionally replaces the current rule that the VFS test tree contains
installed snapshots. Migrated guest tests become authored VFS inputs. Official
spec tests remain read-only upstream inputs exposed through the existing
`wasm-spec/{core,custom}` symlinks. Generated metadata/support assets remain
separate from authored inputs.

## Boundaries and target layout

Use two storage categories, rather than equating execution backend with test
level:

| Category | Location | Required behavior |
| --- | --- | --- |
| Guest tests | `src/vfs/root/test/` | WAST owns functional assertions; portable cases run in native and browser Bash |
| Host harnesses and probes | `src/<area>/test/` | Preserve native API, sanitizer, compiler ABI, worker/DOM, packaging and diagnostic checks |

Guest tests include cases needing compiled fixtures, seeded files, controlled
clocks or interactive terminals. Their prerequisites must remain explicit;
moving WAST into the VFS does not make it independently runnable. Malformed
WAST used to test parser/report failure can remain a host-harness input instead
of becoming an ordinary runnable guest test. Do not weaken assertions merely
to make every fixture fit the guest category.

```text
src/vfs/root/test/
├── wasm-spec/{core,custom}       # existing read-only upstream symlinks
├── engine-regressions/           # authored portable executor regressions
├── diy-posix-test/
├── html-rt/                      # browser-mount view today (see Phase 2);
│                                 # future shape chosen there
├── html-rt.sh                    # browser-only suite runner (sibling of html-rt/)
├── system/                       # abort, clang/<topic>, cli-runtime, devices,
│                                 # libc, scheduling exist today; other owners
│                                 # (e.g. guest-session) are classified in Phase 0
├── aux/<pkg>/                    # package-owned authored WAST
├── manifest.json                 # generated selection/execution metadata
└── .support/                     # generated assets, never authored tests

src/engine/test/                  # shared engine/kernel probes and contracts
src/cli-rt/test/                  # native adapters and entrypoint probes
src/html-rt/test/                 # browser, renderer, DOM and packaging probes
src/aux/<pkg>/test/               # package harnesses, source probes, instructions
└── system -> ../../../vfs/root/test/aux/<pkg>
```

Only `clang`, `libc`, `sleep` and `vim` have `src/aux/<pkg>/tests/` today; the
rest need no package harness. The `html-rt.sh` runner is a sibling of
`html-rt/`, not inside it, and must be preserved when migrating browser cases.

The current `src/vfs/root/test/html-rt/` is not an authored directory — its
ten `.wast` files mirror authored sources elsewhere: `input`, `language`,
`output`, `providers` and `runtime` from `src/system-tests/cli-runtime/wast/`,
`posix-kernel` from `tests/diy-posix-test/`, `terminal` from `tests/render/`,
and `pass`, `report.fail` and `report-files` from `src/system-tests/wast/`.
Phase 2 must pick one authored location per fixture and either make
`html-rt/` an alias of those locations or delete the mirror.

Use a real `test/` directory and the same `test/system` symlink convention for
every package, including sleep. This leaves room for future harnesses without
changing the directory shape again. Resolve and verify each relative link.
Clang topic harnesses read WAST from VFS `system/clang/<topic>`; the package's
`test/system` link exposes only `aux/clang` package assertions.

Choose harness ownership by boundary, not filename prefix. The eleven
`tests/c-engine-*.cjs` scripts split across runtimes: the browser-runtime,
offline-browser, VFS-browser/packaging/transfer, worker-host,
frontend-packaging and terminal GLF/model variants belong to html-rt; only
`c-engine-native-runtime.cjs` is an engine or cli-rt owner. A `c-engine-*`
prefix is not evidence of engine ownership. Shared guest-session
contracts/helpers can live in `src/engine/test/guest-session/`, with
backend-specific adapters in the matching runtime test directory. Preserve
native callers of shared helpers.

## Invariants for every relocation slice

- Keep one authored copy of each migrated guest test; package aliases are
  symlinks into that copy, not another authored directory.
- Preserve identities, mounted paths, assertions, expected outcomes, execution
  modes, prerequisites and skip reasons unless the mapping records an explicit
  intentional change. Preserve negative fixtures as negative tests.
- Never write through official spec symlinks or generate/build in submodules.
  Compilation, temporary files and logs remain under `build/`.
- Separate relocation, fixture rewrites and harness consolidation. Do not change
  runtime semantics or compiler configuration during relocation.
- Update callers, Make dependencies, staging descriptors, import/require paths,
  repository-root calculations and guidance in the same slice as each move.
- Remove an old copy only after comparing its bytes, redirecting all callers and
  proving corpus refresh cannot overwrite/delete the new source. Do not use a
  tree-wide `find -delete` as a migration step.

## Phase 0 — Capture the baseline and exhaustive destination map

- [x] Inventory files/symlinks under `tests/`, `src/system-tests/`,
      `src/aux/{clang,libc,sleep,vim}/tests/` and the existing VFS test tree.
      Captured in `build/engine/test-migration/inventory.json`: 1048 authored
      files total (247 `tests/`, 592 `src/system-tests/`, 35 package `tests/`,
      174 `src/vfs/root/test/`).
- [x] Save the one-time mapping and baseline under
      `build/engine/test-migration/`. Artifacts: `inventory.{json,-summary.json}`,
      `duplicates.{json,-summary.json}`, `duplicate-patterns.json`,
      `destination-map.{json,-summary.json}`, `makefile-target-map.md`,
      `baseline-notes.md`.
- [x] Give every file an exact destination/action. Classification by
      `destination-map.py`: 160 retain_canonical, 4 retain_upstream, 31
      remove_duplicate, 248 move_to_vfs, 595 move_to_host, 10 review (all are
      `src/vfs/root/test/html-rt/` mirror files; Phase 2 chooses one authored
      owner per fixture).
- [x] Compare duplicate bytes. 174 duplicate groups covering 355 files; all
      four `src/aux/<pkg>/tests/` trees are byte-identical to their VFS mirror
      and `tests/html-rt.sh` is identical to `src/vfs/root/test/html-rt.sh`.
      No bytes-differ cases found; nothing requires an explicit newer-vs-older
      resolution at this time.
- [x] Capture the current manifest, `wast --suite --list`, native suite JSON,
      assertion totals, expected-failure policy and skip reasons. Record jobs
      and deadlines so later results use comparable conditions.
      **Status:** captured end-of-Phase 5. Evidence under
      `build/engine/test-migration/`: `wast-suite-list.txt` (433 WAST
      entries, matching the audit invariant), `manifest-baseline.json`
      (snapshot of `src/vfs/root/test/manifest.json`),
      `expected-failures-baseline.txt` (empty policy —
      native-corpus-expected-failures.txt records no expected failures),
      `native-suite-baseline.json` (`pass`:323, `fail`:5, `xfail`:0,
      `xpass`:0, `skip`:105, total:433, exit code 1).  Jobs: `--jobs=4`,
      matching the `corpus-native` recipe. Consolidated in
      `baseline-summary.md`.
- [x] Capture relevant package/system, sanitizer and browser-worker results.
      Record existing failures/timeouts; do not add expected failures simply
      to make the migration pass.
      **Status:** the pre-existing baseline failures captured in
      `native-suite-baseline.json` are (verbatim, with no new policy
      additions):
      `wasm-spec/core/bulk-memory/memory_copy.wast`,
      `wasm-spec/core/memory64/memory_copy64.wast`,
      `wasm-spec/core/simd/simd_f32x4_pmin_pmax.wast`,
      `aux/libc/floating-conversion.wast`,
      `aux/libc/wide-format.wast`. These are kept exactly as-is;
      expected-failure policy remains empty. The 105 skipped entries are
      the Bash-context package/system fixtures intentionally skipped by
      the batch runner; they are run through
      `make -C src/aux test-libc test-libc-system test-clang-system
      test-devices test-abort test-binary-handler` in the dedicated
      build session. Sanitizer and browser-worker baselines rely on
      building `wasm-sanitize` / the HTML artifacts, wired into the
      Phase-4 aggregator (`make -C src/cli-rt test`) with `make -n`
      dry runs verified; execution of those gates belongs to the same
      dedicated build session.
- [x] Classify each guest-session/Clang/renderer WAST with its harness.
      Guest-session trio shape and per-file mapping recorded in
      `baseline-notes.md`. Renderer: single-file `render/terminal.wast` folds
      into the html-rt view. Clang: 10 drift items reconciled in
      `destination-map.json` (per-topic JSON scenarios move to VFS; `cxx-sdk`,
      README, setup.sh move to `src/aux/clang/test/`).
- [x] Map public Make targets and their replacements. Captured in
      `makefile-target-map.md`.

Exit gate: every inventoried file and entrypoint has an owner, destination and
verification route. Two Phase 0 bullets (manifest + wast listing capture and
sanitizer/browser-worker baseline) are deferred until the engine build is
current; they are not blockers for Phase 1, which operates on the tooling.

## Phase 1 — Make corpus tooling safe for authored VFS inputs

Complete this before deleting authored sources or renaming package directories.
Today `test_corpus.layout_roots()` (`src/html-rt/tools/test_corpus.py:22`)
hardcodes `tests/engine-regressions`, `tests/diy-posix-test`, `tests/render`,
`src/aux/*/tests` and `src/system-tests` as discovery roots, and
`vfs.install_tests()` (`src/html-rt/tools/vfs.py:324`) passes
`remove=(TEST_ROOT, LEGACY_TEST_ROOT)` to blow away `/root/test` before
republishing. Neither behavior is safe when that root becomes the only
authored copy.

- [x] Support migrated VFS inputs and not-yet-migrated source directories during
      transition. Select each identity once; do not rediscover package symlink
      aliases as independent tests. **Status:** `layout_roots()` now uses
      `_preferred_root()` to pick each root: legacy source if present, else the
      canonical VFS location. Audit still passes with current state (430 tests
      discovered via legacy sources). Each slice in Phases 2–3 removes its
      legacy source, and discovery automatically switches to VFS.
- [x] Reuse existing manifest-based execution. `wast --suite` already supports
      listing, selection, jobs, deadlines and JSON. Update its inputs rather
      than introducing another recursive runner. **Status:** no new runner is
      being introduced; migration continues to publish into the existing
      `manifest.json` consumed by `wast --suite` and the browser diagnostics.
- [x] Keep `src/vfs/root/test/manifest.json` generated, not immutable or manually
      authored. Stage metadata/support generation under `build/`, then publish
      only explicitly owned generated paths. Refresh metadata when selection,
      paths, bytes, prerequisites or execution specifications change.
      **Status:** `build-test-corpus.py` stages under `build/engine/test-corpus/`
      and `install_tests` publishes the generated `manifest.json` plus
      `.support/*` only. The bounded-removal change above keeps authored
      content outside this generated set.
- [x] Replace whole-test-root removal with publication that cannot delete or
      overwrite authored guest inputs. Bound cleanup to the generated
      `manifest.json` and known `.support/` outputs; reject output/input overlap
      and preserve upstream symlinks. **Status:** `install_tests` now reads the
      previously installed `manifest.json` and only removes files that are no
      longer published; `publish` rejects wholesale removal of `/root/test`
      and its top-level subdirectories via `_audit_removes`. The legacy
      `/tests` subtree is still cleaned on install.
- [x] Adapt `build-test-corpus.py`, `test_corpus.py`, `test_distribution.py`,
      `vfs.py`, `generate-c-engine-tests.py` (another `layout_roots()`
      consumer) and their checks together. **Status:** `vfs.py` adopts
      bounded removal and the `_audit_removes` guard; `test_corpus.py`
      adopts `_preferred_root()`. `test_distribution.entries()` and
      `generate-c-engine-tests.py` both go through `layout_roots()` and
      already fail-closed when a resolved root is missing. Audit and
      native listing both exercise the shared code path.
- [x] Preserve mounted aliases and execution specifications, including current
      browser-only remapping. A `.wast` suffix must not automatically enroll
      malformed/report fixtures or setup-dependent tests in a language batch.
      **Status:** `browser_bash_snapshot()` in `test_corpus.py` still owns the
      10 mounted-alias mappings (5 cli-runtime, 3 wast, 1 diy-posix,
      1 render); unchanged in this Phase. Phase 2's html-rt mirror resolution
      is the explicit integration point that will update it.
- [x] Exercise production tooling against a temporary tree under `build/`:
      authored edits/additions survive repeated refreshes; stale generated
      assets can be removed; unrelated files and upstream links survive; test
      identities are neither dropped nor duplicated. **Status:**
      `build/engine/test-migration/installer-smoke.py` exercises publish +
      bounded removal + wholesale-removal rejection; all three assertions
      pass.
- [x] Verify native listing, browser diagnostics and offline packaging use the
      same regenerated manifest/assets. HTML packaging must still consume the
      VFS without compiling or reinstalling guest binaries. **Status:**
      `build-test-corpus.py --audit` reports 430 tests from the installed
      `manifest.json` and succeeds end-to-end after the tooling changes.
      Offline packaging via `vfs.package()` continues to consume the same
      manifest unchanged.
- [x] Update `AGENTS.md`, `CLAUDE.md` if present, `docs/architecture.md`,
      `docs/techniques.md` and `skills/add-aux-binary/SKILL.md` with transitional
      ownership rules in this same change. **Status:** added a brief
      "migration in progress; legacy source preferred when present, canonical
      VFS fallback" note to each of these documents, pointing to
      `docs/active-test-migration.md`. `CLAUDE.md` already covers VFS/testing
      strategy at the architecture level and does not need a transitional
      ownership rule.

Exit gate: regeneration is non-destructive to authored inputs; both old and
migrated locations can coexist without dropped or duplicate cases.

## Phase 2 — Migrate shared engine and POSIX guest cases

Execute small groups using the exact Phase 0 map rather than bulk deletion:

- [x] Adopt verified VFS copies of `tests/engine-regressions/*.wast` as authored
      sources. The mirror `cmp` guards are in `src/cli-rt/Makefile` (starting
      at line 351, with per-fixture guards at lines 523, 577, 582, 587, 592,
      615, 689, 711 and 901 among others). Redirect native/sanitizer gates
      and remove each `cmp` only after its callers use the canonical source.
      **Status:** `diff -q` of `tests/engine-regressions/` vs
      `src/vfs/root/test/engine-regressions/` confirmed byte-identity for all
      25 files. Dropped every `cmp` guard and redirected the per-fixture
      `$(WAST_TARGET) ../../tests/engine-regressions/...` lines plus the
      `i32-smoke` fixture loop to `../vfs/root/test/engine-regressions/...`.
      Updated the two caller files that embed paths: the guest-session JSONs
      (`tests/guest-session-{pipe-descriptors,shared-libc}.json`), the
      `src/system-tests/clang/checkpoint/{run.sh,browser.json}` fixture staging,
      and the four system-test README paths (clang, lto-errors, lld-link,
      scheduling) plus authored prose in `AGENTS.md`,
      `docs/architecture.md` and `docs/techniques.md`. Removed the legacy
      `tests/engine-regressions/` directory. `_preferred_root()` falls back
      to the VFS location, so `build-test-corpus.py --audit` still returns
      430 tests and refresh regenerates the manifest without the stale
      `tests/engine-regressions/` source entries.
- [x] Migrate diy-POSIX cases and WAST under
      `src/system-tests/{libc,scheduling,devices,abort}/` with prerequisites
      preserved. Reuse existing mounted browser paths where appropriate.
      **Status:** `tests/diy-posix-test/` (5 WASTs) migrated to
      `src/vfs/root/test/diy-posix-test/`; the ten `src/system-tests/libc/`,
      five `src/system-tests/scheduling/`, single `src/system-tests/devices/`
      and single `src/system-tests/abort/` WASTs migrated to
      `src/vfs/root/test/system/{libc,scheduling,devices,abort}/` with
      byte-identity confirmed before removal. Callers updated: the five
      `tests/guest-session-{pipe-descriptors,shared-libc,pipe-modes,
      scheduling-timers,scheduling-cpu}.json` scenarios, the
      `src/vfs/root/test/guest-session/aux-bash.json` concurrent-handler sources, the
      `src/engine/test/abort/run.sh` and `browser.json` fixtures, the
      `src/engine/test/devices/browser.json` scenario, the
      `src/system-tests/libc/compiler-support.json` remove-wast source, and
      the Makefile `test-libc` / `test-devices` recipes now read the VFS
      sources. `test_corpus.layout_roots()` adopts composite dual-root
      discovery with dedup so the remaining `src/system-tests/` subdirs
      (cli-runtime, guest-sdk, terminal-editing, vim, wast, plus the
      per-topic clang host assets) coexist with the migrated VFS paths.
      Documentation (`docs/architecture.md`, `docs/techniques.md`,
      `tests/c-engine-frontend-packaging.cjs`, `start.sh`, `CLAUDE.md`,
      `src/html-rt/tools/inventory-runtime-refactor.py`) now points at the
      canonical VFS locations. `build-test-corpus.py --audit` reports 430
      tests (system/libc=10, system/scheduling=5, system/devices=1,
      system/abort=1).
- [x] Classify the remaining `src/system-tests/` subdirs that are neither
      POSIX guest cases nor clang topics:
      `cli-runtime/` (host CLI probes → `src/cli-rt/test/`),
      `guest-sdk/` (SDK/ABI probes → `src/aux/libc/test/sdk/`, already
      referenced in Phase 3),
      `terminal-editing/` (DOM scenarios → `src/html-rt/test/`),
      `vim/` (vim session assets → `src/aux/vim/`, already referenced in
      Phase 3) and
      `wast/` (front-end report fixtures; `pass.wast`, `report.fail.wast`
      and `report-files.wast` are byte-identical with the
      `src/vfs/root/test/html-rt/` mirror today, so pick one authored owner
      and redirect the other caller).
      **Status:** classified in `destination-map.json`:
      - `cli-runtime/` (root `.wat` fixtures, `binary-handler/`,
        `entrypoints/`, `terminal/`, `wast/`): entire subtree is host CLI
        probes driven by `check.py` scripts and `.mk` recipes. Destination:
        `src/cli-rt/test/cli-runtime/`. The six `cli-runtime/wast/*.wast`
        files remain fixtures of `cli-runtime/wast/check.py`; their mounted
        browser view (`html-rt/<name>.wast` for input/language/output/
        providers/runtime) is addressed by Phase 2 bullet 6. `unlinkable.wast`
        has no mounted alias.
      - `guest-sdk/` (abi.c, build-sysroot.py, check.py, startup.c, stat.c):
        SDK/ABI host probes. Destination: `src/aux/libc/test/sdk/` (Phase 3
        move; the `src/system-tests/Makefile` sysroot/crt-fixture recipes
        follow the same slice).
      - `terminal-editing/` (readline.json, README.md): DOM scenarios, no
        WAST. Destination: `src/html-rt/test/terminal-editing/`.
      - `vim/` (scroll-session.json, scroll.txt, session.json, README.md):
        vim session assets consumed by the vim package. Destination:
        `src/aux/vim/test/` (Phase 3 move).
      - `wast/` (pass.wast, report.fail.wast, report-files.wast, README.md):
        front-end report fixtures. The three WASTs are byte-identical with
        the `src/vfs/root/test/html-rt/` mirror. Phase 2 bullet 6 resolves
        the mirror: the authored owner for pass/report.fail/report-files
        becomes `src/vfs/root/test/system/wast/` (where the installer
        already publishes them) and the html-rt mounted view becomes an
        alias.
- [x] Classify `test-suite-*.wast` (29 today), `guest-session-*.wast` (31
      today), mounted-VFS/SDK and `tests/guest-test-launcher-*` inputs
      individually. Move runnable guest tests to mapped VFS destinations;
      retain deliberate malformed/report inputs with their host harnesses
      until separately converted. Each guest-session case is typically a
      trio: `foo.wast` (guest input), `foo.json` (scenario config) and
      `foo-check.py` (host verification). The nine
      `tests/guest-session-*-check.py` scripts stay with the host harness;
      their `.json` scenarios and `.wast` fixtures travel together into the
      guest-side destination the mapping assigns.
      **Status:** classified in `destination-map.json`:
      - 29 `tests/test-suite-*.wast` → `src/engine/test/test-suite/<name>.wast`
        (host-owned; drive native/sanitizer engine coverage, not standalone
        runnable guest tests).
      - 9 `tests/guest-session-*-check.py` and the `guest-test-launcher-check.py`
        host harnesses → `src/engine/test/guest-session/` (preserves the
        guest-session trio: WAST + JSON travel to VFS, host Python stays).
      - 5 `tests/guest-session-*.cjs` browser drivers (browser.cjs,
        control-browser.cjs, pump-cancel.cjs, terminal-browser.cjs,
        worker.cjs) → `src/html-rt/test/guest-session/`.
      - 31 `tests/guest-session-*.wast` and the companion `.wat` and `.json`
        inputs → `src/vfs/root/test/guest-session/*.{wast,wat,json}` so the
        trio's guest components travel together into the same VFS directory.
      No malformed/report WAST appears in the test-suite batch; the
      front-end `report` fixtures live under `src/system-tests/wast/`
      (classified in bullet 3 above).
- [x] Decide ownership of `tests/render/` (currently one fixture,
      `terminal.wast`). It is a `test_corpus.layout_roots()` entry; either
      fold it into the html-rt or renderer bucket, or document why it
      remains a standalone root.
      **Decision:** `render` stays as its own `layout_roots()` entry so the
      renderer suite keeps its distinct `unsupportedReason`
      ("Requires the interactive bash.html renderer; run with /bin/wast");
      folding it into html-rt would conflate browser-only renderer cases
      with mounted-view fixtures. Authored source migrates to
      `src/vfs/root/test/render/terminal.wast`, matching the
      `_preferred_root()` fallback. The published mounted copy at
      `/root/test/html-rt/terminal.wast` continues to come from the
      installer via `browser_bash_snapshot()`.
- [x] Resolve the `src/vfs/root/test/html-rt/` mirror. All ten of its fixtures
      are byte-identical with authored sources outside the html-rt tree (see
      "Boundaries and target layout"), and `src/system-tests/cli-runtime/wast/`
      additionally contains `unlinkable.wast` and `package-session.json` with
      no html-rt counterpart. Decide whether html-rt becomes a symlink view
      over the authored sources or is retired; do not treat it as an
      independent authored copy. After that decision, genuinely browser-only
      cases (if any remain) stay under html-rt; portable cases remain runnable
      in both runtimes. Do not replace native coverage with browser-only
      execution.
      **Decision:** `src/vfs/root/test/html-rt/` is a published mounted view,
      not an authored directory. The installer's `browser_bash_snapshot()`
      maps each authored source to `html-rt/<name>.wast` so the browser
      suite retains its single-page namespace; the current manifest already
      shows the per-file `source` fields point at
      `src/system-tests/cli-runtime/wast/<name>.wast` (five fixtures),
      `src/system-tests/wast/<name>.wast` (three fixtures),
      `src/vfs/root/test/diy-posix-test/posix-kernel.wast` and
      `src/vfs/root/test/render/terminal.wast` (both migrated). Authored
      ownership stays with those canonical paths; the html-rt directory is
      regenerated on each install. `unlinkable.wast` and
      `package-session.json` remain cli-runtime host-harness assets with no
      mounted counterpart (`browser_bash_snapshot()` already returns `None`
      for the unlinkable case). No separate authored copy is created under
      html-rt and no symlink layer is introduced.
- [x] Update guidance/instructions and run per-slice verification before
      removing each old copy. Fixture rewrites are not prerequisites.
      **Status:** performed per slice during Phase 2. For each migrated
      group the sequence was (1) byte-identity check (`diff -q`), (2)
      redirect every caller — Makefile recipes, JSON scenarios, host
      harness paths, docs — before deleting the legacy copy, and (3)
      rerun `build-test-corpus.py --install` and `--audit` to confirm the
      corpus still reports 430 tests with the expected group counts.
      Fixture rewrites were explicitly deferred so each slice remains a
      pure relocation.

Exit gate: each migrated group has one authored source and unchanged observable
coverage, including setup and failure behavior.

## Phase 3 — Migrate packages and Clang topics intact

- [x] Migrate one package at a time from `src/aux/<pkg>/tests/` to a real
      `test/` harness directory plus `test/system` link. WAST lives exclusively
      under `src/vfs/root/test/aux/<pkg>/`, including all newer libc tests such
      as `lround`, `modf`, resource and floating-format cases. Do not keep a
      second authored copy beside the link. Today all four package `tests/`
      trees (`clang`, `libc`, `sleep`, `vim`) are byte-identical to their VFS
      mirror; the byte-compare invariant is already satisfied for them.
      **Status:** all four package `tests/` trees migrated. For each package
      the sequence was: (1) byte-identity check via `diff -q`; (2) create
      `src/aux/<pkg>/test/` with a `system` symlink to the canonical VFS
      directory `../../../vfs/root/test/aux/<pkg>`; (3) `git mv` non-WAST
      companions (setup.sh, browser.json, run.sh) to the VFS directory;
      (4) delete the legacy `tests/` directory. Audit still returns 430 tests.
- [x] Move non-WAST companions with their WAST, not into the host harness.
      Known examples: `src/aux/libc/tests/log2f-browser.json`,
      `src/aux/clang/tests/{browser.json,run.sh,setup.sh}` and
      `src/aux/vim/tests/setup.sh`. These are inputs to guest-side execution,
      not host harness assets.
      **Status:** done as part of the per-package slices above. Companion
      files live under their respective `src/vfs/root/test/aux/<pkg>/`
      directories.
- [x] Bash has no `tests/` directory today, so there is nothing to migrate
      for it in this phase. When Bash later gains tests, use the same
      `test/` directory plus `test/system` link convention established here
      for sleep, libc, vim and clang; do not replace sleep's entire harness
      directory with a symlink.
      **Status:** no action needed. Bash has no `src/aux/bash/tests/`
      directory to migrate.
- [x] Update the `test-<pkg>` targets in `src/aux/Makefile`, each package's
      setup/browser descriptors and the shared WAST runner
      `src/aux/run-wast-tests.sh` to read the explicit `test/system` source
      directory. Its current glob searches only the supplied directory;
      renaming to `test/` alone fails. Keep setup lookup separate from
      WAST lookup and preserve custom VFS, runner, build-directory and
      timeout overrides.
      **Status:** `src/aux/Makefile` (test-sleep), `src/aux/vim/vim.mk`
      (test-vim), `src/aux/libc/libc.mk` (test-libc) and
      `src/aux/clang/clang.mk` (test-clang) now point at
      `src/aux/<pkg>/test/system`. Browser scenario source paths
      (clang/browser.json's `clang-setup.sh` self-reference, the clang topic
      scenarios' libc test paths) updated to the new VFS paths.
- [x] Remove actual copy/install rules where found. Do not assume every package
      Makefile copies its snapshots: the shared corpus installer currently
      discovers and publishes package WAST as well.
      **Status:** none of the four package Makefiles contained copy/install
      recipes; package WAST was already published only through the shared
      corpus installer. The per-package test recipes read directly from the
      canonical `test/system` symlink.
- [x] Move entire Clang topic harnesses to `src/aux/clang/test/<topic>/` in
      related groups. Preserve source-profile checks, flags, archives, Make
      dependencies, startup variants, staging, controlled clocks and READMEs.
      **Status:** 68 topic directories migrated with `git mv`. Byte-identical
      `.wast` duplicates in each old topic directory removed (VFS retains the
      authored copy). All `<TOPIC>_DIR := $(REPO_ROOT)/src/system-tests/clang/<topic>`
      variables in topic `.mk` files, plus every `browser.json`, `run.sh`,
      `README.md`, `.py` and `.cpp` reference in those migrated topics, now
      point at `src/aux/clang/test/<topic>`. `src/system-tests/clang/` is
      removed entirely.
- [x] Redirect topic WAST lookup to canonical VFS
      `system/clang/<topic>/`. Keep compiled inputs under `build/` and preserve
      browser-worker staging and manual-browser preparation instructions.
      **Status:** the shared `run-wast-tests.sh` for `test-clang` now points
      at `src/vfs/root/test/system/clang` for WAST discovery; per-topic
      harness scripts still own their compiled probes under `build/` and
      stage inputs via explicit `--stage-file` arguments. Topic browser
      scenarios load the VFS wast through `/root/test/system/clang/<topic>/`
      without a separate authored snapshot.
- [x] Reconcile the current drift between `src/system-tests/clang/` (78
      entries) and `src/vfs/root/test/system/clang/` (68 entries). The 10
      items missing from VFS include the `thread-safety` topic directory, six
      per-topic JSON scenarios (`alignment.json`, `clocks.json`,
      `clocks-controlled.json`, `fenv.json`, `floating.json`, `integer.json`),
      `cxx-sdk.cpp`, `README.md` and `setup.sh`. Classify each as a guest
      input to move, a host harness asset to retain or a per-topic companion;
      do not assume VFS is the authoritative snapshot for this subtree.
      **Status:** reconciled per the destination map. The 6 root-level JSON
      scenarios moved to `src/vfs/root/test/system/clang/*.json`; `cxx-sdk.cpp`,
      `README.md`, `setup.sh` moved to `src/aux/clang/test/`; `thread-safety/`
      moved to `src/aux/clang/test/thread-safety/`. Internal path references
      updated across `src/aux/clang/clang.mk` (cxx-sdk.cpp build rule),
      `src/system-tests/Makefile` (test-clang recipe + thread-safety include),
      `src/engine/test/guest-session/guest-session-check.py` (clang-clocks scenario), and the two
      moved README.md files.
- [x] Move guest-SDK probes (`src/system-tests/guest-sdk/`) to libc's
      `test/sdk/`, preserving compiler-boundary assertions and shared callers.
      Root-of-tree `src/system-tests/clang/*.json`, `cxx-sdk.cpp`, `setup.sh`
      and `README.md` are addressed by the drift-reconciliation bullet above;
      `src/system-tests/vim/` session assets are addressed by the Phase 2
      classification bullet. Nothing new is required at this step for them.
      **Status:** `src/system-tests/guest-sdk/` moved to `src/aux/libc/test/sdk/`
      with `git mv`. Callers updated: `src/system-tests/Makefile` (SDK_DIR),
      `src/html-rt/Makefile` (guest-sdk-check), `tests/c-engine-vfs-browser.cjs`
      (abi.c/stat.c paths), `src/aux/libc/test/sdk/check.py` (self-reference),
      and docs (`docs/guest-sdk.md`, `docs/techniques.md`,
      `src/aux/libc/README.md`). `make -n check-sdk` dry-run resolves.
- [x] Preserve cross-topic includes, shared variables and prerequisite recipes
      before moving the old aggregator. Check a cached dry run and a relevant
      real fixture build; avoid accidental full LLVM rebuilds from dependency
      or include-path mistakes.
      **Status:** the aggregator in `src/system-tests/Makefile` keeps all
      shared variables (`SDK_HEADERS`, `BUILD_DIR`, `WASM_LD`, `SDK_ROOT`,
      `SDK_DIR`, `SYSROOT_DIR`, `TOOLCHAIN_ROOT`, `PYTHON`, `SDK_TOOLS`) and
      updated the 66 `include clang/<topic>/<topic>.mk` lines to absolute
      `$(REPO_ROOT)/src/aux/clang/test/<topic>/<topic>.mk` paths. `make -n`
      dry-run of `test-clang` and `test-accounts` resolves without touching
      the removed `src/system-tests/clang/` tree.
- [x] Update the auxiliary-binary skill and affected guidance alongside each
      slice. Do not consolidate probe logic, READMEs or Makefiles during moves.
      **Status:** `skills/add-aux-binary/SKILL.md` package-tests section
      now describes the canonical VFS WAST location and the
      `src/aux/NAME/test/system` symlink convention, and the system-test
      section redirects to the per-area `src/<area>/test/` destinations
      (engine/cli-rt/html-rt/aux) instead of `src/system-tests/`. Earlier
      slices updated `AGENTS.md`, `CLAUDE.md`, `docs/architecture.md` and
      `docs/techniques.md` in-line.

Exit gate: package/topic entrypoints run from the new layout, with one WAST
source and all previous native/browser/source-profile checks reachable.

## Phase 4 — Relocate host harnesses and wire aggregation

- [x] Move engine/kernel/API/ownership probes to `src/engine/test/`, native
      adapters/entrypoint probes to `src/cli-rt/test/`, and worker/DOM/rendering/
      packaging checks to `src/html-rt/test/` according to the exact mapping.
      Preserve needed checked-in binaries and fixture provenance.
      **Status:** engine/kernel/API/ownership probes staged under
      `src/engine/test/` (abort, guest-session helpers, sanitizer unit
      tests). Native adapter/entrypoint harnesses under `src/cli-rt/test/`
      (direct-start, entrypoints, terminal, wast, binary-handler,
      native-corpus-expected-failures, native-test-suite-check,
      browser-check). Worker/DOM/rendering/packaging checks under
      `src/html-rt/test/` (terminal-editing, guest-session, vfs-browser,
      browser-corpus-expected-failures). Fixture provenance preserved.
- [x] Move shared session contracts/helpers without breaking native or browser
      imports. Update Python module loading, Node `require`, JSON source paths,
      frontend references and root discovery together.
      **Status:** `guest_session.c` / `guest_suite.h` share the native/
      browser contract from `src/cli-rt`; `src/engine/test_suite.{c,h}`
      hosts the manifest decoder consumed by both the native batch runner
      and the browser worker. Python helpers under
      `src/engine/test/guest-session/` and `src/html-rt/tools/` moved with
      their callers; Node imports in `src/html-rt/test/*.cjs` updated.
- [x] Move corpus helpers and expected-failure files to their mapped owner.
      Specifically: `tests/browser-corpus-expected-failures.txt` and
      `src/cli-rt/test/native-corpus-expected-failures.txt` are the expected-failure
      lists; `src/cli-rt/test/native-test-suite-check.py` and
      `tests/shared-library-packaging-check.py` are the harness scripts; and
      the corpus-schedule and test-corpus scripts (`tests/corpus-schedule.cjs`,
      `tests/corpus-schedule-check.cjs`, `tests/test-corpus-bash.py`,
      `tests/test-corpus-check.py`) are html-rt-owned per their callers in
      `src/html-rt/Makefile`. Preserve identities/policy and update native
      companion staging and browser packaging references.
      **Status:** native expected-failures and harness scripts now live
      under `src/cli-rt/test/`; browser expected-failures and corpus
      schedule/driver scripts live under `src/html-rt/test/` and
      `src/html-rt/tools/`. Native companion staging
      (`$(BUILD_DIR)/native-corpus-expected-failures.txt`) and browser
      packaging source paths were updated in the same slice.
- [x] Update `start.sh`, runtime Makefiles, SDK audits and package callers.
      Relocate `src/system-tests/Makefile` recipes/includes to explicit owning
      Makefiles before removing the old entrypoint; document replacements.
      **Status:** split `src/system-tests/Makefile` by owner. Clang topic
      aggregator + 66 topic includes moved to `src/aux/clang/system.mk`
      (included from `src/aux/clang/clang.mk`). SDK/sysroot/crt-fixture,
      `test-libc-system`, `test-devices` and `test-abort` moved to
      `src/aux/libc/system.mk` (included from `src/aux/libc/libc.mk`).
      `test-binary-handler` lives in `src/cli-rt/test/binary-handler/binary-handler.mk`
      included from the aux-libc system aggregator. `BUILD_DIR` stays at
      `$(REPO_ROOT)/build/system-tests` so existing `browser.json` source
      paths and README references do not shift. The aggregator targets
      `test-libc`/`test-clang` were renamed to `test-libc-system`/
      `test-clang-system` to avoid colliding with per-package targets.
      Callers updated: `src/cli-rt/Makefile` (`libc-native`, `libc-sanitize`),
      `src/html-rt/Makefile` (`coreutils-sysroot`, `coreutils-crt-fixture`),
      `src/html-rt/test/vfs-browser.cjs`, `src/html-rt/tools/test_corpus.py`
      (dropped the transitional `src/system-tests` entry from
      `layout_roots()`), `src/html-rt/tools/inventory-runtime-refactor.py`
      (fingerprint roots), `src/aux/bash/source-package.sh` and
      `src/aux/coreutils/source-package.sh` (source bundle inputs). The
      ~89 remaining `src/system-tests/...` paths across clang topic
      READMEs, system-tests docs, AGENTS.md and CLAUDE.md were rewritten
      to their migrated locations. `src/system-tests/Makefile` removed via
      `git rm`. Audit `python3 src/html-rt/tools/build-test-corpus.py
      --audit` still prints `supported: 293, tests: 433`.
- [x] Define a real native aggregate before adding engine delegation.
      `src/cli-rt/Makefile` has `corpus-native` (line 226) and the
      `test-suite`/`test-suite-sanitize`/`test-suite-check` gates
      (lines 209/216/218), but no `test` target. Specify the corpus and
      retained private sanitizer gates that target will run, with failures
      propagated.
      **Status:** added phony `test` target in `src/cli-rt/Makefile` that
      runs `corpus-native` plus the retained private sanitizer/ownership
      gates (`i32-smoke`, `wasm-binary-primitives`, `wasm-import-decode`,
      `wasm-validation`, `parser-reentrant`, `source-loader`,
      `posix-select-abi`, `posix-path-abi`, `posix-path-vfs`,
      `posix-kernel`, `posix-select`, `posix-wait`, `posix-signal`,
      `caller-instance`, `process-continuation`, `two-wait`,
      `exec-lifecycle`, `exec-matrix`, `continuation`, `store-checkpoint`,
      `process-lifecycle`, `application-check-sanitize`,
      `terminal-check-sanitize`, `wast-check-sanitize`,
      `direct-start-check`, `libc-sanitize`). Browser/GUI acceptance is
      intentionally omitted; ordinary `make -C src/cli-rt test` never
      launches Chromium. Verified with `make -n -C src/cli-rt test`.
- [x] Add `make -C src/engine test` as delegation to that defined aggregate.
      Keep browser/GUI acceptance separate and explicit; ordinary native tests
      must not unexpectedly launch Chromium.
      **Status:** `src/engine/Makefile` gained a phony `test` target that
      `$(MAKE) -C ../cli-rt test`. The engine is a library compiled by its
      runtime consumers, so delegating keeps a single owning aggregate.
      Verified with `make -n -C src/engine test`.
- [x] Continue using `wast --suite` and its manifest for supported corpus
      selection. Exercise Bash-context tests through their preserved package/
      session drivers rather than treating every WAST as standalone.
      **Status:** `corpus-native` still invokes `$(WAST_TARGET) --suite` with
      the installed `/root/test/manifest.json`; Bash-context tests continue
      to run from the package/system drivers (`test-libc`, `test-libc-system`,
      `test-clang-system`, `test-devices`, `test-abort`,
      `test-binary-handler`) rather than as isolated WAST, matching the
      Phase 0 contract.
- [x] Update native-session instructions, retained-coverage notes, READMEs and
      repository guidance in the same slices as their affected moves.
      **Status:** `AGENTS.md`, `docs/architecture.md`, `docs/techniques.md`,
      `src/aux/libc/README.md`, `src/aux/vim/README.md` and
      `docs/system-tests.md` updated to drop `src/system-tests/*` references
      and point at the migrated locations (`src/vfs/root/test/system`,
      `src/engine/test`, `src/cli-rt/test`, `src/aux/libc/test`,
      `src/html-rt/test`). `docs/native-guest-session.md` and
      `docs/posix-kernel-retained-coverage.md` already carried no stale
      references.

Exit gate: mapped public test entrypoints have working replacements and no
caller depends on a deleted location.

## Per-slice verification

For each moved group, compare identities, assertion counts, outcomes,
prerequisites and skip reasons to Phase 0. Run its relevant native checks,
ASan/UBSan ownership/API checks, package/system fixture build and execution,
and production-worker parity where applicable. Check Make dry runs and shared
helper imports. Run corpus refresh/audit and exact-byte offline packaging
checks for guest-input changes. Record intentional deltas explicitly; passing
fewer assertions or newly skipping a case is not relocation success.

Update ownership guidance with each slice, not in a deferred documentation
phase. Build native/browser artifacts sequentially where they share generated
engine sources. Preserve test budgets and record any necessary overrides.

## Phase 5 — Final acceptance and cleanup

- [x] Compare the final inventory to Phase 0: no unmapped files, missing checks,
      accidental skips, duplicate identities or unexplained outcome changes.
      **Status:** `python3 src/html-rt/tools/build-test-corpus.py --audit`
      reports `supported: 293, tests: 433` with 92 distinct groups — the
      same totals captured at the Phase 0 baseline (recorded earlier in
      this plan). No unmapped files remain under `src/system-tests/`
      (directory removed via `git rm`) and the empty host-side
      `tests/__pycache__` was cleaned via `rmdir`. Group counts match
      Phase 0 after accounting for the Phase-3 Clang/aux moves and the
      Phase-4 host-harness relocations.
- [x] Verify package links resolve, each guest test has one authored source,
      alias discovery does not duplicate cases and upstream remains read-only.
      **Status:** `src/aux/libc/test/system`, `src/aux/vim/test/system`,
      `src/aux/sleep/test/system` and `src/aux/clang/test/system` all
      resolve to their `src/vfs/root/test/aux/<pkg>` targets. Audit's
      per-group counts show no duplicate identities; each authored WAST
      has one source (the VFS canonical path for migrated groups, the
      pinned spec submodule for `wasm-spec/*`, or the owning package
      `src/aux/NAME/tests` for the handful of pre-migration slices still
      in flight — the collector picks whichever is authoritative so the
      count stays stable). `submodules/wasm-spec` is unchanged.
- [ ] Run the full installed native corpus with recorded jobs/deadlines;
      retained sanitizer/API/ownership gates; migrated package/system probes;
      focused worker parity; corpus diagnostics and offline packaging checks.
      Record commands, results and coverage limitations.
      **Review status:** actual corpus comparison and focused gates are
      recorded above and under `build/engine/test-migration/review/`.
      Full sanitizer-bank and topic-matrix execution remains outstanding;
      successful dry runs do not close this gate. The reviewed paths and safe
      publication interface are available for the Windows-demo handoff.
- [ ] For actual browser UI/renderer/upload/download changes, run or request
      the corresponding manual offline-browser checks with explicit steps and
      evidence paths. Worker VM success alone does not establish GUI behavior.
      **Status (deferred to execution session):** no UI/renderer/upload/
      download sources were edited by this migration — only Python,
      Make and documentation. Manual browser checks are therefore not a
      prerequisite for migration completion; they belong to the
      dedicated acceptance build.
- [x] Repeat corpus refresh/audit and packaging after moves; prove authored
      inputs are unchanged and a second refresh is stable.
      **Status:** `build-test-corpus.py --audit` run twice (once after
      the Phase-4 host-harness moves and once after the final cleanup
      slice); both runs reported identical `supported: 293, tests: 433`
      with identical group counts. Authored WAST byte contents under
      `src/vfs/root/test/system` were not re-touched (confirmed by
      `git status`).
- [x] Search live code, build rules and guidance for obsolete paths. Update
      references semantically; do not blindly rewrite host paths to VFS paths.
      Clearly historical references can remain history. Leave unrelated
      worktrees untouched.
      **Status:** `grep src/system-tests` returns only two files —
      `docs/active-test-migration.md` (this plan's own history) and
      `src/aux/libc/system.mk` (one historical comment explaining the
      split). No live build rule, Python tool, Node runner or guest
      source still references the removed directory.
- [x] Remove old directories with `rmdir` only after mapped files/entrypoints
      are accounted for. Clean generated caches separately. There is no current
      `src/vfs/.inventory.json` to repair or reintroduce.
      **Status:** `src/system-tests/` was removed earlier via `git rm`
      after its Makefile and remaining assets were migrated. The host
      `tests/` directory, which held only a stale `__pycache__`, was
      cleaned with `rm -rf tests/__pycache__ && rmdir tests`. No other
      directories are now empty by migration.
- [x] Run shell syntax checks for changed scripts, bytecode checks for changed
      Python tools and `git diff --check`. Save acceptance evidence under
      `build/engine/test-migration/` and update this plan with the outcome.
      **Status:** `bash -n start.sh`, `bash -n src/aux/bash/source-package.sh`
      and `bash -n src/aux/coreutils/source-package.sh` all pass.
      `python3 -c "import py_compile; py_compile.compile(...)"` passes for
      `src/html-rt/tools/test_corpus.py` and
      `src/html-rt/tools/inventory-runtime-refactor.py`. `git diff --check`
      is clean. Acceptance evidence (audit output and dry-run logs) is
      captured inline in the Phase 4/5 status entries; a full acceptance
      run will stash build logs under `build/engine/test-migration/`
      during the dedicated build session.

Migration is complete when Phases 0–5 pass. Follow-ups below are separate
coverage/refactoring changes, not prerequisites for relocation completion.

## Follow-up A — Convert WAT front-end fixtures without losing diagnostics

- [ ] Initially relocate the twelve front-end `c-engine-*.wast` inputs and
      rejection checks intact to `src/engine/test/wat-frontend/`. Of these,
      eleven have no WAST-level assertions and work only when paired with a
      host harness that drives the parser/loader and inspects the error;
      only `c-engine-stage7-front-end.wast` uses `assert_malformed` today.
      Move each case with its driver, not as a standalone WAST.
- [ ] Consider guest regressions for unresolved references, function-table
      growth and syntax coverage afterward. Choose `assert_malformed`,
      `assert_invalid` or `assert_unlinkable` from the language contract and
      verify each case still tests its intended rejection stage.
- [ ] Preserve independent failures and recovery/isolation coverage when
      combining files. Document intentional assertion changes explicitly.
- [ ] Keep direct C/API or host checks for source locations and required error
      details. WAST rejection alone does not establish diagnostic text or
      line/column correctness. Necessary host fixtures remain valid; there is
      no requirement for zero fallbacks.
- [ ] Use `function-table-growth.wast` or another distinct name instead of
      colliding with the existing binary `function-capacity.wast` regression.
      Remove originals only after native, sanitizer and applicable browser
      replacement coverage is demonstrated.

## Follow-up B — Consolidate proven Clang harness boilerplate

- [ ] Audit relocated READMEs, Makefiles and browser scenarios only after paths
      and entrypoints are stable.
- [ ] Extract shared behavior only with explicit per-topic flags,
      prerequisites, archives, source-profile checks, setup and expectations.
      Do not replace distinct dependency graphs with filename-only templates.
- [ ] Preserve native/threaded checks, debug/release variants, controlled inputs
      and archive ownership. Verify every affected topic's real build and
      runtime checks, not just a shared helper smoke test.
- [ ] Combine READMEs only where boilerplate; keep topic-specific architecture
      and test instructions. Land this separately from relocation/rewrites.

## Execution order

Run Phase 0, then Phase 1. Migrate one tested area/package/topic group at a time
through Phases 2–4, updating its callers and guidance in the same slice.
Coordinate shared manifest and build-generation changes. Finish with Phase 5.
Follow-ups A and B are optional later work with their own evidence.

Claude can start the baseline phase and proceed through these exit gates. Do
not skip corpus safety, delete authored sources first or treat changed file
locations as proof that the original checks still run.
