# Libc harness coverage audit

Stage 6B.39, 2026-10-04: `tests/libc-test/libc-runtime.cjs` is a legacy
OCaml application-runtime driver. Its guest value expectations already reside
in fourteen authored `*-client.wast.inc` files. The installed `libc-test`
group combines each client with the merged guest-libc module and runs through
the native C runner and production browser worker with real C providers.
No additional guest assertion is moved or counted as a migration here.

## Driver expectations and replacements

| Legacy operation | Current C coverage / disposition |
| --- | --- |
| Discover and sort all generated `.wast` files | Native `libc-native` gate requires the installed group's ordered identities to equal the nonempty authored client selection. It verifies installed client suffix bytes and obtains assertion names from the existing C parser. Corpus installation/checks retain full generated-byte and provenance audits. |
| Exact generated fixture filename filter | `waste-test --vfs-root=src/vfs libc-test/matching-sort.wast`, or repeat identities for multiple files. Both runtime controllers reject unknown selections. The old filter can silently select zero files. Preserve rejection, not vacuous success. |
| Optional export-name filter deletes other single-line `assert_return` commands | Existing runners retain the complete fixture and report each named assertion. No source-rewriting filter is added. This legacy debugging convenience is not an additional guest expectation; filtering can discard coverage or stateful setup assertions. |
| Sequential/threaded OCaml artifact choice, scheduler quantum and thread count | Legacy OCaml application scheduling is outside the language-oracle scope. C batches isolate each fixture; native `--jobs` and browser job selection have their own host-boundary gates. No OCaml kernel/provider work is required. |
| Prepend deterministic `waste_kernel` and `env` providers | Retain only as legacy profile evidence. C tests use production providers; the stub functions do not establish C POSIX behavior. See the provider table below. |
| Await loader completion and reject a nonzero interpreter exit | Native batch completion/exit status and browser `done`/controller exit status are already covered by host-boundary gates, including guest failure, malformed tails, deadlines and cancellation. The focused libc gate additionally requires every ordered assertion and exact counts. |
| Print the number of passing suites; reject thrown errors | Native JSON/text and browser downloadable reports preserve identities, statuses and individual outcomes. Expected failures remain visible. The new gate rejects missing records, skips, setup diagnostics on the native side and any untracked assertion failure. |

The focused gate is `tests/libc-corpus-check.py`, exposed by
`make -C src/cli-rt libc-native` and `libc-sanitize`. It consumes installed
inputs without generating a page or building OCaml. `--report PATH` checks an
existing native/browser libc group report using the same assertion contract.
The native sanitizer target enables ASan/UBSan and retains the documented
ptrace-related Make leak-check exception; a separate run verifies leaks.

## Complete installed assertion inventory

| Fixture | Assertions | C outcome |
| --- | ---: | --- |
| accounts | 2 | PASS |
| allocator | 5 | PASS |
| entropy-messages | 1 | PASS |
| environment-boundaries | 19 | XFAIL: 15 pass, four fail |
| locale-wide | 3 | PASS |
| matching-sort | 1 | PASS |
| memory-conversion | 2 | PASS |
| path-runtime | 2 | PASS |
| select-abi | 6 | PASS |
| select-runtime | 4 | PASS |
| stat-abi | 5 | PASS |
| stdio | 3 | PASS |
| terminal | 1 | XFAIL: compound expectation fails |
| time-resource | 2 | PASS |
| **Total** | **56** | **12 PASS, 2 XFAIL; 51 passing assertions** |

The five current failed expectations are `boundary-execve`,
`boundary-readlink`, `boundary-opendir`, `boundary-ioctl`, and `terminal`.
Each currently returns zero where its authored assertion expects one.
They are existing failures under both C runtimes, not new acceptance or
OCaml-provider requirements. The gate pins the failed assertion names and
requires the current zero-versus-one result-mismatch diagnostics, while every other assertion must pass.
Thus an extra failure inside a file already marked XFAIL fails the focused
gate. A corrected expectation or provider must update both the per-assertion
baseline and applicable file-level expected-failure lists in the same change.

The filesystem boundary clients retain older ENOSYS-profile assumptions;
real handlers can report other errors. The `terminal` client combines several
calls and callback state into one boolean. This audit preserves that compound
assertion without claiming its individual suboperations all pass or identifying
which one fails. Resolving or splitting these expectations belongs to a
separately reviewed libc/POSIX contract change; this slice changes no guest
semantics or expected-failure policy.

## Legacy stub-provider assumptions

| Injected imports | Legacy result | C disposition |
| --- | --- | --- |
| `tcgetattr_v1`, `tcsetattr_v1` | `-25` | Real terminal state/guest codecs; shared terminal sessions and private C gates cover the supported contract. |
| `startup_v1`, `isatty_v1` | `0` | Real startup/descriptor profile; stub constants are not C behavior requirements. |
| `path_access_v1` | `-2` | Real namespace/path access. The missing-path client's two assertions pass through the installed C namespace. |
| `path_stat_v1`, `ioctl_v1` | `-38` | Real C providers/ABI; compiled/public-layout and retained codec checks remain. The older ioctl expectation is explicitly among the current failures. |
| `select_v1`, `pselect_v1` | `-22` for negative nfds, otherwise `0` | Four wrapper assertions use real imports. Readiness, timeouts, signals and sessions have separate C coverage; constants cannot establish that behavior. |
| `env.open`, `close`, `readdir_v1`, `chdir`, `getcwd`, `execve`, `readlink` | `-38` | Real engine-owned descriptor/path/process operations where supported; unsupported behavior and old profile assumptions remain explicit. Do not add OCaml replacements. |

All sixteen injected functions are accounted for. They are historical
scaffolding for wrapper tests, not an OCaml POSIX implementation backlog.
OCaml remains solely the Wasm/WAT/WAST language oracle; kernel and
application-runtime pruning stays in the
[deferred retirement plan](active-ocaml-language-oracle-plan.md).

## Retained boundaries and limits

`tests/libc-test/allocator-native.cjs` instantiates the actual guest-libc Wasm
artifact in the host WebAssembly implementation. Keep its 5,000-allocation
stress loop, alignment, live-range nonoverlap, first/last-byte preservation,
free order and memory-buffer refresh after growth. The five installed
allocator assertions complement this artifact/memory-growth boundary; they
do not replace the stress workload or prove the host's memory-buffer behavior.
This driver uses no OCaml and remains a focused Node gate.

The audit found its instantiation setup stale: the merged artifact now imports
24 POSIX functions, so instantiation without an imports object fails before
allocation. The harness now binds these functions to throwing guards. All
5,000 allocations and final frees must complete with zero import calls; an
explicit `isatty` wrapper call afterward must reach and throw from the guard.
This supplies no POSIX semantics and does not add an OCaml provider. Unexpected
nonfunction imports or import modules are rejected. The original allocator
stress body and endpoint checks remain intact.

Keep compiled guest C header/layout clients, audited SDK/provider signatures,
stat/SELECT/signal codecs and canaries, and native private ownership checks.
WAST imports alone do not establish C compilation against the public headers.
`native-test-suite-check.py` and `browser-test-suite-runtime.cjs` retain
selection/completion/isolation/policy failure controls. Packaging, DOM, worker
boot and actual `file://` delivery remain browser boundaries.

The libc gate rejects native setup diagnostics and checks parsed names/counts,
but is not a complete command-stream acceptance audit. Ordinary setup modules
still do not each produce a standalone acceptance record in the general
runner, and browser output previews are bounded. Full module acceptance and
broader language conformance remain separate work. No driver, kernel code or
dashboard is retired by this audit.

Evidence and ordered native/sanitizer/browser comparison are recorded under
`build/engine/refactor-stage6b-libc-harness-audit/`; see the
[active plan](active-browser-vfs-layout-plan.md) for validation and next work.
