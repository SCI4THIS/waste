# Mounted test distribution

`src/vfs/root/waste/tests` is an explicitly installed snapshot of the WebAssembly corpus and
authored engine regressions: 296 WAST files in 15 groups, including four
unsupported legacy inputs. The original 284 identities remain unchanged;
Stages 6B.22–6B.33 add twelve `engine-regressions` fixtures. Stage 6B.36 extends
the pipe fixture from 99 to 136 assertions, bringing the group to 1,230.
Stages 6B.27, 6B.29, 6B.30, 6B.34 and 6B.35 add five shared wait/terminal/process-group
sessions. Stage 6B.37 extends terminal readiness to 90 checks, bringing their
total to 351; these require host events and do not change the installed batch.
OCaml is a reference only for Wasm/WAT/WAST language semantics and standard
spec-test imports. POSIX/kernel/libc/application fixtures are verified through
C native/browser contracts, not OCaml kernel parity. No additional OCaml
kernel development is planned; existing kernel integration is scheduled for
removal in [deferred cleanup](active-ocaml-language-oracle-plan.md).

Do not edit the installed WAST files. Official sources remain in the pinned
`submodules/wasm-spec/test`; authored regression sources remain under `tests/`;
generated libc fixtures remain under `build/html-rt/waste-libc/tests`.

Refresh and check the distribution from the repository root:

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
```

Installation first generates fresh worker-test metadata under `build/engine`,
checks every source and companion hash, then atomically publishes only test
snapshots and the upstream test license. It refuses edited installed snapshots
and changed selection policy. After reviewing a deliberate selection change,
use `python3 src/html-rt/tools/build-test-corpus.py --install --review-selection`.
HTML packaging only consumes the installed inventory; it never refreshes it.

`/root/waste/tests/manifest.json` records source paths/hashes, pinned oracle revision,
grouping, expected outcomes, skips, original execution specifications, assertion
counts when known, and runtime profiles. Feature labels are group-derived, not
a complete analysis of each module. Original host source paths are provenance,
not permission to fall back to host files during future mounted execution.
`/root/waste/tests/.support` contains assembled DIY modules, mmap fixture bytes and libc
client includes. Host-only test harnesses and build inputs are hash-traced in
the manifest, not installed as guest executables. The upstream Apache-2.0
license is mounted at `/usr/share/licenses/wasm-spec-tests/LICENSE`.

Only `diy-posix-test/posix-kernel.wast` retains the browser-native WebAssembly
compatibility backend. Native `cli-rt` marks it SKIP; browser Bash's Installed
tests controller dispatches its packaged module/step spec to the same
compatibility imports and executes it in an isolated worker. Stage 6B.41 moves
bulk operations and both spectest memory fixtures to shared command streams:
25 ordered checks with unchanged WAST bytes and no assembled support modules.
Together with mmap, the DIY group has four C-engine WAST passes / 48 checks;
the retained compatibility module contributes seven browser-side Wasm checks.
Intentional memory aliases within one script and fresh stores across scripts
are checked in both orders with one and three jobs. The three language fixtures
also pass the OCaml oracle individually; no OCaml POSIX providers are required.
OCaml direct/threaded profiles remain recorded, including the original
threaded-runner default, quantum and timeout. These legacy metadata fields
record provenance; they do not require further OCaml kernel development or
continued POSIX parity. Their retirement belongs to the deferred cleanup plan.
The original C dashboard harness had no per-test deadline; the manifest
records that historical profile. The Bash page's installed-test runner applies
the current browser group budgets. The initial
installation corrected stale `sourceBytes` in fourteen libc staging entries,
without changing any source bytes, hashes or execution policy; these repairs
are retained in `baselineRepairs`.

After rebuilding the offline Bash page, verify the actual mounted files and
the shell redirection path:

```sh
python3 tests/test-corpus-bash.py build/html-rt/bash.html
node tests/c-engine-vfs-browser.cjs build/html-rt/bash.html
make -C src/cli-rt vfs-check
```

The development and release workflows are deliberately separate:

```sh
# Fast iteration: build the page, then run the native corpus as needed.
./start.sh --html-bash
make -C src/cli-rt corpus-native

# Focused browser-worker and terminal-model checks (Node; does not start Chromium).
./start.sh --html-check

# Release acceptance: actual offline-page/browser checks and the full browser suite.
./start.sh --html-browser-full
```

`--html-bash` only compiles/packages `bash.html`; it does not invoke test
harnesses. The focused check writes `build/engine/logs/c-engine-bash.log` and
currently takes about 38 seconds on the measured development host. The page
build took about 92 seconds. `make -C src/cli-rt corpus-native` took 58.5
seconds on the same host for 296 records using four isolated jobs. Full browser verification is intentionally
explicit because it starts Chromium and executes the complete browser suite;
the installed-corpus results use the same assertion-aware JSON contract as the
native runner. For quick diagnosis, use the native runner's `--group`,
`--exclude`, positional identity, and `--results` filters rather than rerunning
the complete corpus.

The Bash probe opens all 296 paths using guest redirection; the compiled-engine
probe reads each through EOF and checks length and endpoint bytes. The native
sanitizer probe compares every installed node's metadata and complete contents and
tests the 960 installed-node / 1,024 kernel-node bounds. The file-content limit remains
64 MiB. The installed namespace reserves 64 slots beyond the maximum 960
installed nodes for runtime-created nodes.

The Bash redirection sweep is a separate shell boundary check. Its initial
Stage 6C failure was traced to the variadic Wasm `fcntl` ABI: Bash passes a
guest stack-area pointer for the third argument, but the fixed-width adapter
treated it as the `F_DUPFD` minimum and returned `EMFILE`. Bash now imports
`waste_kernel.fcntl_varargs_v1`, which reads the integer argument from guest
memory before calling the kernel operation; fixed-width `env.fcntl` callers
remain available. The focused launch-file redirection passes, and
`python3 tests/test-corpus-bash.py build/html-rt/bash.html` opens all **296/296**
installed WAST files through guest redirection. The `--limit N` option remains
available to bound future diagnostic runs. The compiled-engine VFS harness
continues to cover exact bytes and inventory consistency as a distinct lower-
level boundary.

Stage 6C browser acceptance was completed manually in Firefox. The user
confirmed prompt startup and the launch-file redirection check, then saved
`build/html-rt/stage6c-browser-results.json`. Its 296 records report 290 PASS,
0 FAIL, 2 XFAIL, 0 XPASS, and 4 SKIP with exit code 0 and no failure or
unexpected-pass entries. This provides the actual `file://` page and browser
batch evidence that the worker-only redirection sweep cannot supply.

The unified `bash.html` page supplies browser batch scheduling, isolation,
unsupported-test reporting, and downloadable assertion-aware results; native
execution uses the same installed manifest. Focused Node worker tests consume
generated payload metadata without building a second HTML page. Supported
language tests are compared with the OCaml oracle; no OCaml POSIX/kernel
parity is required.

The native batch companion now runs the installed C-engine corpus without
Node or dashboard payload generation:

```sh
make -C src/cli-rt corpus-native
```

Executor regression fixtures can be selected directly:

```sh
build/cli-rt/waste-test --vfs-root=src/vfs --group=engine-regressions --json
```

The same group is available through browser Diagnostics and guest
`/bin/waste-test --group=engine-regressions`. Its assertion-by-assertion C/WAST
mapping and retained sanitizer/API boundaries are recorded in
[test-coverage.md](test-coverage.md).

This builds `build/cli-rt/waste-test`, reads the installed tree directly with
its inventory metadata, then executes mounted `/root/waste/tests/manifest.json` with four
isolated native children. It writes assertion-aware records to
`build/cli-rt/corpus-results.json`. It does not build the browser engine or
refresh the installed distribution. An installed directory can be tested directly:

```sh
build/cli-rt/waste-test --vfs-root=src/vfs \
  --expected-failures=tests/native-corpus-expected-failures.txt \
  --group=core --jobs=4 --json --results=build/cli-rt/core-results.json
build/cli-rt/waste-test --vfs-root=src/vfs --list
```

The installed guest command runs the same batches from native or browser Bash:

```sh
/bin/waste-test --list --group=libc-test
/bin/waste-test --jobs=2 --results=/tmp/results.json path-runtime.wast address.wast
/bin/waste-test --json path-runtime.wast >/tmp/results.json
/bin/download /tmp/results.json
```

`--results` names a guest VFS file; stdout/stderr redirection uses guest descriptors.
Completed reports retain assertion details. Exit codes are 0 for accepted results,
1 for failures/XPASS/timeouts, 2 for invalid options or unavailable capability, and
130 for Ctrl-C cancellation. Bash remains usable after each batch. Requests use
the installed directory (native) or extracted package (browser), plus the
runtime's tracked expected-failure policy;
changes to the parent shell's `/root/waste/tests` overlay do not change the isolated workers.
The guest cannot override host root, executable, manifest or policy paths.

Build and explicitly refresh the launcher snapshot with
`make -C src/cli-rt guest-test-install`. Then regenerate the browser page; packaging
never compiles it. `make -C src/cli-rt guest-test-check` checks native/production
worker parity using the generated `build/html-rt/bash.html`. Host-only worker,
Chromium and private engine checks remain separate from ordinary guest batches.

Repeat `--group`, `--exclude` and `--exclude-group` to select subsets; positional
arguments select an identity, mounted path or filename. Positive selections
are combined, then exclusions apply. Output and JSON records retain manifest
order across jobs. Every executed test gets a fresh store/kernel and the same
installed bytes and metadata. Manifest `vfs-file` assets are copied from mounted support paths to
their declared guest paths before execution; host provenance paths, inline WAST
and base64 fixtures are never used as execution fallbacks.

PASS/FAIL/XFAIL/XPASS classify assertion results and ordinary setup outcomes;
TIMEOUT and CANCELLED are
failures, and missing files, mount failures or process crashes cannot become
XFAIL. Five legacy/browser-compatibility entries remain visible as SKIP with
reasons. The mmap fixture runs through the shared process driver. The result
contract keeps the previous identity/status/count/timing fields, adds a skip
count, and retains every assertion in `nativeReport`. Its separate `setup`
object records attempted/successful module instantiations and ordered failure
diagnostics (`line`, `status`, `phase`, `error`); `completed` records reaching
the command stream's EOF. Definitions are not instantiations. Empty streams
and valid modules with no assertions can pass; invalid, unlinkable, trapping
or unencodable modules fail even with zero assertions. Recovering at the next
command preserves later results without clearing the failed setup outcome.
Explicit module assertions retain their existing assertion records.

Stage 6B.42 exposed ten previously ignored setup failures across seven official
language files. Stage 6B.43 fixes six of them by retaining up to 128 data/element
segments and rejecting excess declarations explicitly, rather than silently
truncating at 32. Five repaired files pass C and the OCaml language oracle and
leave both XFAIL lists. Stage 6B.44 preserves non-null function-index segment
types in WAT and binary input, repairing the three active-element errors in
`core/elem.wast` and removing its XFAIL entry. Stage 6B.45 supplies the standard
`spectest.table64` binding (64-bit nullable funcref, limits 10–20), removing the
last official setup XFAIL after native/browser/OCaml language agreement.
The full corpus is **289 PASS / 2 XFAIL / 5 SKIP**, with all **64,309** prior
assertion/action results preserved and **2,313 setup attempts / 2,313 successes**.
Only the two existing libc XFAILs remain. Portable table behavior/isolation
fixtures are host-staged outside installed selection; manifest and inventory
bytes are unchanged. See
[the setup audit](wast-setup-coverage.md); XFAIL records retain every setup
diagnostic rather than concealing acceptance gaps. Unexpected failures or
passes exit 1, configuration/reporting errors exit 2, and SIGINT/SIGTERM return
the corresponding signal exit status after reaping children and writing results.

Stage 6B.46 repairs plain `table.init`, `data.drop` and `elem.drop`, plus missing
folded explicit table/segment combinations. Native/browser/OCaml agreement on
`tests/test-suite-flat-bulk.wast` preserves 190 portable checks and three setups
outside installed selection. All installed outcomes, assertion and setup records
remain unchanged. Rejection probes include missing immediates/names, unknown
indices and stack/address types.

Stage 6B.47 implements plain `table.copy`/`table.fill` and missing folded numeric
copy/fill forms. `tests/test-suite-table-copy-fill.wast` stays outside installed
selection: **611 checks / seven setups**, including overlap, bounds atomicity,
null/reference identity, cross-module function ownership and mixed table widths.
Twenty-six invalid and three quoted malformed assertions preserve rejection
coverage. Native/browser C and authored/33 C-encoded module OCaml language checks
agree; sanitizer/leak, seven ordinary acceptance/rejection probes and full
corpus/setup/session parity pass without installed byte changes. Evidence:
`build/engine/refactor-stage6b-table-copy-fill/`. Next enumerate supported official
inputs for the language-only OCaml comparison ledger, recording bounded limits
and unsupported shorthand explicitly.

Stage 6B.48 records every installed identity in
[the official language ledger](wasm-language-coverage.md): **265 official
identities / 261 compared and independently accepted / four legacy exclusions**;
31 repository fixtures are outside official scope. **259** inputs agree on
ordered check identity/kind and setup counts. C truncates a 257-byte name in
`core/names.wast`; `core/inline-module.wast` validates a definition in OCaml but
instantiates once in C. Both stay visible in a source-hash/issue-kind-pinned
comparison policy, separate from runtime expected-failure lists. Fresh native
results reconcile with the Stage 6B.47 actual-browser report; all corpus/setup
records and installed/runtime bytes remain unchanged. Run
`tests/language-oracle-check.py` with a current native report and optional browser
report; new/changed gaps or repaired baselines fail, and `--strict` fails known
gaps too. Next repair full export/action names and equivalent escape spellings.

The CLI and native batch runner execute WAST through the engine's balanced
command scanner, as the browser does. Each action runs immediately against the
current or explicitly named instance, including actions after module assertions.
Successful registration changes subsequent imports without changing the current
instance. Result records include the command's source `line`; balanced parse
errors produce failed records and allow later commands to execute, while an
unterminated tail fails after preserving earlier results. Module definitions and
live instance metadata remain owned until replacement or script teardown.
Ordinary module setup diagnostics still follow the existing reporting policy:
they go to native stderr and contribute no standalone assertion result. A passing
assertion report alone does not establish acceptance of every setup module.

Default native deadlines are 15 seconds for `core`, 10 seconds for SIMD,
bulk-memory and memory64, and 5 seconds elsewhere. `--timeout-ms=N` overrides
every group's default; repeatable `--timeout-group=NAME:N` takes precedence.

The offline `bash.html` page also runs the installed corpus directly. Open
**Diagnostics → Installed tests**, choose **List tests** to populate the group
selector, then choose a group and/or space-separated filenames and **Run tests**.
Positive selections combine as in the native runner. The page defaults to two
jobs and a 60-second deadline per test, including worker startup and preparation.
**Cancel tests** terminates active workers and marks undispatched runnable tests
CANCELLED; unsupported entries remain SKIP. **Download results** saves the full
assertion-aware JSON report. Bash has its own worker and remains usable throughout.
Running these tests in the browser requires no Node process.

`test-suite.js` supplies browser scheduling and event handling; each test uses
a fresh instance of the production `worker.js` and C engine. The C API mounts
the tar-extracted files with inventory metadata, decodes `/root/waste/tests/manifest.json` with the same bounded
decoder as `waste-test`, and reads WAST/companions through the kernel. Dashboard
payloads, provenance source paths, inline WAST and base64 fixture fields are
never execution fallbacks. The tracked browser expected-failure list is a
separate host package asset; it is validated against mounted identities and
does not become a guest VFS node. Failed source/companion preparation, worker
crashes, deadlines and cancellation cannot become XFAIL.

The browser report uses the native `summary`/`tests` identity, status,
expected-failure, count and elapsed-time contract. `browserReport` retains each
completed assertion; interrupted records discard partial assertion reports.
Current browser workers read full bounded function names through the C API's
name pointer/length exports and preserve leading UTF-8 BOM characters. The
legacy short-name result layout remains available to older boundary probes.
The report's `exitCode` is 0 for accepted outcomes, 1 for regressions/XPASS and
130 after cancellation. Selection/configuration errors reject before execution.
The browser controller API also accepts `excludeFiles`, `excludeGroups` and
`timeoutGroups`; absent a global override its defaults match the native group
budgets. Records retain manifest order across parallel jobs; UI progress arrives
as tests complete. Browser jobs are bounded to 1–8.

Focused host-boundary and actual offline-page automation remain available:

```sh
node tests/browser-test-suite-runtime.cjs
node tests/c-engine-offline-browser.cjs --suite-full
```

The latter checks the full mounted corpus, cancellation, assertion fidelity,
Bash use after a batch and the absence of external runtime requests. The
compatibility-mode migration, regression-fixture migration, and full
WebAssembly-language oracle acceptance remain separate work. Missing OCaml
POSIX imports are outside that acceptance scope.
Report parent directories must already exist. The tracked expected-failures
list is an explicit host-side input, separate from immutable corpus provenance.
See `waste-test --help` and Stage 6B.17 in the active VFS plan.

The libc missing-path client uses `/tmp/waste-missing-path` so its expectation
also holds in the installed command namespace. Its authored source remains in
`tests/libc-test/path-runtime-client.wast.inc`; rebuild and explicitly install
fixtures after changes.

The open probe deliberately uses `: < "$f"`. The separate Bash `read` trap was
reproduced natively and traced to its `pop_scope` cleanup callback at table slot
145: a void callback was passed to an int-returning unwind-protect dispatcher.
The launch builder now supplies a typed adapter, preserving Wasm validation.
The shared native/browser session regression reads the mounted file and returns
to the prompt; see `native-guest-session.md`. The original Stage 5 evidence
remains available and is not a missing-file diagnostic.

The portable caller-memory and timed-continuation fixtures use real guest POSIX
imports. Native CLI/batch adapters service finite SELECT deadlines through the
shared process driver; terminal or indefinite waits still require `waste-session`.
Their C callback identity and explicit snapshot-replay checks remain separate
sanitizer gates (`caller-instance`, `continuation`). The native OCaml oracle
accepted the pipe-based caller fixture in earlier checks but lacks
`env.select` for the timed fixture. These POSIX comparisons are historical
evidence, not language-oracle requirements or an OCaml provider backlog. The path fixture uses private guest-created files and moves fifteen
traversal/normalization assertions out of C; its oracle run stops at the
unsupported `env.chmod` import. That POSIX import is outside oracle scope.
See the coverage ledger for historical results and retained path/codec
sanitizer checks.

The pipe/descriptor fixture adds 99 real-import assertions and moves ten
checks out of the C kernel gate. SELECT uses zero timeouts; EOF, capacity,
duplication and close-on-exec flags run through both ordinary batch adapters.
Read/write preserve their current raw-error import convention. The native
OCaml interpreter lacks `env.select` for this fixture as well; no additional
OCaml kernel implementation is required. Its zero-count
read/write probes exposed and now cover the shared adapter's NULL-buffer bug;
see `test-coverage.md` for the fix and retained ownership/readiness checks.
