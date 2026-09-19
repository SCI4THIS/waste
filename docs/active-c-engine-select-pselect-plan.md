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

The historical implementations in `src/html-rt/lib/sys/select.c` instead
returned:

```c
return n > 0 && (r || w || x) ? 1 : 0;
```

This shortcut was introduced so that Bash would treat an input descriptor as
ready, enter `read`, and yield through the existing `posix_read` bridge when no
input was available.  With the boundary fixture's all-zero arguments it
returns `0`, so `filesystem-and-process-boundaries` returns `0` instead of
`1`.

The same generated fixture failed in the native C runner and the sequential
OCaml runner.  The browser was faithfully reporting behavior already compiled
into `waste-libc.wasm`; this was not a browser linker or C evaluator
differential.  Stage 4 now routes these calls through the engine-owned kernel;
the fixture's all-null, zero-descriptor case remains an immediate zero-result
poll by design.

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

### Stage 3: Make host imports instance-aware — COMPLETE

- Extend the host-call binding/invocation interface with an explicit caller
  instance or per-instance binding.
- Replace `native_posix_memory`'s registry-name lookup with caller-memory
  access.
- Exercise direct imports, imports reached through another Wasm module, shared
  imported memory, nested calls, and two instances of the same decoded module.

Gate: all existing core/linker tests and the libc suite still pass except for
the recorded boundary mismatch; AddressSanitizer and UndefinedBehaviorSanitizer
remain clean with the repository's documented LeakSanitizer exception.

#### Completed work

**exec_host_func signature change** (`src/engine/engine_internal.h`): added a
trailing `const waste_exec_engine *caller` parameter to the `exec_host_func`
function pointer typedef.  The invocation site in `exec_invoke`
(`src/engine/op/execute.c`) passes the calling engine as the last argument.
Every host function now receives the engine that invoked it, enabling direct
access to the caller's memory, tables, and globals without registry lookups.

**POSIX stubs updated** (`src/html-rt/posix_stubs.c`): `native_posix_memory`
now reads `caller->memory` directly instead of searching the store for a
module named `"waste-runtime"`.  This eliminates the fragile name-based lookup
that broke when libc was instantiated as `"waste-libc"` or any name other than
`"waste-runtime"`.  All 13 POSIX host functions updated to accept the `caller`
parameter; those that access guest memory (`open`, `read`, `write`, `getcwd`,
`stat`) use it for memory access.  `getcwd` retains the store in `host_data`
for its `malloc` call through the `env` module.

**All host function implementations updated:**
- `src/engine/store.c`: `native_linked_call`, `native_spectest_noop`
- `src/html-rt/browser_api.c`: `linked_call`, `spectest_noop`
- `src/html-rt/posix_stubs.c`: all 13 POSIX stubs
- `tests/c-engine-i32-smoke.c`: `host_add`

**Caller-instance test suite** (`tests/c-engine-caller-instance.c`): 28 tests
under ASan/UBSan covering:
- Direct host call from module A → host sees A's memory (initial data and
  store/read round-trip)
- Direct host call from module B → host sees B's memory (different initial
  data)
- Nested call: B calls A's export, A calls host → host sees A's memory (not
  B's), and only A's memory is modified
- Two instances of the same decoded module: each has independent memory,
  stores to one are invisible to the other
- Memory object and data pointer isolation between instances

Test WAT modules: `tests/c-engine-caller-a.wat` (memory with `\de\ad\be\ef`
data, imports `host.read_caller_mem`) and `tests/c-engine-caller-b.wat`
(memory with `\ca\fe\ba\be` data, imports both host function and A's export).
Built via `make -C src/cli-rt caller-instance`.

**Build system** (`src/cli-rt/Makefile`): added `kernel.c` to ENGINE_SOURCES
(required because `store.c` now depends on `posix_kernel_create`/`destroy`);
added `caller-instance` target with wasm-as compilation of both WAT modules
and ASan/UBSan-linked test binary.

#### Baseline results (2026-09-18)

All native test suites pass under ASan/UBSan:
- caller-instance: 28/28
- posix-kernel: 240/240
- posix-select-abi: 1097/1097
- i32-smoke: pass (direct, import, extern-alias, decoded-module-instance,
  public-API)
- wasm-binary-primitives: pass
- wasm-import-decode: pass
- wasm-validation: pass
- parser-reentrant: pass (concurrent isolation)

### Stage 4: Add synchronous `select`/`pselect` — COMPLETE

- Add the internal versioned imports and thin libc wrappers.
- Implement argument, set, descriptor, and timeout validation.
- Implement immediate-ready and zero-timeout results, including output-set
  updates and errno translation.
- Share the core operation between `select` and `pselect` while retaining
  their ABI differences.

Gate: native tests pass for multiple sets, no ready descriptors, duplicated
interest across sets, `EBADF`, `EINVAL`, zero timeout, and memory bounds.

#### Completed work — 2026-09-19

**Versioned guest ABI imports** (`src/html-rt/lib/sys/select.c`): replaced the
placeholder readiness return values with `waste_kernel.select_v1` and
`waste_kernel.pselect_v1` imports.  The wrappers retain the POSIX signatures,
translate negative engine errno values to `-1`, and write the translated value
to the guest `errno` location.  The generated libc module now contains the
explicit `waste_kernel` imports rather than recursively resolving public
`select` symbols.

**Kernel operation** (`src/engine/lib/kernel.c`): added one shared polling
implementation for both operations.  It validates `nfds`, timeout ranges, and
requested descriptors; scans independent read/write/exception sets; reports
terminal and pipe readiness; clears non-ready output bits; counts ready bits;
returns immediately for zero timeouts; and reports `-EAGAIN` after registering
an engine-owned wait when a non-zero or indefinite wait is required.  `pselect`
keeps its timespec and signal-mask ABI; signal-mask semantics are completed in
Stage 6 below.

**Instance-aware host bridge** (`src/html-rt/posix_stubs.c`): decodes and
re-encodes fd sets and timeout structures through the invoking instance's
linear memory, checks every non-null range before access, validates the
optional pselect signal-set range, and dispatches only the versioned internal
imports.  The HTML runtime Makefile now links the kernel and ABI codec objects
into the browser engine.

**Native and build gates:** `make -C src/cli-rt posix-select` passes 86/86
ASan/UBSan tests covering multiple sets, duplicated interest, descriptor
errors, invalid timeouts, zero/non-zero waits, pipes, EOF, and output-set
construction.  `make -C src/html-rt wast-browser` links the browser runtime,
and the generated `waste-libc.wasm` contains both versioned imports.  The
documented LeakSanitizer-disabled sanitizer setting remains in effect.

### Stage 5: Generalize yield/resume for descriptor waits — COMPLETE

- Add explicit wait records and wait reasons to the executor/scheduler.
- Register and cancel readiness watchers without retaining guest pointers.
- Add monotonic deadlines and spurious-wakeup handling.
- Preserve wait state through nested Wasm-to-Wasm-to-host calls.
- Ensure traps, exits, and sandbox teardown cannot leave registrations behind.

Gate: native deterministic-clock tests cover ready-before-wait, readiness after
yield, timeout, cancellation, repeated yield, and teardown.  The evaluator's
existing `posix_read` test continues to pass during the transition.

#### Completed work — 2026-09-19

**Pointer-free wait records** (`src/engine/lib/include/kernel.h` and
`src/engine/lib/kernel.c`): each sandbox kernel now owns one copied wait
record containing the descriptor sets, `nfds`, generation token, and optional
monotonic deadline.  It never retains guest-memory pointers.  Readiness is
rechecked through the kernel's descriptor table, and timeout polling is driven
by an injectable monotonic clock.  Explicit cancellation clears the record;
kernel destruction tears it down with the rest of the sandbox.

**Select/pselect continuation:** a blocked synchronous operation registers or
reuses the wait record and returns `-POSIX_EAGAIN`.  The host adapter marks the
yield reason as `EXEC_YIELD_SELECT` and returns `EXEC_YIELD`, allowing the
existing executor frame preservation and `waste_wast_resume` path to re-enter
the import.  A readiness transition completes the operation and cancels the
record; an expired finite deadline clears output sets and returns zero.  A
repeated poll retains its generation token, so stale scheduler notifications
cannot identify it as a new wait.  Existing no-input `posix_read` yields are
classified as `EXEC_YIELD_READ` and remain compatible with the same machinery.

**Deterministic gate** (`tests/posix-wait.c`): sanitizer-covered tests verify
ready-before-wait, readiness after a registered yield, finite timeout expiry,
explicit cancellation, repeated-yield generation stability, and teardown.
The `posix-wait` Make target runs these alongside the existing select, kernel,
and ABI suites.  The HTML runtime also rebuilds with the updated wait-aware
kernel and host adapter.

Native results: `posix-wait` 21/21, `posix-select` 86/86, `posix-kernel`
240/240, and `posix-select-abi` 1097/1097 under ASan/UBSan with leak
detection disabled per the repository environment.  `make -C src/html-rt
wast-browser` links successfully.

### Stage 6: Add signals and complete `pselect` — COMPLETE

- Connect pending signals and per-thread masks to readiness waits.
- Install the supplied `pselect` mask atomically with the readiness check.
- Restore the original mask on readiness, timeout, interruption, error, exit,
  and cancellation.
- Return `EINTR` only when the wait is actually interrupted according to the
  selected oracle behavior.

Gate: deterministic tests cover pending-before-call, signal-after-yield,
blocked signals, simultaneous signal/readiness, and mask restoration on every
path.

#### Completed work — 2026-09-19

**Per-sandbox signal state** (`src/engine/lib/include/kernel.h` and
`src/engine/lib/kernel.c`): each kernel now owns a 128-bit pending-signal set
and active signal mask.  Signals are raised, queried, and consumed through
bounded engine APIs; no signal state is global.  Unmasked pending signals are
reported as `POSIX_WAIT_SIGNAL` and produce `-EINTR`, while masked signals stay
pending.

**Atomic `pselect` mask handling:** the kernel decodes the guest's fixed
16-byte `sigset_t`, saves the current mask, installs the temporary mask before
the readiness/pending-signal check, and restores it on readiness, timeout,
`EINTR`, validation errors, explicit cancellation, and sandbox teardown.
Blocked waits retain the copied mask in the pointer-free wait record, so a
signal delivered after yield is evaluated with the same mask.  `select` uses
the kernel's persistent mask and is interrupted by an unmasked pending signal.
When readiness and a signal arrive together, the pending signal is handled
first and the wait returns `-EINTR`.

**Host ABI:** `native_posix_pselect` now decodes and bounds-checks the optional
signal set before passing it to the kernel.  `POSIX_EINTR` and the signal-set
codec are part of the platform-independent POSIX ABI; no JavaScript signal
policy was added in this stage.

**Deterministic gate** (`tests/posix-signal.c`): ASan/UBSan tests cover
pending-before-call, signal-after-yield, blocked signals, simultaneous
signal/readiness, and restoration on ready, cancellation, and descriptor
errors.  Results: `posix-signal` 26/26, `posix-wait` 21/21,
`posix-select` 86/86, `posix-kernel` 240/240, and `posix-select-abi`
1097/1097.  The HTML runtime's pselect host path compiles with the updated
signal-set codec; browser event delivery remains Stage 7.

### Stage 7: Replace the browser transport shortcut — COMPLETE

- Add terminal-input and wait-state exports to `browser_api.c`.
- Update the C-engine Bash HTML worker to enqueue input into the kernel and
  resume based on the reported wait condition.
- Retire JavaScript's `-2` pseudo-result and the libc “always ready” shortcut
  after no caller depends on them.
- Keep output presentation and browser timers as narrow host capabilities.

Gate: the self-contained Bash page starts, reaches a prompt, waits without a
busy loop, accepts multiple commands, handles EOF and interruption, and exits.
Its existing 5/5 smoke gate must remain green.

#### Completed work — 2026-09-19

**Engine-owned interactive terminal** (`src/engine/store.h`, `src/engine/store.c`,
and `src/engine/lib/kernel.c`): interactive launches can replace the default
noninteractive kernel with terminal descriptors 0–2.  Reads use the kernel's
bounded terminal queue and register a pointer-free read wait on `EAGAIN`;
successful reads cancel the wait.  The JavaScript `-2` pseudo-result is no
longer part of the interactive path.

**Browser control ABI** (`src/html-rt/browser_api.c`): added integer-handle
exports to enable the terminal sandbox, enqueue input bytes, signal EOF, raise
a signal, and report the current wait kind.  Input is copied into the kernel
from a temporary engine allocation and then released.  Yield reasons are
preserved across resume so the worker can distinguish read/select waits.

**Worker transport** (`src/html-rt/src/bash/worker.js` and
`tools/generate-c-engine-bash-html.py`): removed the JavaScript input queue and
`-2` handling.  The worker enables the terminal kernel before starting Bash,
copies submitted bytes through `waste_wast_enqueue_input`, raises signals via
the engine export, and only waits for browser messages before calling
`waste_wast_resume`.  Output remains the narrow `posix_write` host capability;
no server, asyncify transform, or shared-memory requirement was introduced.

**Gate:** `node tests/c-engine-bash-browser-runtime.cjs` passes 5/5.  The
native sanitizer suites from Stages 5–6 remain green, and the browser runtime
rebuild links successfully.  Generic noninteractive WAST sandboxes retain
their existing host-read fallback; only the explicitly enabled Bash sandbox
uses the engine terminal.

### Stage 8: Correct the libc fixtures and differential gates — COMPLETE

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

#### Completed work — 2026-09-19

**Boundary expectations:** `tests/libc-test/environment-boundaries-client.wast.inc`
now treats `select(0, ...)` and `pselect(0, ...)` as supported readiness polls:
both return zero with no descriptors ready.  The fixture retains `ENOSYS`
assertions for operations that remain outside the engine's capability boundary.

**Focused libc coverage:** added
`tests/libc-test/select-runtime-client.wast.inc`, covering immediate zero-timeout
select/pselect results and invalid descriptor/`EINVAL` handling.  The existing
native kernel/select/signal suites provide the deterministic finite-timeout,
indefinite-wait/wakeup, and signal-interruption coverage without making a
browser fixture block indefinitely.

**OCaml differential provider:** `tests/libc-test/libc-runtime.cjs` prepends a
small `waste_kernel` module for the OCaml oracle.  Its versioned `select_v1` and
`pselect_v1` exports model the synchronous wrapper cases while leaving the
C-engine/browser fixtures unprovided, so those paths exercise the real host
resolver and engine kernel.

**Native C integration:** `src/cli-rt/main.c` now resolves the same
`waste_kernel` imports for native C-engine runs, decodes guest ABI structures
through the caller instance, and returns the kernel's negative-errno results.
The focused native fixture passes 4/4 assertions.

**Differential and browser gates:** both sequential and threaded OCaml libc
runs pass all 12 suites.  The regenerated dashboard payload contains the
updated boundary and select fixtures; targeted C-engine browser execution
passes `libc-test/environment-boundaries.wast` and
`libc-test/select-runtime.wast` with no environment mismatch.

**Bash relink integration:** the Bash runtime builder and the standalone libc
builder now consume one shared guest-libc source manifest.  This prevents the
runtime relink from omitting exports after libc sources are split, and ensures
the interactive page links the versioned `waste_kernel` select imports.  The
full `./start.sh --html-bash` generation path passes its 5/5 browser smoke gate.

The full Stage 8 gate is therefore complete: the unsupported-call contract is
explicit, synchronous wrappers have an OCaml oracle, native C-engine imports
are exercised, and browser tests use the engine-owned readiness path.

## Likely File Boundaries

The names may be adjusted to the current refactor, but responsibilities should
remain separated:

- `src/engine/lib/include/kernel.h` and `src/engine/lib/kernel.c`: per-sandbox
  kernel lifetime, descriptor table, OFDs, readiness, terminal, pipe, I/O;
- `src/engine/lib/include/select.h` and `src/engine/lib/select.c`: guest ABI
  decoding (fd_set, timeval, timespec) and shared select logic;
- `src/engine/lib/include/kernel.h` and `src/engine/lib/kernel.c`: signal
  masks, pending signals, and interruption remain part of the per-sandbox
  kernel until a later decomposition warrants a dedicated signal module;
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
