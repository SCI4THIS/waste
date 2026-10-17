# Test tree migration

## Goal and current status

Consolidate every `.wast`, driver, and probe currently under `tests/`,
`src/system-tests/`, and `src/aux/*/tests/` into a single two-bucket layout:

- **System tests** (run inside `bash.html` against the real browser runtime)
  live under `src/vfs/root/test/` and nowhere else.
- **Unit tests** (native drivers, harness scripts, Node/Python probes that
  cannot run inside `bash.html`) live under `src/<area>/test/`, where
  `<area>` is `engine`, `cli-rt`, `html-rt`, or `aux/<pkg>`.

Every per-package `src/aux/<pkg>/test/system` is a **symlink** into the VFS
tree, so there is only ever one copy of a system test on disk. The current
copy-plus-`cmp` mirror in `src/cli-rt/Makefile` and the `install-<pkg>` copy
steps that produce `src/vfs/root/test/aux/<pkg>/` go away entirely.

The destination directory `src/vfs/root/test/` already carries 87 files/links
from earlier moves (engine regressions, aux package tests, system tests,
diy-posix fixtures, html-rt scenarios, and the two `wasm-spec/{core,custom}`
symlinks into `submodules/wasm-spec/test/`). This plan handles the remaining
~203 files in `tests/` and ~163 files in `src/system-tests/`, plus the rename
of `src/aux/<pkg>/tests/` to `src/aux/<pkg>/test/`.

## Terminology

| Term | Meaning here | Location |
|---|---|---|
| **System test** | self-asserting `.wast` runnable in `bash.html` | `src/vfs/root/test/...` |
| **Unit test** | native/browser harness that cannot run in `bash.html` | `src/<area>/test/...` |

`src/aux/<pkg>/test/system` → `../../../vfs/root/test/aux/<pkg>` (3 `..` to
reach `src/`, direct down into VFS; no round trip through repo root).

## Target layout

```
src/vfs/root/test/                     # all system tests
├── wasm-spec/{core,custom}            → submodules/wasm-spec/test/{core,custom}   (symlinks, exist)
├── engine-regressions/
│   ├── *.wast                         # current 15 + new wat-frontend/ subdir
│   └── wat-frontend/*.wast            # consolidated from tests/c-engine-*.wast
├── diy-posix-test/*.wast              # + posix-kernel.wast
├── html-rt/
│   ├── *.wast                         # current 10
│   └── render/*.wast                  # from tests/render/
├── system/
│   ├── abort/abort.wast
│   ├── clang/<topic>/*.wast
│   ├── cli-runtime/wast/*.wast
│   ├── guest-session/*.wast           # new, from tests/guest-session-*.wast
│   ├── libc/*.wast
│   ├── scheduling/*.wast
│   ├── test-suite/*.wast              # new, from tests/test-suite-*.wast
│   └── vfs/*.wast                     # new, from tests/vfs-mounted-paths.wast
├── aux/
│   ├── bash/*.wast                    # new, from tests/tail-call-smoke.wast
│   ├── clang/installed.wast
│   ├── libc/*.wast                    # authored here (not src/aux/libc/)
│   ├── sleep/nanosleep.wast
│   └── vim/installed.wast
└── manifest.json

src/engine/test/                       # unit tests: native engine
├── Makefile                           # .PHONY: test → $(MAKE) -C ../cli-rt test
├── c-engine-*.{c,wat,cjs,so.wasm}     # ~35 harness files from tests/
├── posix-*.c, wasm-*.c                # native engine C probes
├── wast-parser-reentrant.c, source-loader.c
├── shared-library-packaging-check.py
├── abort/{probe.c, run.sh, browser.json}
├── vfs/{directory-check.py, package.cjs, tar-loader.cjs}
└── corpus/
    ├── corpus-schedule*.cjs
    ├── test-corpus-*.py
    └── {native,browser}-corpus-expected-failures.txt

src/cli-rt/test/                       # unit tests: cli-rt drivers
├── direct-start.c
├── {failed,incompatible-{entry,import,runtime},malformed,memoryless,
│    missing-{entry,library},read-signals,resources,
│    trapping-{runtime,start},yielding-start}.wat
├── browser-check.mjs
├── entrypoints/{arguments.wat, hello.wat, check.py}
├── terminal/{check.py, interruptions.json, resize.json, stdio.wat}
├── terminal-editing/readline.json
└── wast/{check.py, package-session.json}

src/html-rt/test/                      # unit tests: browser harness
├── offline-html-package.cjs
├── browser-{test-suite,upload}-runtime.cjs
├── render-worker-session.json
├── terminal-screen.cjs
├── runtime-config.{py,cjs}
└── guest-session/*.{cjs,py,json,wat}  # non-wast half of tests/guest-session-*

src/aux/<pkg>/test/
├── <unit test files or none>
└── system → ../../../vfs/root/test/aux/<pkg>    # the only system-test entry
```

Per-package symlink shape:

| Package | Has unit tests? | `src/aux/<pkg>/test/` layout |
|---|---|---|
| `libc` | yes (guest-sdk probes) | real dir, `test/system` symlink + `test/sdk/*.c*.py` |
| `vim` | yes (session fixtures) | real dir, `test/system` symlink + `test/sessions/*` |
| `clang` | yes (per-topic probes + mk) | real dir, `test/system` symlink + `test/<topic>/*` |
| `bash` | yes (future) | real dir, `test/system` symlink |
| `sleep` | no | `src/aux/sleep/test` itself is `→ ../../vfs/root/test/aux/sleep` |

## Mirror removal

Remove after each phase lands, in roughly this order:

- [ ] `src/cli-rt/Makefile`: the block of `cmp ../../tests/engine-regressions/*.wast ../vfs/root/test/engine-regressions/*.wast` and the surrounding loop (currently 15+ `cmp` lines).
- [ ] `src/aux/libc/libc.mk` install step that copies `tests/*.wast` into `src/vfs/root/test/aux/libc/` — the authored path becomes the VFS path.
- [ ] Same for `vim.mk`, `clang.mk`, and any other `install-<pkg>` rule that mentions `src/vfs/root/test/aux/<pkg>/`.
- [ ] `src/system-tests/Makefile` and `src/system-tests/README.md` once the directory is empty.
- [ ] `tests/html-rt.sh` after confirming `src/vfs/root/test/html-rt.sh` is the referenced copy.

## Migration steps

### Phase 1 — Rename `tests/` to `test/` for aux packages

- [ ] `git mv src/aux/libc/tests src/aux/libc/test`
- [ ] `git mv src/aux/vim/tests src/aux/vim/test`
- [ ] `git mv src/aux/sleep/tests src/aux/sleep/test`
- [ ] `git mv src/aux/clang/tests src/aux/clang/test`
- [ ] Update `src/aux/*.mk`, `src/aux/test.mk`, `src/aux/*/*.mk`,
      `src/aux/*/setup.sh`, and any `AGENTS.md`/`CLAUDE.md`/`docs/*` references
      that mention `tests` under `src/aux/`.

### Phase 2 — Delete confirmed duplicates

All byte-identical to a VFS copy; verified by `diff -q` across the full set:

- [ ] `rm tests/engine-regressions/*.wast` (15 files)
- [ ] `rm tests/diy-posix-test/{bulk-operations,mmap,spectest-isolation-{a,b}}.wast`
- [ ] `rm tests/html-rt.sh`
- [ ] `rm src/system-tests/libc/*.wast` (9 files)
- [ ] `rm src/system-tests/scheduling/*.wast` (4 files)
- [ ] `rm src/system-tests/abort/abort.wast`
- [ ] `rm src/system-tests/clang/compiled.wast`
- [ ] `rm src/system-tests/clang/{checkpoint,classification,endian,filesystem,logb,mutex,stdio,synchronization,thread-pool,wide,wide-format}/*.wast` and `clang/clocks/{after,before}.wast`
- [ ] `rm src/system-tests/cli-runtime/wast/{input,language,output,providers,runtime,unlinkable}.wast`
- [ ] `rm src/system-tests/wast/{pass,report.fail,report-files}.wast`
- [ ] `rm -r tests/__pycache__ src/system-tests/guest-sdk/__pycache__`
- [ ] After each `rm`, update the matching `cmp` line in `src/cli-rt/Makefile`.

### Phase 3 — Move harness code out of `tests/`

Native engine harness (unit tests) → `src/engine/test/`:

- [ ] `tests/c-engine-*.{c,wat,cjs,so.wasm}` (~35 files) → `src/engine/test/`
- [ ] `tests/posix-{kernel,path-abi,path-vfs,select-abi,select,signal,wait}.c` → `src/engine/test/`
- [ ] `tests/wasm-{binary-primitives,import-decode,validation}.c` → `src/engine/test/`
- [ ] `tests/wast-parser-reentrant.c`, `tests/source-loader.c` → `src/engine/test/`
- [ ] `tests/shared-library-packaging-check.py` → `src/engine/test/`
- [ ] `tests/corpus-schedule*.cjs`, `tests/test-corpus-*.py`, `tests/{native,browser}-corpus-expected-failures.txt` → `src/engine/test/corpus/`
- [ ] `tests/vfs-{directory-check.py,package.cjs,tar-loader.cjs}` → `src/engine/test/vfs/`

cli-rt harness (unit tests) → `src/cli-rt/test/`:

- [ ] From `src/system-tests/cli-runtime/`: `direct-start.c`, 12 `*.wat`
      probes, `browser-check.mjs`, `entrypoints/`, `terminal/`,
      `wast/{check.py,package-session.json}`
- [ ] From `src/system-tests/terminal-editing/`: `readline.json`, `README.md`

Browser harness (unit tests) → `src/html-rt/test/`:

- [ ] `tests/offline-html-package.cjs`
- [ ] `tests/browser-test-suite-runtime.cjs`, `tests/browser-upload-runtime.cjs`
- [ ] `tests/render-worker-session.json`, `tests/terminal-screen.cjs`
- [ ] `tests/runtime-config.{py,cjs}`
- [ ] Non-`.wast` half of `tests/guest-session-*` (~55 files) → `src/html-rt/test/guest-session/`

Per-aux unit tests:

- [ ] `src/system-tests/clang/<topic>/{probe.*, run.sh, *.mk, README.md}` → `src/aux/clang/test/<topic>/`
- [ ] `src/system-tests/clang/{alignment,clocks,clocks-controlled,fenv,floating,integer}.json`, `cxx-sdk.cpp`, `setup.sh`, `README.md`, `wide/memory-browser.json` → `src/aux/clang/test/`
- [ ] `src/system-tests/guest-sdk/{abi.c,startup.c,stat.c,build-sysroot.py,check.py}` → `src/aux/libc/test/sdk/`
- [ ] `src/system-tests/vim/{session.json, scroll-session.json, scroll.txt, README.md}` → `src/aux/vim/test/sessions/`
- [ ] `src/system-tests/abort/{probe.c, run.sh, browser.json, README.md}` → `src/engine/test/abort/`

### Phase 4 — Add missing system tests to the VFS

Content that currently lives only in `tests/` and belongs in `src/vfs/root/test/`:

- [ ] `tests/diy-posix-test/posix-kernel.wast` → `src/vfs/root/test/diy-posix-test/posix-kernel.wast`
- [ ] `tests/test-suite-*.wast` (26 files) → `src/vfs/root/test/system/test-suite/`
- [ ] `tests/vfs-mounted-paths.wast` → `src/vfs/root/test/system/vfs/mounted-paths.wast`
- [ ] `tests/render/terminal.wast` → `src/vfs/root/test/html-rt/render/terminal.wast`
- [ ] `.wast` half of `tests/guest-session-*` → `src/vfs/root/test/system/guest-session/`
- [ ] `tests/tail-call-smoke.wast` → `src/vfs/root/test/aux/bash/tail-call-smoke.wast` (and symlink from `src/aux/bash/test/system`)
- [ ] `tests/guest-sdk-mounted.wast`, `tests/guest-test-*.wast` → `src/vfs/root/test/system/guest-sdk/` or `.../guest-test/`

### Phase 5 — Rewrite the WAT front-end fixtures

The 12 `tests/c-engine-*.wast` fixtures all exercise the engine's WAT front
end. 10 of them are bare modules with no assertions; 2 are self-asserting
already. Each needs to be rewritten self-asserting (wrapping each module in
`assert_malformed`/`assert_invalid`/`assert_unlinkable` based on where the
engine actually rejects it) and consolidated to four files:

- [ ] `src/vfs/root/test/engine-regressions/wat-frontend/unresolved-references.wast`
      — merges 9 `c-engine-unresolved-*.wast` fixtures: `element`,
      `element-index`, `function`, `global`, `global-init`, `module-indices`,
      `start`, `table`, `type`.
- [ ] `src/vfs/root/test/engine-regressions/wat-frontend/error-location.wast`
      — from `c-engine-deferred-location.wast`, asserts the diagnostic
      carries source position.
- [ ] `src/vfs/root/test/engine-regressions/wat-frontend/function-capacity.wast`
      — from `c-engine-many-functions.wast`; keep the 128+ function fixture,
      add explicit `invoke`/`assert_return` for the top of the table.
- [ ] `src/vfs/root/test/engine-regressions/wat-frontend/syntax-coverage.wast`
      — from `c-engine-stage7-front-end.wast`, already self-asserting; rename
      for clarity.
- [ ] Delete the 12 original files from `tests/`.
- [ ] Update `src/engine/README.md:168` to point at the new filename.

If any case resists `assert_*` wrapping because the engine's rejection path
isn't observable from a WAST assertion, that single file instead lives at
`src/engine/test/wat-frontend/<name>.wast` with a C driver that asserts
rejection. The expectation is zero fallbacks — all 12 are routed to the WAT
decoder.

### Phase 6 — Install per-package symlinks

- [ ] `ln -s ../../../vfs/root/test/aux/libc src/aux/libc/test/system`
- [ ] `ln -s ../../../vfs/root/test/aux/vim src/aux/vim/test/system`
- [ ] `ln -s ../../../vfs/root/test/aux/clang src/aux/clang/test/system`
- [ ] `ln -s ../../../vfs/root/test/aux/bash src/aux/bash/test/system` (after
      `src/aux/bash/test/` exists)
- [ ] `rm -r src/aux/sleep/test && ln -s ../../vfs/root/test/aux/sleep src/aux/sleep/test`

Verify: `find src/aux -maxdepth 3 -name test -type l` prints the sleep link,
`find src/aux -maxdepth 4 -name system -type l` prints the four others, and
every link resolves to an existing directory.

### Phase 7 — Entry-point wiring

- [ ] Add to `src/engine/Makefile`:

      ```make
      .PHONY: test
      test:
          $(MAKE) -C ../cli-rt test
      ```

- [ ] Confirm `make -C src/engine test` runs the full regression battery
      including `src/vfs/root/test/wasm-spec/core/*.wast` via the cli-rt
      runner (requires the runner to walk the VFS test tree).
- [ ] Delete `src/system-tests/Makefile` and `src/system-tests/README.md`.
- [ ] `rmdir tests` and `rmdir src/system-tests` once empty.

### Phase 8 — Documentation and references

- [ ] Replace every `tests/engine-regressions/`, `tests/diy-posix-test/`,
      `src/system-tests/` reference in `AGENTS.md`, `CLAUDE.md`,
      `docs/architecture.md`, `docs/techniques.md`,
      `docs/native-guest-session.md`, `docs/posix-kernel-retained-coverage.md`
      with the new VFS path.
- [ ] Update `start.sh` references to `tests/c-engine-browser-runtime.cjs`
      (line 33) and anything else that hard-codes `tests/`.
- [ ] Update `src/engine/README.md` references to new filenames.
- [ ] Remove the `tests/`-related entries from `src/vfs/.inventory.json` if
      any exist (currently empty/malformed).

## Open items

1. **cli-rt test runner must enumerate the VFS tree.** Today the runner is
   invoked per-fixture; after the migration it needs to walk
   `src/vfs/root/test/{engine-regressions,diy-posix-test,html-rt,system,aux}/`
   recursively (plus the `wasm-spec` symlink target). Decide: Makefile glob,
   manifest file, or walker inside the cli-rt binary itself.

2. **`src/vfs/root/test/manifest.json`.** Currently 553 KB and listed as
   generated. Confirm whether it needs regeneration after each phase or
   whether it's an immutable top-level descriptor.

3. **`src/aux/bash/test/`** does not exist yet. Create it before Phase 4
   moves `tail-call-smoke.wast`; alternatively hold the bash symlink until
   bash has a package-level test harness.

4. **Guest-session split.** The ~65 files under `tests/guest-session-*` have
   both self-asserting `.wast` and matching `.json`/`.cjs`/`.py` drivers.
   Audit each pair before Phase 3/4 to ensure the `.wast` can run standalone
   in `bash.html`; some may need the harness to seed VFS state first, in
   which case they stay with the harness in `src/html-rt/test/guest-session/`.

5. **Stale `.claude/worktrees/agent-a6252a14/`** contains an outdated copy of
   `src/cli-rt/Makefile` and other files. These are agent worktrees and not
   touched by this migration, but any script that globs the whole tree
   (e.g., corpus runners) should exclude them.

## Phase sequencing

Phases 1 and 2 are independent and can land in either order. Phases 3–6 can
proceed per-area in parallel once Phase 1 is done. Phase 5 (WAT front-end
rewrite) is independent of everything else and can start immediately. Phase
7 blocks on Phases 1–6; Phase 8 blocks on 7.
