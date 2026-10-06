# Native guest sessions (Stage 6A process-driver slice)

`waste-session` executes the actual installed guest runtime, libc and Bash
through the shared C engine. It does not substitute host Bash or native
utilities. The existing freestanding `waste-cli FILE.wast` conformance runner
and its result format are unchanged.

From the repository root:

```sh
make -C src/cli-rt guest-session
build/cli-rt/waste-session --vfs-root src/vfs
```

The default source is mounted `/usr/share/waste/launch.wast`, initial cwd is
`/root`, and the terminal is 80 columns by 24 rows. `--script HOST.wast` selects
an explicit host staging input instead; guest `open`/`read`/`write` still use
only the mounted kernel. `--columns N` and `--rows N` configure initial size.
Use `--help` for the complete flag contract.

The companion uses system libc for native polling, monotonic/realtime clocks
and terminal output. Native syscalls remain outside the engine. Input is
consumed only after a READ or SELECT yield; the same saved guest evaluator is
then resumed. SELECT deadlines wake without requiring input. Raw, binary,
canonical and EOF behavior is supplied by the shared terminal/kernel ABI.

Guest stdout/stderr share one stdout transcript, matching the browser's engine
output boundary. JSON diagnostics go to stderr, or `--result-file NEW.json`
separates them. Result files must not already exist: existing files, symlinks
and hardlinks are never truncated. Reports contain assertion counts,
READ/SELECT wait counts, fork/exec/child-exit counts, input bytes, applied
`resizeEvents`/`signalEvents`, timeout/cancellation/exit
state and errors. `--trace-waits` emits one JSON line to stderr at each genuine
external wait, including the selected PID and wait kind; regression drivers
use this with a separate result file to deliver input after a confirmed wait.
Success
returns the guest exit status; runtime failures return 1, argument errors 2,
timeouts return 124 and scheduled cancellation 125. A bare guest exit may pass
its WAST invoke while the
process still returns its nonzero exit status.

`--stage-file GUEST_PATH OCTAL_MODE HOST_FILE` explicitly stages an additional
guest file after mounting the installed tree (up to 64 files and 64 MiB combined).
Both the guest path and host input must be supplied; guest file operations
never discover or fall back to host paths. Shared regression contracts use
the same file bytes and modes with the browser staging API/production worker.
Text executable fixtures use mode `755`; staging does not weaken exec checks.

## Scripted terminal controls

`--control-fd N` borrows an explicitly inherited readable host descriptor
numbered 3 or higher. The session polls it alongside stdin only at READ/SELECT
waits. It does not expose it as a guest descriptor, discover host paths, change
its flags, send host signals or take ownership of closing it. The controller
must be its sole reader. Without the option, terminal controls are disabled.

The version-1 transport uses fixed 16-byte records: ASCII `WSC1`, then three
little-endian unsigned 32-bit words: operation, first argument, second argument.
Operation 1 resizes to `(columns, rows)`, both 1–65,535. Operation 2 raises a
guest signal numbered 1–128; the second argument must be zero. Partial records
are buffered in a bounded session-owned record; coalesced records retain their
boundaries. Bad versions, operations, arguments and truncated EOF fail the
session explicitly, without fabricating a guest trap/exit or a passing
assertion. Clean control EOF disables only this channel, not terminal input.
For example, a Python controller can construct a resize with
`struct.pack("<4sIII", b"WSC1", 1, 120, 40)` and pass the pipe's read end using
`subprocess.Popen(..., pass_fds=(read_fd,))`.

Both adapters apply controls to the engine-selected waiting process. A changed
terminal size generates SIGWINCH through the existing shared kernel; an
identical size does not. Signal disposition, pending state, SELECT interruption
and guest handlers remain engine-owned. An ignored signal or unchanged resize
may wake the adapter, but the engine waits again when no event is deliverable.
A resize during READ does not insert input or manufacture read completion.
This is selected-process event delivery, not process-group broadcast, arbitrary
PID targeting, host signal forwarding or new signal semantics.

The worker's existing `{type:"resize", columns, rows}` and
`{type:"signal", signal}` messages now validate integer ranges before Wasm
argument conversion. Invalid events and pending-signal queue overflow produce
`control-error` messages without mutating/waking the guest. Pre-start resize
messages coalesce to the latest size; up to sixteen pre-start signals retain
order. Queued controls remember their wakeup even before `waitForIO` installs
its resolver. Active controls use the same wakeup path. Delivery still requires
the worker to regain its event loop; this is not immediate interruption of
runnable synchronous guest code.

```sh
python3 tests/guest-session-terminal-check.py --page build/html-rt/bash.html
```

The shared `terminal-control` scenario verifies nine assertions through native,
browser exports and the packaged worker: SIGWINCH/size reads, unchanged resize,
ignored and caught signals, guest-handler execution with EINTR, READ remaining
blocked until real input, and a selected child followed by parent recovery.
Only one terminal byte is supplied; native control records are deliberately
fragmented. Additional sanitizer probes reject 28 malformed/truncated records
and seven invalid descriptor arguments, retain terminal input after control
EOF, accept coalesced records and reject malformed child delivery without
reporting a child exit. Browser probes reject eight invalid export values,
check same-instance fresh-store recovery, and reject 35 invalid/overflow worker
messages while testing pre-start delivery. These fixtures are authored host
regressions, not additions to the installed `/root/waste/tests` corpus. Evidence is under
`build/engine/refactor-stage6a2-terminal`.

`--timeout-ms N` defaults to 30,000 ms from startup and bounds interpreted
runnable execution as well as READ/SELECT waits. `--cancel-after-ms N` schedules
session cancellation from the same starting point. Both accept 1–3,600,000 ms;
the earliest limit wins (cancellation wins ties). Reports retain separate
`timedOut`/`cancelled` flags, and interruption is not a guest exit or trap.
Runtime policy is shared by providers, fork clones, replacement images and
WAST handlers. Its allocation-free evaluator safepoint polls at most every
4,096 dispatched instructions, including tail calls. Module starts receive
the policy before invocation. Parser/decoder work, a long single instruction
and blocking host callbacks are not preempted; this is a cooperative limit,
not a hard host-process deadline or a general concurrent scheduler.

The browser export `waste_wast_set_execution_limits(timeout_ms, cancel_ms)`
configures subsequent script runs; zero disables that limit. It rejects
out-of-range values and changes while paused. `waste_wast_execution_stop_reason`
reports none/timeout/cancellation as 0/1/2 and survives cleanup, resetting on the
next run. The production worker accepts optional
`executionLimits: {timeoutMs, cancelAfterMs}` in its start message. It wakes
blocked execution with a timer and reports `timedOut`/`cancelled` in `done`.
Browser policy time uses the existing host clock, clamped against backward
steps; native time uses the monotonic clock. Scripted guest clocks and
wait-time advancement are described below; host policy time stays independent.
Cancellation of running code still requires a cooperative engine safe point.

Both runtimes now
use `src/engine/process_driver.{c,h}` for bounded child-first fork, executable
replacement, parent/provider continuation restoration and child exit/wakeup.
The kernel/store still owns wait/reaping and process memory. Nested child-first
fork remains unsupported; this is not a concurrent process/thread scheduler.
Both runtimes use `src/engine/wast/handler.{c,h}` for WAST child command streams,
including definitions/instances, registration, module assertions and invocation
assertions. READ/SELECT yields retain the exact assertion and its selected
engine until completion; resumed returns, expected traps and result mismatches
are checked once, not replaced by an unconditional successful `main` result.
Completed child command modules are released before provider-clone reaping,
and parsed module metadata remains owned until its engines are released.
Assertion failures are counted and produce child status 1; the parent Bash
session can recover and exit with its own status. Native reports distinguish
root `error` from `handlerFailures`/`handlerError`. The browser records the
failed assertion as usual, so its final worker `ok` is false even when Bash
recovers. Unasserted invalid setup commands in an executable WAST handler
are failures, not silent successes. Stage 6B.42 also makes ordinary setup
failures visible in the native batch and both browser frontends, separately
from assertion totals. Executable handlers retain their child-status policy;
the corpus runners recover at command boundaries while preserving a failed
file outcome and every setup diagnostic.

Nested process transitions/host-I/O inside WAST invocations and yielding module
starts are explicitly unsupported (child status 126); a paused start cannot
satisfy a module assertion. Native browser-specific host I/O remains pending.
The loader preserves the returned start status even when an import did not
set `error.status`; disposing a yielded partial instance is not success.
This is not a batch conformance runner: unsupported
WAST command kinds fail visibly; use `waste-cli` for the full language corpus.

## Shared boundary regression

```sh
make -C src/html-rt wast-browser
python3 tests/guest-session-check.py --page build/html-rt/bash.html
make -C src/cli-rt guest-session-sanitize
python3 tests/guest-session-check.py --native build/cli-rt/waste-session-sanitize
```

The same JSON interaction contracts and installed files and inventory metadata drive native and
compiled-browser engine sessions, and optionally the packaged production
worker (`--page`). Inputs follow guest output/wait events with
a delay; they are not preloaded. Checks compare exact transcripts and assertion
counts for fragmented Bash commands, a subsequent prompt, mounted-file `read`,
binary NUL/`0xff` input, EOF, file round-trip and errno. Additional contracts
cover all eleven installed Coreutils programs, pipelines/redirection,
external/builtin/stdout heredocs, long-path/repeated/quoted/fragmented heredocs,
closed-descriptor redirection cleanup, missing-command/malformed-WAT recovery,
dynamic ncurses loading and two Rogue children followed by fragmented input.
The separate `rogue-fresh` contract launches Rogue without first running `ldd`,
so a direct dynamic-library failure is distinguishable from session poisoning.
The handler contract additionally checks valid WAT, direct/shebang launches,
repeated WAST, definitions/registration/module assertions, two READ pauses in
one assertion, a resumed expected trap, SELECT readiness, a resumed mismatch
and successful parent recovery. Its expected score is 22/23, with exactly one
intentional failure and child status 1; that negative check is not a conformance
pass. The non-POSIX handler fixture also passes the OCaml differential oracle.
The separate `handler-start` negative contract rejects a yielding module start
with status 126 (7/8 results), proving it cannot falsely pass `assert_invalid`
and that the parent shell can still recover.
Native and browser now report the same explicit guest exit status. Browser
`waste_wast_guest_exited` and `waste_wast_guest_exit_status` survive script
cleanup and reset on the next run; a trap is not an explicit guest exit.
The production worker includes these fields in its `done` result.
Deterministic scenarios compare exact transcripts; Rogue compares documented
semantic markers, selected child PIDs, genuine wait boundaries and exit status,
retaining raw transcripts rather than masking arbitrary differences.
The native Bash case reads its bootstrap from the installed tree; browser
bootstrap bytes are the identical installed launch snapshot. Use `--vfs-root`
to select another installed directory. Empty directories and guest metadata
come from `.inventory.json`, independently of host checkout timestamps.
Native boundaries verify SELECT deadlines, fork return values/private-memory
isolation/reaping, bounded wait timeout and non-destructive result output.
Package checks compare individual file bytes and canonical inventory metadata.
`--scenario NAME` selects a focused interaction (linked-provider isolation and native
boundary checks still run). Sanitizer runs enable
ASan, UBSan and leak checks; environments tracing child processes may require
an unsandboxed run for LeakSanitizer.

The Bash file-input regression required a typed `pop_scope` cleanup adapter
at table slot 145, following the existing cleanup-adapter scheme. It fixes the
historical C function-pointer cast without loosening engine type checks.
The host-side regression fixtures are not additional installed dashboard
suites, so the frozen `/root/waste/tests/manifest.json` corpus remains unchanged.

The formerly trapping `/tmp/session-heredoc.txt` command is retained verbatim
in `guest-session-heredoc-long.json`, now a positive acceptance contract.
Forked Bash called the canonical parent libc through shared linked-call
bindings, freeing the parent's still-live saved command. Import rebinding also
selected importing aliases before the defining owner, and left the cached
default-memory pointer unchanged. The child and parent therefore did not have
isolated provider state. Linked calls now resolve through the caller's clone
graph; imported memory/table/global objects bind to their defining provider,
including the default-memory fast path. Cloning an importer does not install
child memory access checks on its borrowed parent object before rebinding.
This fixes process isolation, not allocator bounds or Bash cleanup semantics.

`guest-session-linked-fork.wast` independently checks three repeated forks,
transitive linked calls, imported mutable globals, indirect callbacks, shared
memory aliases within each process, private parent/child state and reaping.
Provider host writes also check the cached default-memory path. The same
fixture and exact output run natively and through browser exports, alongside
the real Bash interaction contracts. Pre-fix traces and final verification
are retained under `build/engine/refactor-stage6a2-heredoc`.

The provider-isolation fix also exposed an exec continuation-lifetime bug:
child libc retained the suspended old-image `execve` activation. Executable
bootstrap and dependency constructors could resume that activation instead of
the requested export. `ldd` misclassified a built-in provider and left a pending
exec request; Rogue also failed independently during ncurses initialization.
Clearing frames only after image commit fixed `ldd` but was too late for
preflight library constructors. The shared driver now snapshots and detaches
the old root/provider activations before loading. Failed loading or commit
restores them so the original `execve` call can return its error normally;
successful replacement discards them and stale jump environments. Child
cleanup does not modify canonical parent providers. These are explicit saved
interpreter-state transitions, not native stack manipulation or Asyncify.
Browser regression failures include the bounded transition evidence in their
local diagnostic logs.
The final `provider-full-sanitize-worker.log` records all eleven interaction
contracts plus the independent provider-isolation and native-boundary gates.
The two handler negatives keep their intentional assertion failures and
expected exit statuses. Native ASan/UBSan/leak checks, compiled browser exports
and the rebuilt page's production worker agree on the acceptance outcomes.

Fork root clones now have an explicit capsule owner across exec/handler
replacement. Reaping/store teardown releases them independently of the current
image, including after a trap; the harness must not free them separately.
Loader-created GOT globals are owned with the store's linked-call bindings,
so dynamic-library/executable loading does not leak their borrowed storage.

The small export driver exercises compiled C exports; the worker driver adds
production message/packaging coverage, not DOM or pixels. Keep existing transfer,
offline-page and rendering checks. Immediate external runnable cancellation,
deterministic clocks, native host-I/O simulation and manifest batch reporting
were pending when this note was written. The installed-corpus runner and its
browser UI now live in the unified `bash.html` page; focused Node conformance
uses worker payload metadata directly.

Execution-limit regression gate:

```sh
make -C src/cli-rt guest-session-sanitize
python3 tests/guest-session-control-check.py --page build/html-rt/bash.html
```

The authored `guest-session-control-*` fixtures cover runnable loops, linked
providers, fork clones, constant-stack tail calls, module starts, replacement
WAT images and WAST-handler invocation/start assertions. Minimal guest
launchers test child transitions without repeatedly booting Bash; the existing
eleven Bash/adapter contracts still retain real delayed input and full-program
coverage. Native checks enable ASan/UBSan/LeakSanitizer and require ordinary
teardown with no false child exit. Browser checks reject false expected-trap
and `assert_invalid` passes, then run a fresh store in the same Wasm instance.
The worker checks runnable and blocked limits, preserving earlier successful
assertions rather than labeling interruption as either success or invalidity.
These host-side fixtures do not alter the frozen installed `/root/waste/tests` corpus.
Evidence is under `build/engine/refactor-stage6a2-control`.
The final combined gate passes eighteen native interruptions with leak checks,
twelve browser-export interruptions plus same-instance recovery, and four
production-worker limit cases. The eleven existing shared interaction contracts
also pass against the final native, browser-export and packaged-worker bytes.

The installed `/bin/waste-test` command delegates isolated batches to the sibling
native `waste-test` executable (or `waste-test-sanitize` for a sanitized session).
The session retains an open installed-root directory and passes that descriptor
to the companion, which reads and revalidates its files directly. Renaming or
replacing the root pathname does not redirect the companion. External content
edits are subject to inventory validation; the directory descriptor does not
freeze bytes. The runtime binds the sibling expected-failure policy; guest
arguments cannot name host files. Reports return through a bounded, versioned
host-I/O reply and the guest writes them to its own descriptors. Ctrl-C cancels
and reaps the batch process group, returning 130 to Bash. Session-wide deadlines
and external cancellation also stop and reap that group. A cancellation before
enumeration returns an explicit `cancelledBeforeEnumeration` report with no test
records. See `test-corpus.md` for guest options and explicit installation.

## Deterministic wait-event contract

Stage 6B.27 adds `tests/guest-session-waits.wast` and its JSON contract: 24
shared checks covering ready-before-wait, real delayed terminal input,
exact/repeated SELECT deadlines and post-yield pselect handler/mask restoration.
The fixture needs a session driver; ordinary installed batch execution cannot
provide its outside-guest events. `nativeCounts` records exact native boundary
observations; `stillWaiting` requires a subdeadline event to publish no progress.

```sh
make -C src/cli-rt posix-wait
python3 tests/guest-session-wait-check.py
python3 tests/guest-session-check.py --scenario waits --page build/html-rt/bash.html
```

The Make gate uses ASan/UBSan with LeakSanitizer disabled for ptrace. The
standalone Python gate defaults to full ASan/UBSan/LeakSanitizer. It validates
four rejected clock controls and a frozen guest that still reaches its host
execution deadline. Native execution needs no Node process or browser package.

Configure a nonzero frozen monotonic clock before starting. WSC1 operation 4
then advances its absolute u64 nanoseconds using low/high u32 argument words;
JSON events use `monotonicNs`. Equal/forward values are accepted; backward,
reset-to-zero and unconfigured/zero overrides are rejected. The host-side clock
and deadlines keep real time. Valid updates count in `clockEvents` and resume
the selected READ/SELECT continuation; partial records use the existing reader.

The browser keeps initial `set-clock` configuration separate from
`waste_wast_advance_clock_monotonic_ns(low, high)` at a paused READ/SELECT safe
point. The production worker accepts `{type:"advance-clock", low, high}` only
with integer u32 words and the preconfigured frozen capability; it reports a
`control-error` on rejection. An accepted update wakes I/O, leaving the kernel
to determine readiness/timeout. The actual offline Chromium gate runs the same
scenario alongside the shell and checks the transcript plus individual results.
No new signal handler ABI or scheduler is introduced.

## Shared terminal readiness contract

Stage 6B.29 adds `tests/guest-session-terminal-readiness.wast` and its JSON
contract with 76 checks; Stage 6B.37 extends it to 90. Native, browser exports
and the production worker use the same real guest imports, input bytes and
expected transcript. Actual offline
Chromium runs the same contract in an independent worker alongside the shell.

```sh
make -C src/cli-rt terminal-readiness
python3 tests/guest-session-readiness-check.py
python3 tests/guest-session-check.py --scenario terminal-readiness --page build/html-rt/bash.html
```

The native-only Make gate uses ASan/UBSan without LeakSanitizer for ptrace;
the standalone Python checker defaults to full leak checking. The `posix-select`
and `posix-kernel` gates also require this session. No Node or HTML is required
for native execution. `--results PATH` saves the standalone native report,
transcript and two premature-input control counts; Make saves them to
`build/cli-rt/terminal-readiness-results.json`. `docs/test-coverage.md` maps the
48 Stage 6B.29 checks and four additional Stage 6B.37 terminal byte-I/O checks.

The session observes terminal sets before input, after a real delayed `hello`
and after drain. Canonical `ab` stays blocked until `c\n`; backspace/DEL erase,
line kill and ICRNL editing produce their actual guest bytes. Raw VMIN=3 stays
blocked after `xy`, then `z` completes READ. Two `stillWaiting` events require
no output progress; premature complete-input negative controls exercise the
native guard. `nativeCounts` records six SELECT yields, two READ yields and
32 input bytes. These waits use the ordinary host clock; no frozen-clock
capability or new worker operation is needed.

EOF is delivered as canonical VEOF input: pending bytes are returned, the
control byte is excluded, and subsequent reads return zero while SELECT stays
read-ready. This uses existing terminal semantics in both adapters. The host
EOF API and raw readiness flags retain direct C coverage. The session stays
outside the ordinary installed batch because it needs outside-guest events.
Stage 6B.37 checks both canaries and the whole untouched tail after every read,
adds repeated EOF/shared readiness, and verifies a five-byte write after EOF.
The exact transcript retains all original markers and appends hello. Input
events and yield counts are unchanged; all 76 original commands remain in
relative order, with fourteen new assertions and the same successful exit.

## Shared terminal timing/output contract

Stage 6B.30 adds `tests/guest-session-terminal-timing.wast` and its JSON
contract with 46 checks. The same source, post-yield clock/input events and
exact transcript run natively, through browser exports, the production worker
and actual offline Chromium. `docs/test-coverage.md` maps all 14 migrated C
checks, including two output-byte expectations in the host transcript.

```sh
make -C src/cli-rt terminal-timing
python3 tests/guest-session-timing-check.py
python3 tests/guest-session-check.py --scenario terminal-timing --page build/html-rt/bash.html
```

The native-only gate is also required by `posix-kernel`; its standalone checker
defaults to full ASan/UBSan/LeakSanitizer, while Make retains the ptrace
exception. `--results PATH` preserves the native report, transcript and negative
control count. No Node or browser package is required for native execution.

OPOST+ONLCR expands newline bytes; raw, OPOST-only and ONLCR-only modes preserve
them. A 257-byte write crosses the adapter's 256-byte chunk boundary and emits
259 bytes while reporting source bytes consumed. `outputSegments` records the
five output expectations; every adapter checks the complete unmodified output.
The private C output helper's transformed-buffer length remains a separate gate.

A frozen monotonic clock near the u32 boundary advances VTIME=2 deadlines
through WSC1 operation 4 / `advance-clock`. Three `stillWaiting` subdeadline
events stay pending at expiry minus one nanosecond and complete at expiry;
three premature-deadline controls prove the native guard notices progress.
Partial `x` input survives a timed read, a fresh read releases `Q` early and
another read has a fresh deadline. Canary/sentinel assertions check the buffer.
Canonical VEOF releases `partial`, then returns zero without changing it.
Boundary counts are eight READ yields, one SELECT yield, six clock events and
ten input bytes.

These checks preserve the existing first-read-wait timer contract. READ uses
explicit clock events here; autonomous timer wakeups and broader VMIN/VTIME
semantics remain acceptance work. The new session needs host events and stays
outside the unchanged installed batch. No runtime API or timer model changed.

## Shared process-group contract

Stage 6B.34 adds `tests/guest-session-process-groups.wast` and its JSON event
contract, with 71 checks through real process/foreground-group, termios,
pselect and signal-handler imports. This is a C POSIX session contract;
OCaml language-oracle comparisons and additional OCaml providers are outside
its scope.

```sh
make -C src/cli-rt process-groups
python3 tests/guest-session-process-group-check.py
python3 tests/guest-session-check.py --scenario process-groups --page build/html-rt/bash.html
```

The Make gate is also required by `posix-kernel` and needs no Node or browser
page. Its standalone checker defaults to ASan/UBSan/LeakSanitizer; Make retains
the ptrace leak-check exception. `--results PATH` saves the native report,
transcript and two premature-input control counts.

Two background VINTR events must leave genuine SELECT waits pending before
real x/Y input releases them. Foreground changes then deliver each queued
SIGINT once. Foreground VINTR interrupts a wait immediately; disabling ISIG
makes the same ETX byte ordinary data. Events use terminal input rather than
host signal injection: six bytes, six SELECT yields, zero host signal events.
Both negative controls send VINTR plus a data byte at a background boundary
and prove the native driver rejects premature progress. Browser exports,
packaged worker and actual offline Chromium consume the same contract.

The current env.setpgid import returns the new positive process group, not
POSIX's zero. Invalid group/descriptor updates preserve state; the three
standard terminal descriptors share foreground state. No full job-control,
forked-membership, setsid or background-I/O conformance is claimed. Private
signal queues, host enqueue and raw kernel EINTR remain C checks mapped in
`test-coverage.md`. No engine or runtime API changed in this slice.

## Shared terminal descriptor contract

Stage 6B.35 adds `tests/guest-session-terminal-descriptors.wast` and its JSON
event contract: 119 guest assertions plus successful exit, totaling 120 checks.
It uses real dup/dup2, close, read/write, SELECT and termios imports to verify
terminal aliasing, shared input/settings, failed-operation preservation,
closing either alias and restoring stdin.

```sh
make -C src/cli-rt terminal-descriptors
python3 tests/guest-session-terminal-descriptor-check.py
python3 tests/guest-session-check.py --scenario terminal-descriptors --page build/html-rt/bash.html
```

The native-only Make gate is required by `posix-kernel` and saves ASan/UBSan
results in `build/cli-rt/terminal-descriptor-results.json`. The standalone
checker defaults to LeakSanitizer; Make retains the documented ptrace exception.
`--results PATH` saves the native report, exact transcript and one premature
input control. Seven events deliver eight bytes over four READ and three
SELECT yields. The first byte must leave a VMIN=2 duplicate read pending;
the negative control sends both bytes and proves the driver rejects progress
at that boundary.

All host input targets fd 0. The guest closes it only after input is queued
and uses real dup2 to restore it before subsequent events. After the final
event, stdout reads shared queued data even after stdin/duplicates close;
stderr retains terminal state until its close. Failed reads preserve the
whole buffer and both canaries. Current read errors return raw negative EBADF
without changing errno; dup/dup2/close return -1 and set errno.

Native, browser exports, packaged worker and actual offline Chromium use the
same source/contract and pass 120 checks with the exact transcript. The five
audited shared sessions total 351 checks after Stage 6B.37, outside the installed
batch.
Raw readiness, OFD/reference lifetime and independent kernel isolation remain
private C gates. No runtime API or error convention changed, and OCaml kernel
development/comparisons are outside this C POSIX contract's scope.
