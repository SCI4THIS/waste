# Guest libc

`make -C src/aux libc` builds `build/aux/libc/libc.so.wasm` with Clang and
`wasm-ld --shared`. The sources consume the installed public SDK, PIC objects,
imported process memory/table and versioned engine-kernel adapters.
`make -C src/aux install-libc` explicitly publishes identical mode-0644 copies
at `src/vfs/lib/libc.so.wasm` and `src/vfs/usr/lib/libc.so.wasm`, using the common
VFS installation lock. The aux wizard installs libc before commands.

`make -C src/aux test-libc` runs the authored `tests/*.wast` through
`/bin/wast --verbose` in native Bash. Assertions live entirely in those WAST
files, and call the installed shared library through Bash's process memory,
table and `libc` provider. There is no embedded/static libc test build. The
allocator stress case performs 5,000 allocations with a bounded live ring,
checking alignment, overlap, reuse and retained bytes. `aligned-allocation.wast`
checks powers of two through page and larger alignments, reusable padding,
ordinary free/realloc ownership, mixed live blocks and overflow/error behavior.
`aligned_alloc` accepts nonzero power-of-two alignments and requires size to
be a multiple of alignment; invalid inputs fail with EINVAL. `posix_memalign`
requires alignment to be a power of two at least as large as a guest pointer,
accepts arbitrary sizes, and preserves both errno and the output on failure.
Zero-size success returns a freeable allocation. Oversized requests fail with
ENOMEM before narrowing block sizes; the allocator's signed i32 bookkeeping
limits the contiguous heap to addresses below 2 GiB, alongside engine limits.
`fenv.wast` checks the immutable Wasm floating-point environment against actual
arithmetic. The public header advertises only FE_TONEAREST and FE_ALL_EXCEPT=0.
The shared library supports empty flag snapshots and default save/restore;
nonempty exception requests, alternate modes, null outputs and malformed
snapshots fail without changing errno or output. Zero-divide, overflow and NaN
operations produce Wasm results without sticky flags. No host fenv state,
dynamic rounding or exception traps are exposed.
`log2-round.wast` checks binary64 `log2` and ties-away `round`. Logarithms
normalize normal/subnormal inputs and use a bounded atanh series; exact powers
of two and selected 100-digit references within four ULPs are covered. This is
not a correctly rounded transcendental guarantee. Zero logarithms return
negative infinity with ERANGE; negative inputs return NaN with EDOM. Other
inputs preserve errno. Both functions quiet signaling NaNs, tested from integer
bits independently of text NaN parsing. Rounding preserves signed zero,
NaNs/infinities and
errno, with explicit neighbors around halfway values. These guest providers
also serve the LLVM heat-color helper through the shared library.
`lround.wast` checks the wasm32 long conversion, including ties away from zero,
neighbors of the signed range limits, subnormals and non-finite inputs.
Successful conversions preserve errno. Unrepresentable results return LONG_MIN
and set EDOM before any trapping Wasm conversion; floating exception flags
remain unavailable, as described above.
`log2f.wast` checks the binary32 entrypoint used by LLVM's power simplifier.
It promotes inputs exactly, reuses the binary64 logarithm and narrows once;
it shares the domain/pole errno behavior and preserves errno on success.
The 60 assertions cover all 277 positive binary32 powers of two, 32
100-digit reference cases within one binary32 ULP, range-reduction boundaries,
subnormals, neighbors of one, infinities and signaling/quiet NaNs. This is
not a general correctly rounded transcendental guarantee. Run through Bash
with `/bin/wast --verbose /root/test/aux/libc/log2f.wast` after refreshing
installed tests, or run the package with `make -C src/aux test-libc`.
After packaging `bash.html`, automate the same WAST through the production
worker with `node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html
src/vfs/root/test/aux/libc/log2f-browser.json`. For a full sanitizer package run, use
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
WASTE_TEST_TIMEOUT_MS=180000 make -C src/aux test-libc
WASM_RUNNER="$PWD/build/cli-rt/wasm-sanitize"`; the longer budget accommodates
the existing binary128 formatting cases.
`clocks.wast` checks `clock_gettime` for realtime and monotonic, including
progression across a yielding sleep, the 16-byte guest timespec, errno,
canaries and complete-range rejection without a partial timestamp write.
Only CLOCK_REALTIME and CLOCK_MONOTONIC are exposed; CPU clocks, setters and
POSIX timer creation are not provided. Missing runtime clock callbacks return
ENOSYS rather than a synthetic epoch value.
`vsnprintf.wast` checks the existing public shared `vsnprintf`/`vsprintf`
providers: return counts, truncation, exact fit, one-byte/zero-size buffers,
empty formatting, mixed wide/promoted arguments, repeated argument areas and
canaries. Compiler-generated C/C++ `va_list` calls are checked separately by
`make -C src/aux test-stdio`; both clients use the installed provider.
These checks cover the existing integer/string formatter subset, not every
printf conversion or flag.
`reentrant-accounts.wast` checks `getpwnam_r`/`getpwuid_r` against the same
single guest record used by `getpwnam`/`getpwuid`. All five strings are copied
into caller storage, with a complete size check before writing the record or
buffer. Independent results survive later lookups and account changes. Missing
accounts return zero and a null result; insufficient capacity returns ERANGE
and a null result. Both preserve errno. Null required pointers return EFAULT
as a guest extension; other invalid pointers remain ordinary invalid C inputs.
Null optional strings in a configured record are copied as empty strings.
Reentrant calls do not consume the enumeration cursor or allocate storage.
This provides the existing guest account database, without host NSS lookup,
`/etc/passwd` parsing, or new account enumeration semantics.
The public `sysconf` queries the engine's actual combined argv/environment
string budget for `_SC_ARG_MAX` through `waste_get_exec_limits`; the same
public query also returns argument and environment entry limits. These come
from src/config.h and are enforced before exec image commitment, including
interpreter expansion. Counts exclude NULL sentinels; bytes include string NULs
and exclude pathname and pointer storage. Public `sysconf` also returns the
existing Wasm page size for `_SC_PAGESIZE`
and its `_SC_PAGE_SIZE` alias. `_SC_GETPW_R_SIZE_MAX` returns -1 without changing
errno because configured strings have no fixed bound. Other selectors return
-1/EINVAL. See the POSIX contracts for
[account lookup](https://pubs.opengroup.org/onlinepubs/9699919799/functions/getpwnam.html)
and [sysconf](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sysconf.html).
`make -C src/aux test-accounts` checks compiled public C++ calls and
their layouts separately; it does not execute the full LLVM Path backend.
`wide-format.wast` checks the separate bounded `swprintf`/`vswprintf` providers:
integer flags/lengths, pointer/count conversions, wide strings/characters,
UTF-8 strings, precision/width, variadic alignment, counts, canaries and errors.
All eight floating conversions (`f/F/e/E/g/G/a/A`) use exact binary64/128 bits
and integer decimal/hex digits with nearest/ties-to-even rounding. `%Lf` never
narrows through double; range, subnormal and low-fraction-bit checks cover it.
Capacity counts wchar elements including the NUL; insufficient/zero capacity
returns -1 with EOVERFLOW. A nonzero valid destination receives a terminated
prefix even on a formatting error. Invalid formats/positional arguments return
EINVAL; invalid UTF-8 returns EILSEQ. Success preserves errno. Wide elements
are copied completely, and UTF-8 precision counts output wchar elements.
There is no alternate locale, grouping or stateful encoding. The narrow
formatter's earlier floating-output limitation remains. Compiled C/C++ varargs
checks run with `make -C src/aux test-wide-format`.
`integer-conversion.wast` checks all twelve shared integer converters: 32-bit
`strtol`/`strtoul` and 64-bit `strtoll`/`strtoull`/`strtoimax`/`strtoumax`,
and their corresponding `wcsto*` APIs. Each ASCII vector runs through both
byte and wide input with shared expectations. Wide-only vectors check all six
APIs against non-ASCII characters, signed wchar extremes and misleading low
bytes. Wide end pointers advance by complete four-byte elements; input is
never narrowed or copied by the provider. They accept all six C whitespace
characters, bases 2 through 36 and octal/hex auto-detection. A hex prefix is
consumed only when followed by a hex digit;
binary prefixes are not recognized. No conversion leaves the end pointer at
the original input and preserves errno. Invalid bases return zero with EINVAL.
Overflow saturates at the signed/unsigned limit with ERANGE and continues
consuming digits. In-range negative unsigned inputs wrap at the return width;
negative unsigned overflow still returns the maximum. Successful conversion
preserves errno. The SDK declarations have C linkage, including `<inttypes.h>`.
`floating-conversion.wast` checks all six `strtof`/`strtod`/`strtold` and
`wcstof`/`wcstod`/`wcstold` providers. One C-locale scanner handles ASCII
whitespace, decimal/hex input, valid exponent suffixes, case-insensitive
infinity and quiet NaN (accepted payloads map to a canonical value). Failed
conversion leaves the end pointer at the original input and returns positive
zero. Wide input keeps full wchar values and element end offsets. Bounded
integer ratios round directly to binary32/64/128 with nearest, ties-to-even;
there is no intermediate double or dependence on the approximate quad
arithmetic helpers. Overflow returns signed infinity with ERANGE; inexact
tiny values set ERANGE, including rounding to signed zero. Exact subnormals
and other successes preserve errno. Fixed storage derives from the guest
float format and a retained prefix/sticky tail bounds arbitrarily long input.
Trailing prefix zeros remain in the scale rather than enlarging the ratio.
The WAST checks exact bits, rounding boundaries, full binary128 range,
exponent saturation, non-ASCII wide input, unchanged buffers and canaries.
Compiled C++ calls and return layouts are checked by `test-wide`.
`quad-comparison.wast` checks all seven binary128 compiler comparison helpers
against signed zeros, adjacent normal/subnormal values, full-range finite
values, infinities and NaNs (including low-word-only payloads). Comparison now
inspects both 64-bit words without narrowing. The ordered/NaN return values
match compiler-rt's Wasm ABI; the existing quad arithmetic/conversion helpers
still use double internally and do not provide general binary128 precision.
The public `math.h` classification macros preserve float/double/long-double
types and evaluate the input once; `make -C src/aux test-classification`
checks actual C and libc++ `<cmath>` calls, with all assertions in WAST.
`wide-memory.wast` checks signed four-byte wide-element comparison, copy,
overlapping move, full-element fill and memory/string searches. Counts are
elements; zero counts perform no reads or writes. String operations stop at
NUL while memory operations preserve and search embedded NULs. Reverse, set
and substring search return the first/last corresponding element pointer or
null. Comparisons avoid subtraction overflow and use signed guest wchar order.
The public `wchar.h`/`wctype.h` retain C linkage beneath libc++ wrappers;
`make -C src/aux test-wide` verifies compiled C++ calls with WAST.
Wide stream formatted IO remains unavailable, as do the previously annotated
unsupported classification functions. Bounded wide string formatting is
provided by `wide-format.c`.
Kernel/POSIX interaction
probes live in `src/vfs/root/test/system/libc`; run them with
`make -C src/aux test-libc-system`. The CLI's `libc-native` and `libc-sanitize`
compatibility targets run both sets, with the latter selecting ASan/UBSan.

Refresh browser snapshots with `make -C src/html-rt vfs-tests-install`, then
rebuild `bash.html`. From its Bash prompt, run:

```sh
status=0
for test in /root/test/aux/libc/*.wast; do
  /bin/wast --verbose "$test" || status=1
done
printf 'libc test status: %s\n' "$status"
```

`--verbose` prints dots for passing results, `F` for failures, totals and the
first failure. Exit status is zero only when the test succeeds. Reports can
be redirected into `/tmp` and downloaded. These fixtures also run directly with `build/cli-rt/wast --verbose FILE`
or `wast --suite`, which supply the installed production providers in an
isolated runtime context.
Native Make tests stage the authored inputs directly into `/tmp`, and save
commands/results under `build/aux/libc/tests/run-*/`. `WASM_RUNNER` selects a
public `wasm` or `wasm-sanitize` frontend; `BUILD_DIR` overrides the aux output root.

Production builds need Make, Clang and `wasm-ld`; installation also needs
Python and `flock`. Tests additionally build/use the public native `wasm` frontend.
`tools/shared_libc.py` preserves exact signatures while assigning imports,
and also serves Bash, Coreutils and ncurses. SDK/sysroot checks live under
`src/aux/libc/test/sdk`. The retired `libc-fixtures` and `libc-sysroot`
targets have no replacement inside this package.

`runtime/` remains private freestanding interpreter support, operating on host
pointers independently of guest APIs. `waste-crt.c` supplies guest startup;
it binds executable stdin/stdout/stderr before invoking C++ constructors and
main, using the already initialized process-local shared libc. Both main
signatures, constructor I/O/sleep and actual C++ archive allocation have a
[system startup fixture](../../system-tests/clang/startup/README.md).
`include/helper.h` remains private. `make -C src/aux/libc` is a convenience
entrypoint for the same package rules.

Public `link`/`linkat` and `symlink`/`symlinkat` use shared engine providers.
Hard-link inode ownership and component traversal are kernel semantics. Guest
stat/lstat/fstatat use `waste_kernel.fstatat_v2`, retaining the public 128-byte
stat layout while reporting namespace link counts. The older 48-byte metadata
boundary remains compatible. See the [hard-link system tests](../../system-tests/clang/hardlink/README.md)
for compiled C++ calls, WAST behavior and the remaining clone/reap namespace limits.

`usleep` delegates to the existing yielding nanosleep/pselect deadline path.
Zero has no effect, intervals below one million microseconds preserve errno
on success, and larger unsigned arguments return EINVAL. A caught signal
returns EINTR. Longer sleeps use nanosleep. Package `usleep.wast` checks live
progression; the [timed-lock system fixture](../../system-tests/clang/timed-locks/README.md)
checks exact deadlines, interruption and actual LLVM retries in both runtimes.

Public `rusage` uses time64 `timeval` timestamps and wasm32 `long` counters.
CPU accounting is unavailable: `getrusage` returns -1/ENOSYS for SELF/CHILDREN
and -1/EINVAL for invalid selectors without accessing the output. The legacy
env adapter follows the same contract. Resource-limit storage remains a
compatibility facade, without kernel enforcement. `resource-usage.wast`
checks the unsupported/error boundary and untouched output.

Shared `_Exit` calls the existing engine `_exit` provider. The
[LLVM process system fixture](../../system-tests/clang/process/README.md)
checks actual C++ time/PID calls, immediate child exit statuses, signal and
descriptor cleanup. C/C++ exit registration/finalization now uses the
allocation-backed list in exit.c; see the
[shutdown fixture](../../system-tests/clang/shutdown/README.md).
Normal exit invokes callbacks before the kernel boundary. The allocator
bootstrap clears abandoned registrations after successful exec, while fork
inherits private state. Automatic C++ dlclose cleanup, concurrent registration
and buffered flush conformance remain separate acceptance work.

`execv` supplies the current public `environ` to the existing execve provider
and uses its exact path without PATH lookup. `wait` selects any child through
the existing waitpid adapter. The
[public process system fixture](../../system-tests/clang/exec-wait/README.md)
checks copied arguments/environment, PID and descriptor preservation, status
consumption, signals and invalid output recovery in native/browser Bash.

Public `setsid`, `getsid`, `getpgid` and `getpgrp` use versioned queries over
the existing engine process table. Successful queries preserve errno; missing
PIDs return ESRCH and group/session leaders cannot create another session.
`tcgetpgrp` now queries actual controlling-terminal ownership. Detachment keeps
inherited descriptors while returning ENOTTY for their terminal-control queries.
The [session system fixture](../../system-tests/clang/session/README.md) checks
identity across nested fork/exec and parent terminal preservation in both runtimes.

Public `alarm` calls `waste_kernel.alarm_v1` with unsigned seconds and an i64
remaining/error result, preserving UINT_MAX and errno on success. The shared
process kernel uses its existing monotonic clock and scheduler, queues SIGALRM
once, rounds remaining seconds up and preserves pending signals on cancellation.
Fork clears the child timer; exec preserves it. Native and browser delivery use
the same READ/waitpid and bounded opcode signal paths. A clockless private
sandbox reports ENOSYS; production runtimes always install a clock.
See the [whole LLVM Watchdog fixture](../../system-tests/clang/watchdog/README.md).

Public `sigaction` uses the fixed 24-byte `waste_kernel.sigaction_v1` adapter
and returns -1/errno on failure. Unsupported flags and SIG_ERR handlers return
EINVAL before output/state changes; bad ranges return EFAULT. Ordinary C
handlers use the shared function table and SA_RESTART controls restartable reads.
`signal` now installs a persistent restartable action and returns its predecessor.
Legacy raw WAT keeps its 20-byte prefix and -2 ignored sentinel; the public
adapter exposes SIG_IGN as one. Signal-info callbacks and alternate stacks
remain unavailable. The [LLVM signal fixture](../../system-tests/clang/signals/README.md)
checks registration, actual LLVM callbacks/cleanup and forked-child termination.

Public `waste_waitpid_until` takes an absolute monotonic nanosecond deadline
through `waste_kernel.waitpid_until_v1`. It shares ordinary process scheduling,
status validation and signal callbacks; expiry returns zero without reaping or
killing the child. Caught signals obey SA_RESTART without extending the deadline,
and the independent alarm is preserved. See the
[shared child-deadline tests](../../system-tests/clang/child-deadline/README.md).

Public `modf` splits binary64 values using the existing exact truncation and
sign primitives. Both results preserve the input sign, including zero and
infinity; NaNs remain NaNs and errno is unchanged. Its 20 package WAST
assertions check fractional/integral boundaries, subnormals, special values
and output canaries. The real LLVM JSON writer uses this provider; this does
not add general narrow floating printf or binary128 math support.

Narrow floating printf now shares the wide formatter's exact IEEE-to-decimal
and hexadecimal conversion through the private `include/format.h` interface.
The f/F, e/E, g/G and a/A directives support double and binary128 arguments,
ties-to-even, signs, alternate form, width, precision and padding. Narrow
snprintf counts the complete result while retaining a bounded byte prefix;
wide swprintf keeps its insufficient-capacity error contract. Padding and
precision zero runs are counted directly after narrow storage fills, and
counts or parsed fields beyond INT_MAX report EOVERFLOW. Package
`narrow-floating-format.wast` covers 51 cases, while the
[LLVM inlining fixture](../../system-tests/clang/inlining/README.md) checks
compiled mixed argument alignment and production four-digit statistics.
This extends floating formatting; unrelated printf extensions and alternate
locales still require separate acceptance.

## Portable binary64 math

GNU Awk's math builtins use the shared `exp`, `log`, `pow`, `sin`, `cos`,
`atan2` and `fmod` providers. Their C wrappers live in `llvm-math/`; the build
stages headers from the pinned, read-only `submodules/llvm-project/libc`
checkout under `build/aux/libc/llvm-math/source`. Clang++ compiles this private
C++ implementation without libc++, exceptions, RTTI or host headers. Consumers
still call the ordinary public C ABI. Installation preserves LLVM's license
under `/usr/share/licenses/libc/llvm-math`, and Bash/Coreutils/Awk source bundles
include the required upstream math inputs.

This profile selects LLVM's fast pass, fixed nearest-even rounding and errno
reporting, without floating-point exception emulation or a correctly rounded
transcendental guarantee. No extra math service or host math imports are used.
`src/vfs/root/test/aux/libc/awk-math.wast` checks ordinary results, large trig
arguments, signs of zero, nonfinite inputs and domain/range errno through the
installed shared provider. The public `mbrlen` declaration exposes the existing
shared UTF-8 provider; it does not add a second package implementation.

## Opaque stream state

`ungetc` preserves one unsigned byte of pushback without modifying the
backing input. `fgetc`, `fread` and `fgets` consume that byte first; successful
seeks and purges discard it, and successful pushback clears EOF.
Failed seekability probes on pipes preserve the stream's error indicator.
`__freadahead`, `__freading`, `__fwriting` and `__fpending` expose the current
stream state without exporting the private FILE layout. Read-ahead counts the
unconsumed input buffer; the direction queries report the current read/write
flags. Current public streams use fixed directions, rather than full update
mode semantics. Pending output is zero because descriptor and memory writes
commit immediately. These read-only queries preserve errno and return zero
for null streams. `stdio-state.wast` checks direction, consumption, seek/purge,
output and errno behavior against the installed provider. GNU M4's Gnulib
support uses these interfaces instead of a host FILE representation.
