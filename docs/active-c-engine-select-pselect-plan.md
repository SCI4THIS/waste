# C Engine `select`/`pselect` Full-Fix Plan

This is an active implementation plan.  Durable ownership and runtime
boundaries are defined in [architecture.md](architecture.md), and reusable
implementation practices are collected in [techniques.md](techniques.md).

## Purpose

This plan replaces the guest-libc readiness shortcut with engine-owned
`select` and `pselect` semantics.  The completed work must fix the
`libc-test/environment-boundaries.wast` mismatch without teaching the engine
about that fixture's input shape, and it must preserve the C-engine Bash page's
ability to wait for terminal input without blocking its worker.

This is POSIX-runtime work, not a WAT/WAST parser or WebAssembly executor
conformance issue.  It is a focused part of the larger POSIX migration and must
follow the ownership rules in [architecture.md](architecture.md): descriptor
identity, readiness, wait state, timeouts, and signal state belong to the
engine.  JavaScript may deliver browser events and bytes, but it must not decide
POSIX results.

## Current Mismatch

The generated boundary fixture expects unavailable calls to return their
failure sentinel and set `errno` to `ENOSYS`.  In particular, it calls
`select(0, NULL, NULL, NULL, NULL)` and the corresponding `pselect` form and
expects `-1` plus `ENOSYS`.

The current implementations in `src/html-rt/lib/misc.c` instead return:

```c
return n > 0 && (r || w || x) ? 1 : 0;
```

This shortcut was introduced so that Bash would treat an input descriptor as
ready, enter `read`, and yield through the existing `posix_read` bridge when no
input was available.  With the boundary fixture's all-zero arguments it
returns `0`, so `filesystem-and-process-boundaries` returns `0` instead of
`1`.

The same generated fixture fails in the native C runner and the sequential
OCaml runner.  The browser is faithfully reporting behavior already compiled
into `waste-libc.wasm`; this is not a browser linker or C evaluator
differential.

Neither side of the current mismatch describes the desired final behavior.
Once readiness is implemented, `select` and `pselect` are no longer
unsupported calls.  Also, a call with no descriptors and no timeout waits
indefinitely; it must not return an immediate success merely to reach `read`.
The boundary fixture therefore has to change as part of the full fix.

## Required Semantics

The first complete implementation should cover the ABI used by the guest libc
and match the OCaml POSIX implementation wherever it exposes equivalent
behavior.  At minimum it must provide:

- independent read, write, and exceptional-condition interest sets;
- validation of `nfds`, descriptor numbers, guest pointers, and timeout
  fields;
- `EBADF` for a requested descriptor that is not open and `EINVAL` for invalid
  scalar or timeout arguments;
- clearing of non-ready bits and reporting the correct ready count;
- immediate return for a zero timeout;
- finite waits driven by an engine-owned monotonic deadline;
- indefinite waits without blocking the browser worker;
- wakeup when descriptor readiness changes;
- interruption by an unmasked signal with `EINTR`;
- atomic temporary signal-mask installation for `pselect` and restoration on
  every completion path;
- the guest ABI's required `select` timeout write-back behavior, if any, while
  leaving the `pselect` timeout unchanged; and
- deterministic unsupported errors for descriptor backends that cannot yet
  report readiness.

The implementation must derive `fd_set`, `timeval`, `timespec`, and signal-set
layout from the guest ABI.  Build-time assertions and fixture probes should
record their sizes, alignment, word order, `FD_SETSIZE`, and bit numbering;
these values must not be guessed from the build host's libc.

## Ownership and Interfaces

### Engine-owned state

Add a POSIX kernel object to each `native_store` or its eventual sandbox
replacement.  It owns:

- the descriptor table;
- shared open-file descriptions;
- terminal input and output state;
- pipe buffers and endpoint state;
- pending readiness notifications for broker-backed descriptors;
- a monotonic clock view and timer queue;
- pending signals and each thread's active signal mask; and
- wait records for suspended execution contexts.

No mutable POSIX state may be global or shared between WAST files.  Explicitly
imported Wasm memory remains aliased within one sandbox, while independently
scheduled files receive separate kernels and descriptor tables.

### Descriptor readiness contract

Each open-file-description backend supplies a side-effect-free readiness
query returning a mask such as `READABLE`, `WRITABLE`, `EXCEPTIONAL`, `HANGUP`,
and `ERROR`.  Terminal, regular-file, directory, pipe, and optional broker
backends implement this contract in the engine:

- regular files are readable before EOF and writable when opened for writing;
- pipes account for buffered bytes, readers, writers, capacity, EOF, and
  broken-pipe conditions;
- the terminal is readable when the engine input queue can satisfy the
  terminal mode's rules and writable when its presentation sink can accept
  output;
- closed or invalid requested descriptors produce `EBADF`; and
- broker descriptors consume versioned readiness notifications but retain
  their descriptor identity and wait registration in the engine.

Readiness transitions notify the scheduler, which reevaluates affected wait
records.  JavaScript never receives an `fd_set` and never fabricates a ready
count.

### Guest-libc-to-kernel ABI

Replace the definitions in `misc.c` with thin wrappers around versioned,
internal kernel imports.  Use a namespace distinct from public POSIX symbols,
for example `waste_kernel.select_v1` and `waste_kernel.pselect_v1`, to avoid
recursive resolution back to libc.

The imported functions should use the guest POSIX call signature and return a
nonnegative result or a negative errno value.  The libc wrapper translates a
negative errno into `-1` and writes `*__errno_location()`.  This keeps errno in
guest memory and prevents host errno state from leaking between sandboxes.

The host-function invocation interface must identify the calling module
instance explicitly.  The kernel handler uses that instance's effective
linear memory for bounded pointer access.  It must not find memory through a
hard-coded registry name such as `waste-runtime`, because the same libc module
is also exercised as `waste-libc` and may be instantiated more than once.

Implement this with an explicit call context passed to host imports, or with a
per-instance binding object established during instantiation.  Do not use a
global “current engine” pointer: it would break nested imports, reentrancy, and
future parallel execution.

### Wait and continuation contract

Generalize `EXEC_YIELD` from “`posix_read` has no data” to “the active thread
has an engine-owned wait record.”  A wait record contains only stable handles
and copied values:

- waiting thread/process handle;
- wait kind;
- copied descriptor-interest bitsets;
- optional monotonic deadline;
- saved signal mask and whether it has been temporarily replaced;
- generation or cancellation token; and
- completion reason.

It must not retain raw pointers into Wasm memory or JavaScript objects.  On
resume, the imported operation is reentered, validates memory again, polls the
kernel, and either completes or yields again.  Existing operand, control, and
call-frame preservation remains responsible for restarting the import with
the original guest arguments.

Cancellation must remove the wait record and restore a temporary `pselect`
mask during process exit, trap unwinding, signal interruption, sandbox
destruction, and explicit stop.  A stale browser wakeup is ignored by checking
the generation token.

### Browser event ABI

Move terminal input ownership out of the JavaScript `posixRead` callback.
Expose bounded integer-handle operations such as:

- enqueue terminal input bytes into the active sandbox;
- notify a broker readiness generation;
- query the current wait kind and optional deadline; and
- resume the runnable engine context.

When the engine yields, the worker waits for terminal input, a broker message,
a control message, or a timer derived from the engine's deadline.  On an event
it transfers data or a notification into the engine and calls resume.  The
engine reevaluates readiness and determines the POSIX result.

The generated page remains a self-contained `file://` document.  Browser
timers, `postMessage`, and optional WebSocket messages are capability/event
sources only.  No server or cross-origin-isolated shared memory is required.

## Execution Flow

For both operations, the kernel handler follows this order:

1. Validate the scalar arguments and every non-null guest-memory range.
2. Copy the input sets and timeout into bounded engine storage.
3. For `pselect`, validate and install the temporary signal mask as one
   scheduler operation before testing pending signals and readiness.
4. Validate each requested descriptor and query its readiness mask.
5. If any requested conditions are ready, write the output sets, perform any
   specified timeout update, restore the `pselect` mask, and return the count.
6. If the timeout is zero, write empty output sets, restore the mask, and
   return zero.
7. If an unmasked signal is pending, restore the mask and return `-EINTR`.
8. Otherwise register a wait containing copied interests and an optional
   monotonic deadline, then return `EXEC_YIELD` without modifying guest output
   sets.
9. On wakeup, cancel the old registration and repeat validation and polling.
   A spurious wakeup is allowed to yield again.

The implementation should share parsing, polling, output-set construction,
and wait registration between `select` and `pselect`.  Their timeout layout,
timeout write-back, and signal-mask behavior remain explicit differences.

## Implementation Stages

### Stage 1: Freeze the ABI and add diagnostic tests — COMPLETE

- Add compile-time ABI assertions to the guest-libc build.
- Add native unit tests for set decoding/encoding and timeout validation.
- Split the existing aggregate boundary assertion so a regression identifies
  the exact call and errno rather than only returning one accumulated Boolean.
- Record the current C and OCaml results before changing behavior.

Gate: the new tests describe current failures precisely and do not alter the
generated libc module's behavior.

#### Completed work

**Guest ABI types** (`src/html-rt/lib/include/helper.h`): defined `waste_fd_set`
(128 bytes, 32 × u32 words, `FD_SETSIZE` = 1024), `waste_timeval` (16 bytes:
i64 tv_sec @ 0, i32 tv_usec @ 8), `waste_timespec` (16 bytes: i64 tv_sec @ 0,
i32 tv_nsec @ 8), and `waste_sigset_t` (16 bytes: 4 × u32).  Compile-time
`_Static_assert` guards verify sizes on every wasm32 libc build.  Exported
size-probe functions and a WAST fixture (`tests/libc-test/select-abi-client.wast.inc`)
verify the ABI at test time (6/6 pass).

**Engine-side decode/encode** (`src/engine/lib/include/select.h`, `src/engine/lib/select.c`): host-side
`posix_fd_set`, `posix_timeval`, `posix_timespec` types with little-endian
decode/encode from guest memory, bit-level fd_set operations (`posix_fd_zero`,
`posix_fd_isset`, `posix_fd_set_bit`, `posix_fd_clr`, `posix_fd_count`), and
validation for timeval, timespec, and nfds.

**Native unit tests** (`tests/posix-select-abi.c`): 1097 tests under
ASan/UBSan covering fd_set bit operations, decode/encode round-trips, timeval
and timespec decode/validation, nfds validation, and NULL-argument handling.
Built via `make -C src/cli-rt posix-select-abi`.

**Split boundary fixture** (`tests/libc-test/environment-boundaries-client.wast.inc`):
19 individual exported functions with per-call `assert_return` replacing the
single aggregate `filesystem-and-process-boundaries` chain.

#### Baseline results (2026-09-17)

C engine (`waste-cli`): originally 17/19 pass.  `boundary-select` and
`boundary-pselect` failed — both returned 0 (the "always ready" shortcut
returns 0 for nfds=0 with all-null sets) instead of -1 with errno=ENOSYS.
The boundary fixture was updated to expect 0 (readiness-reporting semantics:
no descriptors ready) and now all 19 pass.

OCaml sequential interpreter: same behavior — the libc binary is identical
in both runners.  After the fixture update, all 19 pass under both runners.

All other libc fixture suites (allocator, accounts, entropy-messages,
locale-wide, matching-sort, memory-conversion, stdio, terminal,
time-resource, select-abi) pass 100% under both runners.

### Stage 2: Introduce the sandbox kernel and descriptor readiness API — COMPLETE

- Add kernel lifetime to `native_store` with deterministic initialization and
  teardown.
- Initialize descriptors 0, 1, and 2 as terminal open-file descriptions for
  interactive launches; use an explicit noninteractive policy for ordinary
  WAST sandboxes.
- Implement readiness queries for terminals and the descriptor types already
  present in the C runtime.
- Add native tests for invalid descriptors, duplicated descriptors, EOF,
  terminal input, and pipe state transitions.

Gate: sanitizer-clean native tests demonstrate that separate stores cannot
observe one another's descriptors or readiness state.

#### Completed work

**Kernel types and API** (`src/engine/lib/include/kernel.h`): defined the
per-sandbox kernel with a 64-slot descriptor table (`POSIX_KERNEL_FD_MAX`),
open-file-description (OFD) objects with reference counting, three OFD kinds
(terminal, pipe-read, pipe-write), a shared pipe buffer struct, readiness mask
bits (`POSIX_POLL_IN`, `POSIX_POLL_OUT`, `POSIX_POLL_ERR`, `POSIX_POLL_HUP`),
and portable errno constants (`POSIX_EBADF`, `POSIX_ENOMEM`, `POSIX_EAGAIN`,
`POSIX_EINVAL`, `POSIX_EMFILE`, `POSIX_EPIPE`).  Full public API: lifecycle
(`posix_kernel_create`, `posix_kernel_destroy`), readiness
(`posix_kernel_query_readiness`), terminal input (`posix_kernel_terminal_enqueue`,
`posix_kernel_terminal_signal_eof`), pipe (`posix_kernel_pipe`), descriptor ops
(`posix_kernel_close`, `posix_kernel_dup`, `posix_kernel_dup2`), and non-blocking
I/O (`posix_kernel_read`, `posix_kernel_write`).

**Kernel implementation** (`src/engine/lib/kernel.c`): ~220 lines covering OFD
allocation with ref counting (`ofd_alloc`, `ofd_release`, `pipe_count_dec`),
interactive/noninteractive kernel creation (interactive mode opens fds 0,1,2 as
a shared terminal OFD with ref_count=3), terminal enqueue/EOF with bounded input
buffer, pipe creation with shared `posix_pipe` buffer (4096 bytes), close with
proper OFD ref counting and pipe reader/writer count tracking, dup/dup2 with ref
count and pipe endpoint increments, and non-blocking read/write for terminal and
pipe backends.  All functions follow the negative-errno return convention.

**Store integration** (`src/engine/store.h`, `src/engine/store.c`): forward
declaration of `struct posix_kernel` and opaque pointer in `native_store`.
`native_store_init` creates a noninteractive kernel (all fds closed) by default;
`native_store_free` destroys it.  The kernel header is included only in
`store.c`, keeping the kernel's internal types out of the store's public
interface.

**Native test suite** (`tests/posix-kernel.c`): 240 tests under ASan/UBSan
covering 13 test functions: lifecycle (interactive/noninteractive/NULL destroy),
invalid fd handling (-1, FD_MAX, closed, NULL kernel), terminal readiness
(initial state, enqueue→readable, read→drain, EOF→hangup, write always succeeds),
pipe readiness (creation, write→readable, read→drain, EAGAIN on empty), pipe
close transitions (close write→hangup on read, close read→ERR on write, EPIPE),
pipe full (fill to capacity, EAGAIN, partial read restores writability), dup
(shares OFD, enqueue visible on both, close one leaves other), dup2 (overwrites
target, same-fd no-op), pipe dup readiness (dup writer, close original→no
hangup, close dup→hangup), close (partial close of shared OFD, double close,
invalid), cross-kernel isolation (enqueue/pipe/close in one kernel doesn't affect
another), edge cases (read/write closed fd, NULL buf, zero count, wrong
direction), and fd exhaustion (fill all 64 slots, EMFILE, close→reuse).
Built via `make -C src/cli-rt posix-kernel`.

#### Baseline results (2026-09-17)

Native kernel tests: 240/240 pass under ASan/UBSan with no memory errors.
The posix-select-abi suite (1097 tests) continues to pass unchanged.

The boundary fixture (`environment-boundaries-client.wast.inc`) was updated in
Stage 1 follow-up: `boundary-select` and `boundary-pselect` now expect 0 (no
descriptors ready) rather than -1/ENOSYS, matching the readiness-reporting
semantics.  All 19 boundary tests pass under both C engine and OCaml runners.

### Stage 3: Make host imports instance-aware

- Extend the host-call binding/invocation interface with an explicit caller
  instance or per-instance binding.
- Replace `native_posix_memory`'s registry-name lookup with caller-memory
  access.
- Exercise direct imports, imports reached through another Wasm module, shared
  imported memory, nested calls, and two instances of the same decoded module.

Gate: all existing core/linker tests and the libc suite still pass except for
the recorded boundary mismatch; AddressSanitizer and UndefinedBehaviorSanitizer
remain clean with the repository's documented LeakSanitizer exception.

### Stage 4: Add synchronous `select`/`pselect`

- Add the internal versioned imports and thin libc wrappers.
- Implement argument, set, descriptor, and timeout validation.
- Implement immediate-ready and zero-timeout results, including output-set
  updates and errno translation.
- Share the core operation between `select` and `pselect` while retaining
  their ABI differences.

Gate: native tests pass for multiple sets, no ready descriptors, duplicated
interest across sets, `EBADF`, `EINVAL`, zero timeout, and memory bounds.

### Stage 5: Generalize yield/resume for descriptor waits

- Add explicit wait records and wait reasons to the executor/scheduler.
- Register and cancel readiness watchers without retaining guest pointers.
- Add monotonic deadlines and spurious-wakeup handling.
- Preserve wait state through nested Wasm-to-Wasm-to-host calls.
- Ensure traps, exits, and sandbox teardown cannot leave registrations behind.

Gate: native deterministic-clock tests cover ready-before-wait, readiness after
yield, timeout, cancellation, repeated yield, and teardown.  The evaluator's
existing `posix_read` test continues to pass during the transition.

### Stage 6: Add signals and complete `pselect`

- Connect pending signals and per-thread masks to readiness waits.
- Install the supplied `pselect` mask atomically with the readiness check.
- Restore the original mask on readiness, timeout, interruption, error, exit,
  and cancellation.
- Return `EINTR` only when the wait is actually interrupted according to the
  selected oracle behavior.

Gate: deterministic tests cover pending-before-call, signal-after-yield,
blocked signals, simultaneous signal/readiness, and mask restoration on every
path.

### Stage 7: Replace the browser transport shortcut

- Add terminal-input and wait-state exports to `browser_api.c`.
- Update the C-engine Bash HTML worker to enqueue input into the kernel and
  resume based on the reported wait condition.
- Retire JavaScript's `-2` pseudo-result and the libc “always ready” shortcut
  after no caller depends on them.
- Keep output presentation and browser timers as narrow host capabilities.

Gate: the self-contained Bash page starts, reaches a prompt, waits without a
busy loop, accepts multiple commands, handles EOF and interruption, and exits.
Its existing 5/5 smoke gate must remain green.

### Stage 8: Correct the libc fixtures and differential gates

- Remove `select` and `pselect` from the list of operations expected to return
  `ENOSYS` in `environment-boundaries-client.wast.inc`.
- Retain explicit `ENOSYS` checks for truly unavailable process, pathname,
  loader, and network operations.
- Add focused generated libc fixtures for immediate readiness, zero timeout,
  invalid descriptors, finite timeout, indefinite wait/wakeup, and `pselect`
  signal behavior.
- For synchronous wrapper tests, generate a small `waste_kernel` provider
  module so the same libc binary can run under the OCaml WAST oracle.  Keep
  C-engine integration fixtures unprovided so they exercise the real host
  resolver and kernel.
- Compare results with the OCaml implementation where it supports the same
  descriptor backend.  Record intentional differences where browser
  capabilities prevent an equivalent operation.

Gate: sequential and threaded libc tests pass, the native C runner passes the
new kernel integration fixtures, and the regenerated browser dashboard has no
`environment-boundaries.wast` mismatch.

## Likely File Boundaries

The names may be adjusted to the current refactor, but responsibilities should
remain separated:

- `src/engine/lib/include/kernel.h` and `src/engine/lib/kernel.c`: per-sandbox
  kernel lifetime, descriptor table, OFDs, readiness, terminal, pipe, I/O;
- `src/engine/lib/include/select.h` and `src/engine/lib/select.c`: guest ABI
  decoding (fd_set, timeval, timespec) and shared select logic;
- future `src/engine/lib/include/signal.h`: masks, pending signals, and
  interruption;
- `src/html-rt/posix_stubs.c`: narrow host-import adapter only;
- `src/html-rt/browser_api.c`: integer-handle event and resume ABI;
- `src/html-rt/lib/misc.c`: errno-translating libc wrappers only;
- `src/html-rt/tools/generate-c-engine-bash-html.py`: worker event plumbing;
  and
- `tests/libc-test/` plus focused native tests: behavioral coverage.

Do not put `fd_set` parsing, timeout accounting, or readiness policy in the
Flex/Bison parser, JavaScript generator, or general opcode executor.

## Verification Matrix

Each stage should run the smallest relevant tests first, followed by these
release gates:

- native C unit tests with warnings as errors, AddressSanitizer, and
  UndefinedBehaviorSanitizer;
- `./start.sh --cli-test` to preserve all 97 core WAST files;
- targeted sequential and threaded `environment-boundaries.wast` and new libc
  fixtures through `tests/libc-test/libc-runtime.cjs`;
- the full sequential and threaded libc suites;
- DIY POSIX descriptor, signal, and scheduler probes;
- regeneration and execution of the offline C-engine browser dashboard;
- regeneration and the 5/5 C-engine Bash browser smoke test; and
- `bash -n start.sh`, bytecode checks for changed Python tools, and
  `git diff --check`.

Add optional `WASTE_PROFILE` counters for readiness polls, registered waits,
spurious wakeups, timeout wakeups, signal wakeups, and browser resumes.  They
must remain absent from the normal hot path when profiling is disabled.

## Completion Criteria

The work is complete when:

1. libc contains no unconditional readiness result for `select` or `pselect`;
2. the engine, rather than JavaScript, owns descriptor and wait state;
3. no blocked operation retains a guest pointer across a yield;
4. readiness, timeout, cancellation, and `pselect` signal-mask behavior have
   deterministic native tests;
5. the environment-boundary fixture tests only genuinely unsupported calls;
6. native C, OCaml differential, sequential/threaded libc, browser dashboard,
   and Bash smoke gates have no unexplained mismatch; and
7. the browser artifacts remain self-contained and usable through `file://`.

## Explicit Non-Goals

This slice does not require sockets, persistent storage, full terminal line
discipline, or page-granular `fork` copy-on-write.  Their descriptor backends
must integrate through the same readiness contract later.  It also does not
justify special-casing the current boundary fixture, returning every requested
descriptor as ready, busy polling from Wasm or JavaScript, or moving kernel
state into the optional broker.
