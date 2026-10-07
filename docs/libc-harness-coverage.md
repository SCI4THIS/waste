# Libc harness coverage

Guest value expectations for the installed `libc-test` group live in fourteen
authored `*-client.wast.inc` files. The installed group combines each client
with the merged guest-libc module and runs through the native C runner and
production browser worker with real C providers.

The focused gate is `tests/libc-corpus-check.py`, exposed by
`make -C src/cli-rt libc-native` and `libc-sanitize`. It consumes installed
inputs without generating a page. `--report PATH` checks an existing
native/browser libc group report using the same assertion contract.
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

The five currently failed expectations are `boundary-execve`,
`boundary-readlink`, `boundary-opendir`, `boundary-ioctl`, and `terminal`.
Each returns zero where its authored assertion expects one. The gate pins the
failed assertion names and requires the current zero-versus-one
result-mismatch diagnostics; every other assertion must pass. An extra failure
inside a file already marked XFAIL fails the focused gate. A corrected
expectation must update both the per-assertion baseline and applicable
file-level expected-failure lists in the same change.

The filesystem boundary clients retain older ENOSYS-profile assumptions;
real handlers can report other errors. The `terminal` client combines several
calls and callback state into one boolean. This documentation preserves that
compound assertion without claiming its individual suboperations all pass.
Resolving or splitting these expectations belongs to a separately reviewed
libc/POSIX contract change.

## Retained native boundaries

`tests/libc-test/allocator-native.cjs` instantiates the actual guest-libc Wasm
artifact in the host WebAssembly implementation. Keep its 5,000-allocation
stress loop, alignment, live-range nonoverlap, first/last-byte preservation,
free order and memory-buffer refresh after growth. The five installed
allocator assertions complement this artifact/memory-growth boundary; they
do not replace the stress workload or prove the host's memory-buffer behavior.

The merged artifact imports 24 POSIX functions, so instantiation without an
imports object fails before allocation. The harness binds these functions to
throwing guards. All 5,000 allocations and final frees must complete with zero
import calls; an explicit `isatty` wrapper call afterward must reach and throw
from the guard. Unexpected nonfunction imports or import modules are rejected.

Keep compiled guest C header/layout clients, audited SDK/provider signatures,
stat/SELECT/signal codecs and canaries, and native private ownership checks.
WAST imports alone do not establish C compilation against the public headers.
Packaging, DOM, worker boot and actual `file://` delivery remain browser
boundaries.

The libc gate rejects native setup diagnostics and checks parsed names/counts,
but is not a complete command-stream acceptance audit. Full module acceptance
and broader language conformance remain separate work.
