# C Engine Implementation Techniques

## Purpose

This document collects reusable techniques learned while bringing the C
engine, WAT/WAST front end, linker, executor, guest libc, and browser runtime
to conformance.  It explains how to extend the implementation without
reintroducing earlier ambiguity, ownership, or portability failures.

System ownership and runtime boundaries are defined in
[architecture.md](architecture.md).  Unfinished staged work belongs in the
active plans rather than this document. The OCaml reference interpreter in
`submodules/wasm-spec/interpreter` may be built via
`./start.sh --ocaml-reference` to cross-check Wasm/WAT/WAST language semantics;
see [OCaml reference interpreter build](#ocaml-reference-interpreter-build).

## Auxiliary Guest Utilities

Authored `ldd` source lives in `src/aux/ldd/ldd.c`. Run `make -C src/aux ldd`
to compile and link `build/aux/ldd/ldd.wasm` directly with Clang and `wasm-ld`,
using the installed guest SDK and shared-libc import attributes. Run
`make -C src/aux install-ldd` to build and install the audited snapshot into
`src/vfs/usr/bin/ldd`; `start.sh` delegates to this target.
Python is used by the import signature check and VFS installer only.
`make -C src/aux` builds the selected auxiliary commands and shared ncurses
library. Compilation does not publish snapshots; use the corresponding
`install-*` targets explicitly.

`ldd` reads Wasm import sections and follows resolved library dependencies
without executing the inspected modules. Check production dependency listings,
malformed input and missing libraries with the `shared-dependencies` scenario
in `src/engine/test/guest-session/guest-session-check.py`.

Rogue's package configuration, ncurses Boolean ABI profile, library import
attributes and patches live in `src/aux/rogue`. Run `make -C src/aux rogue` to
copy upstream inputs into `build/aux/rogue/src`, patch that staging copy and
compile/link `build/aux/rogue/rogue.wasm` directly with Clang/`wasm-ld`.
The build uses current installed headers and attaches `libc`/`libncurses`
namespaces to C declarations; no Python build generator, temporary sysroot or
post-link import rewrite is needed. `make -C src/aux install-rogue` checks
library signatures and explicitly publishes `/usr/bin/rogue`; `start.sh`
delegates to it. Check `rogue-fresh`, `rogue` and `shared-dependencies` in the
production native/browser session harness. See
[the Rogue build notes](../src/aux/rogue/README.md) for patch responsibilities.

Ncurses's source selection, cross-build configuration and shared stdio binding
live in `src/aux/libncurses`. Run `make -C src/aux libncurses` to stage the
read-only upstream checkout under `build/aux/libncurses/source`, configure and
generate headers/tables under `build/aux/libncurses/build`, and compile/link
`build/aux/libncurses/libncurses.so.wasm` with Clang/`wasm-ld`. Host `cc`, `tic`
and `infocmp` generate the tables and four built-in terminal descriptions.
The package preserves the four-byte unsigned Boolean ABI and imports process
stdio and matching functions from shared libc. Python handles only the shared
ABI rewrite/check and SDK publication. `make -C src/aux install-libncurses`
publishes the library at `/lib/libncurses.so.wasm` and `/usr/lib/libncurses.so.wasm`
together with its matching public SDK headers; `start.sh` delegates to this
target. See [the ncurses build notes](../src/aux/libncurses/README.md).

Vim stages the read-only upstream checkout through `src/aux/vim`. Its main
function takes two arguments, so compile its CRT with `WASTE_MAIN_TWO_ARGS`;
a mismatched main signature can link with a warning and leave an unusable
entry shim. Configure the terminal library as termcap (`vim_cv_terminfo=no`)
to match shared libc's formatter. Install the small authored defaults file
under the compiled `/usr/share/vim` runtime directory so ordinary `vim FILE`
does not stop on a missing `defaults.vim` warning. The package tests check
version output and saved file bytes, while the system session checks the
interactive screen and return to Bash in both runtimes. Executable access
alone cannot verify that an installed module's imports resolve or its entry
function runs. See [the Vim package notes](../src/aux/vim/README.md).

GNU Awk stages `submodules/gawk` through `src/aux/awk` and installs as
`/usr/bin/awk`. Use `make -C src/aux awk`, `install-awk` and `test-awk`;
see [the package notes](../src/aux/awk/README.md). Keep upstream configure and
parser generation in the staged tree, use the two-argument shared CRT, and
route child commands to the installed Bash via public `execv`. The shared
libc math providers stage LLVM libc's private implementation headers and
expose their ordinary C signatures; the Awk executable does not embed libc.
The single-byte profile retains bundled regex engines and disables optional
extensions, NLS, MPFR, Readline and persistent allocation. Native Bash and the
packaged production worker execute the same package-owned WAST assertions.

GNU M4 builds through `src/aux/m4` using pinned stable-release and Gnulib
Git objects exported from the local submodule repositories. The M4 master
checkout is a redirect without sources; stage the selected release under
`build/` instead of switching that checkout. Run `make -C src/aux m4`,
`install-m4` and `test-m4`; see [the package notes](../src/aux/m4/README.md).
Bootstrap runs entirely offline in the staged copy. Macro tests cover the
installed binary in native Bash and the packaged production worker.

## OpenSSL Guest Package

`make -C src/aux openssl libcrypto` stages upstream OpenSSL under
`build/aux/openssl`, configures the Wasm profile and builds archives using the
upstream Makefile. Final PIC links produce the shared libcrypto and CLI;
shared_libc.py assigns and signature-checks the libc and libcrypto namespaces.
`install-openssl` explicitly publishes the command, library, public headers,
configuration and license. The library install preserves unchanged headers so
publishing does not invalidate cross-build configuration. The installed guest
SDK is always used without host-header fallback.

`test-openssl` runs setup commands in guest Bash, records exit statuses in guest
files, then runs canonical package WAST assertions. Keep process creation in
Bash setup: the `/bin/wast` child handler cannot itself execute nested fork/exec
transitions. The shared provider test explicitly dlopens libcrypto before
importing its namespace in a later module. See the package README for native
and production-worker verification and supported capabilities.

## Shared Guest Libc Build

`make -C src/aux libc` compiles the production guest library directly with Clang
and links `build/aux/libc/libc.so.wasm` with `wasm-ld --shared`. It uses
installed SDK headers, PIC objects and the existing POSIX I/O profile. Objects
and dependency files stay under `build/aux/libc/objects`; Make rebuilds
changed inputs and supports parallel compilation. The linker can use the
repository-staged toolchain when `wasm-ld` is absent from PATH.

`make -C src/aux install-libc` explicitly publishes the audited library through
`vfs.py` to `/lib/libc.so.wasm` and its `/usr/lib` compatibility copy. The
browser Makefile delegates its `libc-shared` and `libc-shared-install` targets
to the aux rules. The aux wizard exposes libc and installs it before commands
in its build-all sequence. `start.sh --build-libc` builds the production
library only.

`make -C src/aux test-libc` runs package WAST tests against that installed
library from Bash. Kernel/POSIX interaction tests run with
`make -C src/aux test-libc-system`. Assertions live in the WAST files;
the shared shell driver only stages inputs, invokes Bash and preserves status.
`/bin/wast --verbose FILE` prints a dot or `F` for each result, a totals summary
and the first failure. Ordinary descriptor writes support redirection into
`/tmp` and browser download. Quiet invocation preserves its existing behavior.
See [the libc package notes](../src/aux/libc/README.md) and
[system test instructions](../docs/system-tests.md).

`src/aux/libc/sources.mk` supplies the ordered C helper list; Make adds
`allocator.c`. There is one production library, with no static fixture profile
or generated WAST concatenation. `tools/shared_libc.py` also serves Bash,
Coreutils and ncurses. SDK policy and generic Wasm inspection remain tools in
`src/html-rt/tools`; SDK/sysroot tests live under `src/aux/libc/test/sdk`.
Complete VFS installation before running tests that mount the source tree:
publication replaces that tree, and simultaneous discovery can observe missing
paths. For parallel verification, install once and run the common Bash transport
against stable inputs; running multiple Make test targets can trigger additional
installations through their prerequisites.
`make -C src/aux sysroot` stages the installed headers and CRT under
`build/system-tests/guest-sdk/sysroot`. This compiler fixture is independent
of building libc.

## Browser File Transfer

In the offline `bash.html` shell, `upload DEST` selects a host file and writes
its bytes to the guest VFS; `download FILE` sends an existing guest file to the
browser with its basename as the suggested filename. Both commands are installed
under `/usr/bin`. Upload cancellation or a file-read failure returns
nonzero. Download success means delivery to the browser download mechanism; the
page cannot confirm the eventual host disk write or detect a cancelled Save dialog.

Authored transfer sources live in `src/aux/upload/upload.c` and
`src/aux/download/download.c`. Run `make -C src/aux upload download` to build
`build/aux/upload/upload.wasm` and `build/aux/download/download.wasm` with the
installed guest SDK and shared libc. The Makefile invokes Clang and `wasm-ld`
directly, using the mounted headers and explicit C import attributes for the
`libc` namespace. Run `make -C src/aux install-upload install-download` to
build and publish the snapshots through `vfs.py`, preserving other files and
checking shared-libc import signatures. Installation targets serialize VFS
publication with `flock`; `start.sh` delegates to these targets. Python is used
for VFS installation, not compilation. Frontend changes
are explicitly installed with `vfs.py install --component app --source
src/html-rt/src` before packaging. The tarball contains `/root/app`; do not
duplicate transfer code in the HTML generator or stage binaries in frontend sources.

The shared ABI in `src/engine/guest_posix.c` provides
`waste_kernel.host_upload_v1(path, length, flags)` and
`host_download_v1(name, name_length, data, data_length, flags)`. Browser replies
must release temporary buffers after the engine copies them, wake a waiting
continuation even on failure, and avoid delivering an old picker result to a
replacement shell worker. Empty files are valid uploads and downloads.

Focused verification, without launching a browser:

```sh
node src/html-rt/test/browser-upload-runtime.cjs
node src/html-rt/test/vfs-transfer.cjs build/html-rt/bash.html
python3 src/engine/test/guest-session/guest-session-check.py --scenario transfer
```

The first checks DOM/worker failure handling with simulated browser APIs; the
second runs real guest utilities through the packaged worker with scripted file
selection; the third checks native/browser engine transfer parity. Real picker
and download acceptance was confirmed on 2026-10-06: the user verified uploaded
contents and cancellation, and supplied a JSON report downloaded from Bash.
For future manual regressions, run `upload /tmp/test.txt`, select a known text
file and inspect it with `cat /tmp/test.txt`; run another upload, cancel the
picker and immediately check `echo $?` for a nonzero status. Run
`download /tmp/test.txt` and compare the saved contents.

## Clipboard Copy

`clipboard copy FILE` copies UTF-8 text; `clipboard copy --base64 FILE` encodes
binary data as padded ASCII before copying. Source/build/install/test ownership
is in [the clipboard package](../src/aux/clipboard/README.md). The byte limit
comes from `CLIPBOARD_MAX_BYTES` in `src/config.h` and includes Base64 expansion.

Use the existing explicit host-I/O yield/resume path for clipboard requests.
`host_clipboard_copy_v1` copies guest bytes into engine-owned storage; the
worker sends an ID-tagged request and waits for matching acknowledgment before
resuming. The DOM adapter copies automatically through the Clipboard API, with
a temporary selected off-screen textarea as an `execCommand` fallback. It
removes the textarea and restores focus after the fallback. Browser permission
and recent-user-activation requirements still apply; hiding an element does not
bypass them. The command prints `Copied` only after a successful reply, and
returns failure without success output if both methods are denied. Keep Unicode
conversion in the DOM adapter, Base64 in the command and ownership/bounds in
the engine. Worker replacement must not acknowledge a stale request. Native
execution without a desktop clipboard capability fails the command and returns
control to Bash.

A text clipboard/editor cannot promise binary identity: invalid UTF-8, NULs,
and newlines may change. To transfer a Windows executable via Notepad, copy
Base64, save text, then decode with `certutil -decode hello.exe.b64 hello.exe`.
Compare SHA-256 against a direct download before treating the transfer as exact.
Package WAST tests verify encoded bytes and statuses through simulated host
transport; actual offline paste/permissions require a manual check.

## Virtual-Memory Ownership and Commit Boundaries

Keep virtual-memory metadata in the engine-owned process capsule, not in a
browser adapter or a host pointer. A mapping operation should validate its
address, length, overflow, protection, file range, and collision behavior
before publishing any region or acquiring a backing-page reference. On a
failure path, release references acquired during preparation and leave the
previous mapping topology unchanged.

Treat these states separately: a virtual page reserved but not mapped; a
mapped page protected by `PROT_NONE`; an unmapped address; and a mapped file
page whose backing file has since been truncated. The last case is a
structured engine memory fault annotated for the process signal boundary; it
is not a host signal and must not be converted into an ordinary out-of-range
error. `fork` and checkpoints retain backing-page and alias identity, while
`execve` builds a replacement graph transactionally.

Use the native lifecycle/checkpoint fixtures and the offline browser harness
after changing any of these ownership or commit rules.

Guest `posix_madvise` goes through shared libc to the process capsule's mapping
metadata. Validate alignment, advice, rounded bounds and every mapped page;
permission checks or reads would incorrectly reject `PROT_NONE` or touch
truncated backing files. Return positive error numbers without changing errno.
Hints preserve bytes, permissions, dirty state and shared backing; resident or
demand-zero pages need no additional advisory optimization. Keep public hint
values shared with the engine in `waste/abi/mman.h`. This API does not implement
Linux's destructive `madvise(MADV_DONTNEED)` semantics.

For mapping regressions, execute ordinary guest programs with a process VM
capsule and check their captured observations in WAST. An exec'd WAST handler
has no application engine bound to its capsule; directly importing memory
does not give it process mapping ownership. The unchanged LLVM mapping-method
fixture under `src/aux/clang/test/advice` demonstrates this boundary.

## Deterministic WAST Command Framing

A WAST file is a sequence of independently observable commands.  Before
invoking the module grammar, `wast_stream` asks the reentrant Flex scanner's
boundary mode for one balanced top-level form.  That mode uses the same
location, string, escape, annotation, line-comment, and nested-block-comment
state as ordinary parsing.  The driver classifies the returned byte range and
parses it with a fresh `wat_context`.

This boundary provides deterministic recovery:

- a malformed command produces one recorded parse result;
- the next top-level command begins at a known byte boundary;
- registered modules and the current store survive successful commands; and
- partially built objects from a failed command are released once.

Do not add a second handwritten comment/string scanner, or use `setjmp`,
`longjmp`, GLR ambiguity, or grammar error recovery to find the next WAST
command.  Boundary recognition belongs to the shared scanner; recovery policy
belongs to the command driver.

Ordinary module commands are setup, not assertions. Record attempted and
successful instantiations plus ordered line/status/phase diagnostics separately
from assertion/action results. A later passing assertion cannot erase a failed
setup command, and zero assertions do not establish successful execution.
Distinguish EOF completion from scanner failure or interruption. Definitions
are retained syntax; count their instances when loaded rather than claiming a
definition was instantiated. Preserve explicit module assertions as assertions.

Release a failed command's retained parse after the dispatch loop finishes
reading it. A module-processing callback must not free the script whose groups
its caller is iterating. Keep engines from failed starts alive through store
teardown when imported tables may contain their funcrefs.

## Process-Continuation Diagnostics

When a fork/exec regression occurs, compare the parent capsule and resumed
frame before the first post-fork guest store: PID, function/program counter,
locals, memory object and backing buffer identity, page count, and stack-pointer
global.  Then inspect linked provider continuations (especially libc); a store
checkpoint that restores memory without restoring provider evaluator frames can
resume a child `execve` activation in the parent.

Keep runnable selection and readiness in the shared C process scheduler.
Runtime adapters record the published capsule and service input and the
earliest session deadline, then resume through ordinary C returns. They do
not decide which guest process runs next.  Exercise successful exec,
failed exec, command-not-found, repeated terminal waits, child exit, and parent
reaping under warnings-as-errors plus ASan/UBSan.  Disable LeakSanitizer only
for the documented ptrace environment; do not treat that exception as a reason
to weaken ownership checks.

Timed guest sleeps use the versioned `waste_kernel.pselect_v1` timeout ABI. The
engine installs an absolute monotonic deadline and returns through
`EXEC_YIELD_SELECT`; native sessions wait on the host clock, while the browser
worker arms a `setTimeout` for the remaining interval and resumes the saved
engine continuation. Do not implement guest sleeps as busy loops or host calls
inside shared libc.

The legacy `usleep` wrapper converts microseconds into that same nanosleep
timespec and propagates interruption. Zero returns without a wait; intervals
of one million microseconds or more return EINVAL. Use nanosleep for longer
intervals. `src/aux/clang/test/timed-locks` advances the existing injected
clock to just before and onto deadlines, catches a signal, and executes LLVM's
unchanged timed lock retries under ordinary guest process contention. Keep
those observations and all runtime expectations in WAST.

For clock consumers, use shared-libc `clock_gettime` and the versioned kernel
reader rather than importing platform clocks into guest C++. Realtime and
monotonic use distinct injected callbacks and the explicit guest timespec ABI.
`src/vfs/root/test/aux/libc/clocks.wast` checks real progression after a yielding sleep,
normalization, output canaries and errors. The controlled scenario under
`src/aux/clang/test` advances monotonic time without changing realtime,
including seconds beyond 2038 and nanosecond precision. Supply large injected
nanosecond values as JSON strings to avoid JavaScript Number rounding.
libc++ selects its private `_LIBCPP_HAS_CLOCK_GETTIME` backend through the
package's additional compile flags; do not advertise `_POSIX_TIMERS` merely
to satisfy its platform guard.

Do not infer guest CPU accounting from available wall/deadline clocks. Public
`rusage` has two time64 timeval fields and fourteen wasm32 long counters;
shared libc and the legacy env adapter explicitly reject accounting queries
without touching the output. Keep `HAVE_GETRUSAGE` disabled in the LLVM guest
profile so its existing fallback owns the zero CPU durations. The
`src/aux/clang/test/process` fixture compiles unchanged full Process.cpp
and executes its real PID/wall-time calls. Ordinary fork/wait children also
check `_Exit` through the existing engine exit path. Section collection drops
unreferenced crash-context methods, so this does not establish complete LLVM
Process::Exit or C++ static shutdown. Keep all runtime assertions in WAST.

Validate outputs before consuming process state. A wait destination can be
read-only, unmapped or span a protected page; discovering that after reaping
loses the child's status. `exec_memory_prepare_write` validates the full range
and reserves lazy/copy-on-write backing without changing bytes or dirty flags.
Repeat on every WAITPID retry and retain only the interpreter's integer call
operands. `src/aux/clang/test/exec-wait` establishes child exit with pipe
EOF, attempts invalid copies, then successfully reaps the same zombie. It also
checks write-only output, exact-path execv/environment copying and caught/
restarted waits through the existing signal and sleep paths.

For finite child waits, use public `waste_waitpid_until` with an absolute
CLOCK_MONOTONIC nanosecond deadline. Its versioned kernel adapter shares ordinary
waitpid's complete output validation and signal callback path, while registering
the existing syscall wait record. Keep the same absolute operand across EINTR
retries and SA_RESTART; a relative timeout recalculated after each signal can
extend the wait indefinitely. A ready child wins over expiry; expiry leaves it
alive and returns zero without changing status or errno. Cancel the record on
every completed/error/interrupted return without cancelling the independent
alarm. The child-deadline fixture checks live/concurrent nested parents and
controlled boundaries, masked alarms and cleanup. Use the shared scheduler's
earliest deadline publication; do not reserve SIGALRM for a compiler timeout.
Keep deadline presence separate from its numeric value: UINT64_MAX is a valid
saturated finite deadline. When selecting a blocked process, prefer that timer
over an indefinite wait with the same comparison sentinel.

Audit optional output requests at their caller boundary. LLVM's Unix Wait
passes a rusage record even when no statistics were requested; the guest
adapter instead selects existing waitpid and rejects requested accounting
explicitly, before spawning in ExecuteAndWait and before reaping in Wait.
Positive LLVM timeouts now use the shared child-deadline API: read the monotonic
clock once at Wait entry, add the unsigned duration with UINT64_MAX saturation,
and retain that absolute deadline across caught signals. Polling expiry retains
the child. Ordinary expiry sends SIGKILL and reaps only the target, retrying
EINTR without consuming an unrelated child's status. The archive-linked
finite-wait fixture checks these methods, independent alarms, sleeping/CPU-bound
children and exact/full-width clock boundaries in both runtimes.
Exercise CPU-bound children with the production scheduling default, rather than
silently enabling a harness-only quantum. Native Bash and the production browser
worker use EXECUTION_DEFAULT_PUMP_QUANTUM_MS from src/config.h. A browser pump
resume that reaches READ/SELECT must publish the new wait kind through the same
readiness notification as an ordinary IO resume; otherwise post-prompt input or
clock controls can wait indefinitely in the harness.
The actual staged Wait fixture checks polling, indefinite
EINTR retry, status/error interpretation, unchanged signal dispositions and a
later successful wait after rejected requests. Its extracted method preserves
license/boundaries/hashes; full Program.cpp compilation and LLVM execution
methods remain separate gates, rather than fixture fragments in production.

After extracted-method checks, link callers against the actual built Support
archive with ordinary CRT, current C++ archives and the installed libc. Audit
retained imports and exercise real children through Bash in both runtimes.
Section collection makes this a gate for the used methods, not every library
dependency or C++ startup/shutdown. The program-execution fixture covers actual
ExecuteAndWait/ExecuteNoWait, copied StringRef arguments/environments, redirects,
polling, concurrent waits and detachment. A failed child setup must terminate
that child; returning to the caller creates a second parent continuation and
can report success to the original parent. Keep that correction guest-only.
Reject optional affinity and memory budgets before fork when the shared kernel
cannot enforce them, even if libc can store rlimit values. Validate redirect
vector length in release builds before indexing it. Requested accounting
remains explicitly unsupported; elapsed wall time is not child CPU usage.

Implement null redirection as a shared kernel character device, independently
of VFS transport bytes. Keep its metadata in a retained inode and avoid pointers
into a cloned or compacted namespace in its stateless open description. Dup and
fork share that description; the inode survives unlink and parent destruction.
EOF reads and discarded writes are immediately ready in their allowed access
directions, and do not allocate file storage or call a runtime output capability.
Begin synthesized inode allocation beyond the configured VFS entry range to
avoid collisions with automatically numbered native/browser catalogues. Route
native and browser staged files/directories through the same fork-shared kernel
allocator after catalogue mounting. Separate staging counters can collide with
retained or mounted identities: LLVM may then treat different files as the same
input and omit an import library. Test distinct identical-byte inputs, stable
descriptor identities and a valid mounted inode that collides with the former
staging counter.
Preserve the 48-byte path metadata and 128-byte stat ABI; define node kinds in
the shared public ABI so directory entries report DT_CHR. Test openat, symlink/
hard-link resolution, descriptor flags, canaries and error boundaries in WAST.
Keep private sanitizer guards for codec and orphan ownership. Test actual LLVM
empty redirects in both synchronous and detached asynchronous children, checking
their observed stdin, stdout/stderr and metadata through a separate output file.
See `src/vfs/root/test/system/devices` and `src/aux/clang/test/program-execution`.

Separate physical terminal descriptors from controlling-session ownership.
`setsid` must validate the whole process table before changing SID/PGID and
clearing the caller's terminal ownership; it must not close inherited aliases
or change the parent's shared terminal foreground group. Keep the device's
session on its OFD and the process's SID/ownership in its kernel. Check both
when consuming queued terminal signals; route host foreground signals through
the device's session even if a detached child is active and has closed all its
aliases. Copy integer device identity before delivering a signal: termination
may release the device's last reference during the group walk. The compiled public
session WAST checks nested fork/exec, zombies, errors and parent preservation.
Private lifecycle sanitizer guards cover alias refcounts and host/queued signal
routing under an otherwise unrepresentable group-ID collision. Keep terminal
acquisition and session-leader exit/job-control gaps explicit.

Do not reuse the pathname limit for argv/environment strings or advertise the
host's ARG_MAX. Keep engine-owned count/string budgets in src/config.h and
expose them through a bounded versioned query. Count every terminating NUL,
including empty strings; share one remaining budget across both vectors.
Track ownership after each copied element so a bad later pointer or excess
entry frees partial copies. Recheck interpreter-added arguments before image
commit, and preserve the specific E2BIG through rejected-exec resume. LLVM's
guest fit overloads use actual current environment sizes/counts, avoiding a
separate compiler budget or an arbitrary environment allowance. The
argument-limits fixture derives boundaries from the running kernel and keeps
all expectations in WAST; a successful fit followed by an enlarged environment
must still fail exec safely.

Fork must restore the parent's continuation immediately after cloning the
child graph, with a process-owned return value for each side. Deep-copy clone
bindings and compose inherited aliases: copied funcrefs can still name an
ancestor's original provider. Capture only the active process's provider frames,
not another runnable process's evaluator. Test grandchildren's direct, indirect
and tail calls, private memory/globals and both wait statuses under ASan/UBSan.
`guest-session-linked-fork` covers these invariants.

Scheduling rotates runnable capsules on opcode time slices and blocking
imports. Keep kernel wait records and absolute deadlines process-local; retry
blocking waitpid only when its matching child can be reaped. Publish input
waits together with the minimum timer across all processes, so background jobs
progress while Bash waits at its prompt. Use host monotonic time for CPU slices
even when a test freezes the guest timer clock. WAST handlers need independent
command contexts and PID-owned definitions/registrations; cleanup must remove
only their own modules and failed-start engines, including cancellation paths.
The system probes under `src/vfs/root/test/system/scheduling` check overlapping timers
at controlled clock boundaries and a CPU spin that requires a sibling to run.
`aux-bash` checks concurrent handlers using identical module names, nested
scripts and background sleeps through native, browser API and packaged workers.
In the native session transport, check a `stillWaiting` output guard while
awaiting the next expected wait record as well as after matching it. Premature
guest progress can change READ to SELECT (or another PID); waiting only for
the original wait type turns an intended negative control into a timeout.

## Terminal Rendering and Shell Verification

Keep VT parsing independent of rendering and engine execution. Feed the model
the exact engine-processed byte stream; canonical/raw handling and ONLCR belong
in the C kernel. Preserve incremental UTF-8/escape parsing, alternate-screen
state, CSI REP and character-set designators used by ncurses. Application-cursor
mode must affect the input encoder as well as output parsing.

Keep scrolling margins per screen buffer. Vim limits scrolling to its text
rows with `CSI top;bottom r`, indexes at the bottom margin to scroll up, and
inserts lines with `CSI L` when scrolling back down. Implement index/reverse
index, line insertion/deletion and explicit scrolling within those margins;
preserve header/status rows outside them and use the current background for
new blank cells. Reset margins when clearing a buffer for alternate-screen
entry or resizing it. `src/aux/vim/test/scroll-session.json` checks visible
rows in both directions before later input can trigger a full redraw.
New scroll blanks must have no underline/bold decorations: Vim can scroll
while its status-line rendition is active. Preserve effective colors without
copying those text decorations or changing the rendition for later printed
characters. Check cell attributes as well as row text; otherwise stray
underlines in trailing blank cells can pass a text-only regression.
Preserve CSI private prefixes when dispatching commands. Vim's `CSI ?4m`
queries modifyOtherKeys; it is not the ordinary `CSI 4m` underline command.
Ignore unsupported private queries without changing text rendition, and keep
supported DEC private mode changes (`CSI ?...h/l`) distinct from ordinary CSI.

Advertise only implemented terminal capabilities. Readline checks whether an
insert capability exists, even when its returned string is empty; an empty
`im` therefore selects an insert path that overwrites visible text. Shared
libc supplies ANSI insert-mode entry/exit (`CSI 4 h` / `CSI 4 l`), matching the
browser model and native terminals. The libc `termcap.wast` probe checks these
capabilities. The system fixture in `src/html-rt/test/terminal-editing` feeds
real Bash editing keys and checks screen cells and cursor positions before
each next key, so a later redisplay cannot conceal an insertion failure.

Use font metrics to transform GLF geometry into cells. Preserve cmap format
4/6 lookup, missing-glyph behavior, indexed glyph ranges and analytic curve
coverage. Upload immutable geometry once, keep shader setup outside the cell
loop and coalesce redraws. Resize the backing canvas for device pixel ratio
without changing model contents. Any geometry optimization must preserve
glyph appearance; do not introduce a raster atlas into the WebGL path.

The fast checks run without a browser:

```sh
node src/html-rt/test/terminal-glf.cjs
node src/html-rt/test/terminal-model.cjs
```

For actual WebGL pixels, open the rebuilt `build/html-rt/bash.html` in
Firefox and run these commands at its Bash prompt:

```sh
wast /root/test/html-rt/terminal.wast
download /tmp/terminal-renderer-results.json
```

Run the browser Bash fixtures together and save the verbose transcript for
download. Nested Bash now supports launching the WAST commands in the script:

```sh
bash /root/test/html-rt.sh > /tmp/html-rt.txt
download /tmp/html-rt.txt
```

The runner reports PASS, expected-failure, skip and failure counts. The
process-kernel fixture is skipped here because its fork checks require a
top-level process context. The runner defaults `WAST` to `wast`. To use a
different WAST executable from
the project checkout, run the same script with:

```sh
WAST=build/cli-rt/wast bash src/vfs/root/test/html-rt.sh
```

The loop works with either runner; browser-only capability checks still need
the browser runtime to pass.

The authored renderer fixture is `src/vfs/root/test/render/terminal.wast`;
the test corpus installer publishes it, the browser compatibility kernel
fixture and Bash-context tests under `src/vfs/root/test/html-rt/` for this
browser suite. The suite runner source is `src/vfs/root/test/html-rt.sh`.
The WAST calls the versioned `waste_kernel.render_test_v1` browser capability,
then writes the reply through ordinary guest descriptors. The DOM adapter uses
the production GLF renderer to test glyph pixels, blank cells, colors, cursor,
two widths and simulated display densities 1/2. It rejects the Canvas2D
fallback and glyph texture creation. Rendering failure still produces the JSON
file before the WAST assertion fails. Transport/filesystem failures instead
fail the file-writing assertion; do not interpret a stale earlier report as a
new result. The report includes its timestamp.

Save the downloaded JSON under `build/html-rt/`. Closure requires `ok: true`
and a manual check of the shape-sensitive text/colors printed by the fixture.
Also resize the browser and change zoom; simulated densities test sizing, not
monitor/zoom event delivery. The adapter draws into a temporary offscreen
canvas, releases its WebGL context afterward, and preserves the live Bash
terminal. There is no separate generated test page.

The `render` corpus group is marked interactive-only and skipped by unattended
batch runners. Native and isolated browser stores do not enable this capability;
`engine-regressions/render-capability.wast` checks the unsupported return.
The response capacity comes from `src/config.h` and is queried by the guest.
The wire reply is a little-endian u32 pass flag followed by UTF-8 JSON.
`src/html-rt/test/render-worker-session.json`, run through `src/html-rt/test/guest-session/worker.cjs`,
uses scripted replies to verify persistence, failure reporting, stale-reply
rejection and truncation across repeated shell runs. It does not test pixels.

Pixel checks complement the worker-backed shell checks. After building the
production page, run `./start.sh --html-check` for the focused worker suite.
For targeted changes, `src/html-rt/test/bash-browser-runtime.cjs` accepts
`--missing-command`, `--pipeline-probe`, `--shared-library` and `--full-package`.
Run `--missing-command` without `--full-package`: its reduced fixture
deliberately omits `ls` to check command-not-found recovery.
The Rogue fixture must enter/leave the alternate screen, consume an arrow and
a game turn, and return to a usable Bash prompt twice in the same shell.
`python3 src/engine/test/guest-session/guest-session-check.py --scenario shared-libc` checks explicit
guest `dlopen`/`dlsym`/`dlclose`, error consumption, reference counts and reopen
through the installed libc/ncurses libraries in both C builds. Its authored
fixture is `src/vfs/root/test/engine-regressions/shared-libc.wast`; the installed copy can
also run from Bash with `/bin/waste-test engine-regressions/shared-libc.wast`.
Use the installed-test runner so this bootstrap test gets an isolated module
store instead of inheriting the shell's already-loaded libc.

Production packaging also needs a dependency check: a loader fixture alone
cannot establish that applications use the shared provider. Run
`python3 src/html-rt/test/shared-library-packaging-check.py` to inspect installed consumers,
then the `shared-dependencies`, `matrix` and `rogue` scenarios in
`src/engine/test/guest-session/guest-session-check.py` to verify actual `ldd` output and execution
in both C runtimes. `shared_libc.py` rewrites only matching function imports,
checks the installed provider's exact signatures, and leaves memory, table,
relocation and kernel imports intact. Coreutils links imported process memory
without imposing a maximum absent from the exporter; ordinary Wasm limit
validation still applies. The install review flags cover those checked libc
imports explicitly. Preserve the separate static libc fixture profile.

For PIC libraries, keep relocation bases in storage owned by the loaded
module's call block. A pointer into the loader's temporary context silently
changes an existing DSO's base when another library loads. Patch GOT entries,
apply data relocations, then run constructors. Reserve executable table slots
before allocating dependency slots. Reuse the process's shared stack global,
falling back to the `waste-runtime` owner when the active application does not
export it. Test libc allocation after loading another DSO.

The synchronous loader defers cooperative pump yields across instantiation,
relocations and constructors, while retaining deadline/cancellation polling.
Executable image preparation applies the same rule to module starts and startup
hooks. Otherwise an expired opcode time slice can become a spurious ENOEXEC
instead of committing a valid executable. Restore the caller's pump quantum
on every return. A yielded constructor
cannot resume a loader C activation that has already returned; do not report a
cooperative timeslice as `dlopen` failure. Verify through the installed browser
batch runner, which enables the pump, as well as direct engine calls.

Exec bootstrap must find the process-local libc through the loaded-library
catalogue before using legacy `env` fallback. Reset the allocator beyond the
immutable startup block, then set the environment and stdio. Registration
aliases alone do not identify the provider reliably; skipping initialization
can let inherited allocator state overlap a new executable's data. Verify `ls`
as well as simpler utilities. Private `guest-session --trace-process` writes
JSON process events to stderr, including exec rejection and child trap details;
redirect stderr under `build/engine/logs/` when diagnosing status 126/127.

Fork copies library metadata and rebinds providers to the child's graph.
Grow that copied metadata array before appending dependencies. On final close,
remove registrations and invalidate table references owned by the freed engine;
clear the handle's identity so reopen creates a live instance. Detach an
inherited clone from capsule ownership before freeing it. Memory/table range
and handle-slot reuse on ordinary close remain deferred; process exit releases the address
space. Repeated Rogue children test these lifetimes across real fork/exec/wait.

## WAT and WAST Parser Policy

WAT and WAST should not have competing lexers or module grammars.  The same
reentrant Flex scanner and pure LALR Bison parser recognize module syntax.
Drivers apply the mode-specific policy:

- WAT requires one complete module and fails transactionally.
- WAST parses one framed command, records the expected or unexpected outcome,
  and continues.

Keep syntax recognition in the lexer and grammar, but keep assertion checking,
name resolution, type checking, validation, instantiation, and execution in C
passes.  Grammar actions should capture structure and raw semantic values,
not decide whether an invalid module satisfies an assertion.

Parser and scanner state must be caller-owned.  `%define api.pure full` and a
reentrant scanner are insufficient if grammar actions still use file-static
accumulators.  Locations, temporary vectors, fixups, numeric scratch storage,
and current-module state all belong to an explicit parse context.

Command streams create many short-lived contexts. Keep zero-initialized parser
state and ownership fields before the bounded scratch arrays in `wat_context`.
The builder clears that state prefix; scratch entries must be initialized before
consumption, with zeroed live counts guarding every lookup. Add fields that need
an initial zero above this boundary. Start group storage with one zeroed group
and grow for whole-script parsing; reserving/clearing unused maximum workspace
on every command can dominate execution. Verify this boundary with nonzero
allocation poisoning and the complete corpus, including names, fixups and
branch-table cases.

## Folded Instruction Boundaries

Most words in a folded expression are instruction mnemonics, not special
grammar keywords.  Structural module fields such as `func`, `type`, `param`,
`result`, and `local` need dedicated grammar roles; ordinary dotted operators
can use metadata-driven tokens and shared operand parsing.

The ambiguity occurs at `(`, before the parser knows whether the form begins a
field, control instruction, or ordinary folded operator.  Resolve it with a
bounded lexical start token for the complete structural prefix, such as a
folded `if`, `block`, `loop`, `select`, or immediate-bearing instruction.  The
lexer should perform only enough lookahead to identify that boundary, then let
the grammar parse the operands and nested expressions.

After a block type, parse fields in the same order as the reference grammar:
type use, parameters, results, then body.  Flatten nullable phase handoffs when
they introduce shift/reduce conflicts.  Avoid adding semantic keyword tokens
for every opcode; opcode metadata should remain the common vocabulary.

## Parse General Forms, Validate in a Later Pass

When valid and intentionally invalid WAST modules share the same syntactic
shape, use a permissive structural production and validate afterward.  This is
essential for `assert_invalid`: the parser must retain the invalid expression
so semantic rejection can be observed.

Constant expressions are the model technique:

1. Parse the general instruction sequence into a terminator-free buffer.
2. Resolve deferred names and type references.
3. Run a `check_const`-style pass over the retained expression.
4. Verify the allowed operator set, operand/result types, immutable
   `global.get`, reference constructors, and declared destination type.
5. Store semantic rejection on the containing assertion group in WAST mode;
   fail the WAT transaction in WAT mode.
6. Let the binary encoder append the required `end` opcode exactly once.

The same pattern applies to element/data offsets and other contexts where the
reference grammar admits a general expression but validation restricts it.
Do not create overlapping grammar alternatives for every valid operator
combination.

## Deferred Resolution and Source Locations

Text syntax permits forward references in several index spaces.  Record a
bounded fixup containing:

- reference kind;
- source line, column, and byte offset;
- destination object and field;
- original name; and
- expected index space or type constraint.

Apply fixups only after the relevant module declarations are complete.  Keep
empty name-table entries so table positions match Wasm numeric index spaces.
Unknown names must produce a structured error at the original reference, not
an encoded sentinel that fails later in the binary decoder.

Function, type, table, memory, global, element, data, start, export, and
initializer references should use the same fixup mechanism.  Named operands
such as `table.init` may refer to more than one index space and must record each
role explicitly.

## Annotation Handling

Registered custom annotations need deterministic, bounded handling without
changing the ordinary module grammar's semantics.  The current implementation
retains annotation placement and payload information long enough for C code to
validate custom sections, names, and branch hints, including folded and plain
control instructions.  Unknown annotations remain ignorable according to the
text-format rules.

The scanner consumes annotation envelopes directly without rewriting the
source and preserves their original line, column, and byte offsets.  An
annotation error inside an assertion must be reported through that assertion's
malformed or invalid classification rather than terminating the WAST stream.
Keep bounded registered-annotation validation in C and do not add another
independent raw-text boundary scanner.

## Binary Readers, Writers, and LEB Values

All Wasm byte processing uses bounded readers and overflow-checked writers.
Readers expose transactions or sub-readers so a failed decode does not leave a
partially advanced cursor.  Section decoders receive a bounded section reader
and must consume it exactly.

Use the shared LEB implementation for unsigned, signed, 32-bit, 33-bit, and
64-bit fields.  Test shortest canonical encodings, accepted non-short
encodings, truncation, excessive length, unused-bit overflow, and cursor
rollback.  The linker and encoder must not carry private LEB implementations.

The decoder distinguishes malformed binary structure from semantically invalid
modules.  It rejects duplicate or out-of-order sections, unsupported standard
section IDs, invalid UTF-8, invalid indexes, and malformed limits with a
structured result rather than an assertion or out-of-bounds read.

## Decode, Validate, and Instantiate Separately

Decoded declarations are immutable and own their source-independent data.
Validation reads those declarations without creating mutable instance state.
Instantiation then allocates and initializes memories, tables, globals,
segments, tags, and runtime objects.

This separation enables three important tests:

- decode once and instantiate twice without shared mutable state;
- release or replace the source byte buffer without invalidating declarations;
  and
- classify decode, validation, linking, initialization-trap, and execution
  failures independently.

Constant-expression evaluation occurs during instantiation after validation.
Instantiation must preflight limits and references before exposing a partially
constructed instance, while preserving specification-required side effects on
failure paths.

The shared `WASM_MAX_TYPES` bound in `src/config.h` controls WAT type storage,
binary reference decoding, executor type tables and structural comparison.
Its current 256 entries fit the existing eight-bit indexed-reference payload;
larger values require a representation change, not just a limit increase.
Allocate binary type slots for the declared entry count and grow within the
bound when recursive groups expand it. Keep the old allocation until growth
succeeds and zero new slots. Ordinary modules should not reserve the full
configured table. Reject both excessive entry counts and excessive expanded
group totals before writing a type slot.

Recursive WAT forward references use parser-only placeholders outside the real
value-type range. Those placeholders must still fit the low 16 bits used by
storage-type packing; higher bits carry mutability and packed-field metadata.
Resolve placeholders at group close before encoding, without changing an
already resolved reference. `src/vfs/root/test/engine-regressions/type-capacity.wast`
exercises high indexes, recursion/growth failures, field mutation and structural
equivalence through a 230-type chain across modules.

Decoded function local types use an exact-sized allocation, separate from the
live value frames used during execution. Preflight all local declaration groups
with a copied bounded reader before allocating: validate even zero-count groups,
check cumulative counts without overflow, and include parameters in the frame
limit. Replay validated declarations into the allocated array. Functions with
no declared locals keep a null pointer. These arrays are immutable instance
metadata, shared with decoded instructions by cloned engines and released only
by the last static owner, including after a failed load. Keep the combined
parameter/local check in the validator too, before accessing its initialization
bitmap. `src/vfs/root/test/engine-regressions/local-storage.wast` exercises allocation
boundaries, mixed groups, malformed declarations and recovery after failures.

Grow decoded instruction buffers by the number of instructions, not by raw
function-body bytes: LEB, floating-point and SIMD immediates are not separate
instructions. Reserve space before decoding each instruction so allocation
failure can release all previously decoded branch-table/catch payloads without
stranding the current one. Keep open-control references as indexes across
`realloc`, and initialize each record before appending it. Bound geometric
growth by remaining original instruction bytes and checked allocation sizes.
After decoding, try to shrink to the actual instruction count; a failed shrink
keeps the valid buffer. Validate only after growth and trimming finish, then
share the immutable buffer through existing instance ownership. The portable
`instruction-storage.wast` regression covers growth boundaries, control-target
relocation, heap-owned immediates and failed-load cleanup. This removes byte
over-reservation; it does not establish that large modules fit the runtime's
memory budget. Audit record size and total decoded counts separately.

Decoded instruction immediates share an opcode-selected union: memory/lane
operands, block/catch metadata, GC cast operands, and vector/scalar bit patterns
are distinct payloads. Keep common indexes and validation caches outside it.
Set the block-type sentinel only for control instructions, and store block
parameter/result counts explicitly rather than in vector bytes. GC heap types,
target types and nullability flags have their own fields. Only `try_table`
instructions own a catch pointer; both failed-load cleanup and final instance
destruction must check the opcode before reading or freeing it. SIMD helpers
receive a lane only for lane operations and a vector immediate only for
shuffle, so they do not read an unrelated union payload. The native and Wasm32
record sizes are currently 56 bytes (previously 104 and 96 respectively).
`instruction-layout.wast` covers mixed payloads and values that would be invalid
catch pointers; retain growth/exception tests as well when changing the layout.

The browser interpreter's maximum linear-memory size comes from
`HTML_RUNTIME_MAX_MEMORY_BYTES` in `src/config.h`; both the production module
and direct-start probe link with that setting. The current maximum is 2 GiB,
with a 64 MiB initial allocation. Distinguish this configured budget from
wasm32's architectural limit, and measure real startup RSS separately from
streamed analysis. A maximum permits growth without allocating that entire
heap initially. A boundary probe should allocate beyond the old ceiling and
check both ends, alongside ordinary portable WAST regressions.

WAST process handlers have no executable-image engine after exec. Before
invoking an assertion, bind its engine to the active process capsule so VM
imports target that process's memory; restore the previous binding afterward,
including on yield or failure. Use the existing owned-memory binding helper
rather than changing imported-memory ownership. Test this through Bash and
parallel handler children as well as the standalone WAST API.

Keep `memory.size` separate from the process's sparse virtual-page extent.
An automatic `mmap` may use high pages without committing allocator heap in
between. Promoting `memory.size` for those pages lets libc skip growth and write
into a file mapping or an unmapped hole after `munmap`. Reuse existing linear
holes first, then select high free pages within the mmap ABI's pointer range.
Before linear growth changes any mapping metadata, reject overlap with existing
virtual mappings; keep their bytes and the old size intact. Exercise both pure
mapping fixtures with their staged file assets and the shared process-memory
`mmap-heap.wast` regression through Bash. A standalone run of the older
`diy-posix-test/mmap.wast` without its `/mmap-file` and `/mmap-short` assets does
not test file-backed mappings correctly; use corpus selection or stage them.

Lazy function loading and bounded decoded-code retention are separate choices:
validate every body while retaining encoded bytes, decode a function on its
first call, and initially retain its code until the shared code owner dies.
Only introduce eviction when workloads justify it. Eviction must preserve code
referenced by active/yielded frames and cloned processes/threads. Input archive
selection is the guest linker's responsibility; interpreter lazy loading reduces
decoded storage for functions in the linker executable itself.

The production loader now uses that first-call policy for every defined
function. Keep the retained bytes independent of the caller's module/source
buffer. Validate even unused bodies before returning an instance, retain local
declarations, and publish decoded code only after successful materialization.
On failure free temporary code/payloads/locals without changing the cold row.
The existing static reference count owns cold and decoded bodies across clones;
release either representation only with its last owner. Revalidation at first
use fills the ordinary instruction-resolution metadata. The private i32 smoke
checks source disposal, cold clone lifetime, shared materialization, reuse and
independent instances; portable stream-validation/instruction/call regressions
check execution and rejection of uncalled invalid bodies.

For large-module sizing, build the private native diagnostic with
`make -C src/cli-rt module-footprint` and run
`build/cli-rt/module-footprint FILE.wasm`. `module-footprint-sanitize` builds the
ASan/UBSan version. By default the diagnostic uses the production body decoder
but skips semantic validation, import binding, instantiation and start execution. It
releases each decoded function before moving on and never returns an executable
engine. It can inspect more definitions than the execution limit; ordinary
loading still enforces that limit. Count-sized function metadata allocation is
first bounded by available type-index bytes.

Its JSON reports instruction arrays, declared locals, branch/catch payloads,
encoded instruction bytes and a conservative single-function buffer-growth
bound. `metadataLowerBoundBytes` projects common retained metadata; it excludes
allocator overhead/slack, exports, guest memories/tables/data, filesystem and
runtime state. Other section contents are skipped or used only for counts;
`complete` means all function bodies decoded, not that the module is valid or
can link/run. In this mode `validationRequested` and `validated` are false.
`runnableInstance` and `executed` are always false in both modes. Use an external
timeout/address-space limit for large native probes;
ASan needs a different virtual-address budget for its shadow mapping.

Add `--validate-functions` to run the same per-function semantic validator as
ordinary loading, releasing each body immediately after it passes. Keep all
function signatures and declaration indexes available until analysis finishes;
validation of later calls must not depend on retaining earlier code. Counters
include only successful bodies, and a failing body reports its definition index.
The shared loader checks the remaining section structure and initializes private
module objects. Function imports receive inert callbacks; memory, table, global
and tag imports use private engine-owned placeholders with declared types and
limits. Tables belong to the owned cleanup range, and memories use the normal
ownership flags. No real provider is resolved, no start function runs and no
engine escapes. Imported non-nullable reference globals are unsupported rather
than given a fabricated reference. Placeholder values and ignored instantiation
traps mean `validated: true` is scoped to function/loader validation; it does
not establish real import compatibility, successful instantiation or startup.

Use both modes on invalid unused bodies: decoding can succeed while validation
fails. Test first/later failures, partial private-import cleanup, trapping and
import-calling starts, and ordinary start traps separately. The portable
`stream-validation.wast` guards full-body validation and ordinary execution;
private generated binary probes cover the diagnostic's non-execution boundary.
This analysis path does not implement an execution cache. A later bounded cache
must pin active/yielded frames and preserve clone/thread ownership.

Require an actual outer function `end` and exact body-reader consumption before
either normal validation or measurement. A closed inner block is not the outer
terminator. `function-envelope.wast` checks missing/trailing ends, recovery,
normal semantic rejection and start traps. Shared function disposal releases
locals and opcode-owned payloads for both streaming inspection and final
instance teardown; clones continue to use the existing last-owner rule.

## Cross-Module Linking

Resolve imports from the decoder's declarations rather than rescanning section
2.  A linked function binding records the provider instance and its local
function index.  Other extern bindings retain pointers to provider-owned
memory, table, global, or tag objects with explicit lifetime ordering.

Type equality cannot compare module-local type indexes directly.  Compare
function parameters/results and indexed reference types structurally across
their owning modules.  Preserve nullability, heap type, table address width,
limits, mutability, and tag signature.

Tests should cover provider/consumer modules, explicit registrations, imports
after expected instantiation failure, two consumers of one memory, and two
instances created from one decoded module.  Destroy consumers before providers
unless ownership has been promoted to a shared store object.

Function import bindings are an exact-sized immutable table of host function,
host-data pointer, type index and control kind. Count only function imports
when allocating it; other import kinds have separate index spaces. The
declaration map is one byte for each combined imported/defined function index.
Allocate the imported prefix even without a function section, grow for defined
functions with checked addition and zero the new suffix. Bounds checks on
exports, element entries, constant `ref.func` and instruction validation use
that combined space, not the defined-function limit alone.

Both allocations share decoded-table reference counting across process clones;
release them only with the last static owner. The host-data pointees retain
their existing external ownership. An empty module needs neither allocation.
`WASM_MAX_FUNCTIONS` in `src/config.h` bounds binary defined functions, while
`WASM_MAX_IMPORTS` bounds import declarations of all kinds in the decoder and
loader. Preserve those separate resource policies when checking combined
indexes. The WAT parser has a separate construction bound. The portable
`function-metadata.wast` regression exercises mixed/import-only modules,
combined indexes through 16,383, declaration checks and failed-load recovery.

## Proper Tail Calls

Implement `return_call`, `return_call_indirect`, and `return_call_ref` as an
update of the active interpreter frame:

1. Validate and resolve the target.
2. Pop or copy its arguments before overwriting the caller's locals.
3. Replace function identity, arguments, locals, and body.
4. Reset the numeric PC and structured-control state.
5. Continue the dispatch loop with the original return continuation.

Do not represent each tail transfer with C recursion, heap allocation, or an
exception. The C engine needs an explicit frame-reuse operation. Deep official
tail-call fixtures and a large bounded native loop should show constant C
stack and no allocation per transfer.

## Guest Non-Local Control Transfer

Native C `setjmp`/`longjmp` cannot express a guest jump through a browser Wasm
interpreter safely.  Treat guest `sigsetjmp` as creation of an evaluator
snapshot identified by an opaque token stored in the first four bytes of the
guest jump buffer. The buffer must have at least four writable bytes. Nested
activations may reuse its address; copying the buffer preserves the prior
checkpoint token. Reuse invalid snapshot slots and keep tokens monotonic across
continuation restoration, so stale copies cannot identify a new activation.
The snapshot records the interpreter frame generation, PC, operand/control
stacks, locals, and
guest stack-pointer state required by the ABI. Compiled guests using this path
must export the mutable i32 `__stack_pointer`; restore only that global, leaving
ordinary global side effects intact.

Treat guest `siglongjmp` as a typed engine result.  Ordinary C returns propagate
it until the matching live interpreted frame restores the snapshot.  Reject
stale environments and normalize a requested zero return value according to
the guest ABI. The public SDK keeps `setjmp` as a caller-local macro and gives
`longjmp` a real C-linkage declaration/import for libc++'s `std::longjmp` alias.
Verify compiled C and C++ returns-twice behavior with real stack-pointer reads,
copied buffers and unchanged global side effects; use WAST for expectations.
See `src/aux/clang/test/checkpoint`. Bash's private header forwards to this
shared ABI. Signal-mask restoration belongs to the engine-owned kernel, not
native process state. Nonzero `save_mask` flags copy all guest mask words into
the evaluator snapshot; zero leaves mask changes intact. Restore only at the
validated matching frame, after clearing abandoned syscall interests and any
temporary pselect mask. Keep copied/forked tokens and yielded continuations
paired with their own saved mask. Signal callbacks restore their own entry
scope before the outer checkpoint restores its mask. Verify jumps after sleep,
out of handlers, with newly unblocked pending signals, and after fork using
actual compiled C/C++ observations.

Link actual LLVM crash recovery against the complete Support/C++ archives;
compiled jumps alone do not establish this integration. Check disabled/enabled
and nested contexts, explicit Process::Exit, caught guest signals, reverse
resource cleanup and real Signals callbacks/file cleanup. Guest utility-mode
registration must record only successfully installed handlers so uninstall
does not overwrite ignored actions with stale previous actions. Native LLVM
keeps its original backend. These checks establish supported guest signal
recovery, not host faults, stack symbolication or recovery from all Wasm traps;
the distinction is documented with `src/aux/clang/test/crash-recovery`.

This design avoids native stack-unwinding machinery and permits deterministic
cleanup on traps, exits, or cancellation.

## Explicit Yield and Resume

A host import that cannot complete synchronously returns `EXEC_YIELD`.  Before
returning, each active interpreter depth records the state needed to reenter
the same call.  The operand and control stacks live in engine-owned storage;
per-depth records retain PC, function identity, and control height.

On resume, reenter the original top-level invocation.  Saved frames recognize
the resume path, retry the pending import, and either complete or yield again.
Push original call arguments back before propagating `EXEC_YIELD` through
ordinary C returns so the imported instruction can be retried exactly.

This is not Asyncify and must never be implemented with an Asyncify transform,
runtime hooks, or unwind/rewind imports. The native C/Wasm call stack is not
saved. Only interpreter-owned data is retained, and the later resume export
starts a new host-to-Wasm call that reenters those saved evaluator frames. When
reviewing a build, any Asyncify flag or `asyncify_*` import/export is a failure,
not an optional optimization.

Do not reset a partially resumed engine, retain pointers into guest memory
across the yield, or infer the blocked operation in JavaScript.  The POSIX
runtime should attach an explicit wait reason, stable handles, deadline, and
cancellation generation as described by the active `select`/`pselect` plan.

## Process continuation and scheduling

Fork captures the parent evaluator/provider graph, clones process-private
state, and resumes the parent with the child PID immediately. The child starts
with fork returning zero. Child exit records a zombie and closes descriptors;
waitpid later reaps the saved status. These transitions remain inside the shared
C scheduler. Input readiness and deadlines wake blocked processes, while opcode
pump yields rotate CPU-bound peers and let the runtime service its event loop.
The existing host upload/download/test-request channel remains serialized.

A regression must exercise more than one post-fork terminal read, because a
single-command smoke path does not validate repeated continuation re-entry.
The static Bash-page build therefore runs an assignment/expansion/process
sequence (`HOME_DIR=/home/a`, `echo ${HOME_DIR}`, `ls`), verifies exit status
127, accepts a later builtin and a second missing command, and exits normally.
Run this gate against the same staged worker and engine bytes embedded in the
self-contained page; an older generated page can otherwise hide a corrected
runtime behind stale assets.

## Proposal-Specific Representation

Feature implementations should preserve semantic distinctions through text,
binary, validation, and runtime layers:

- SIMD uses one mnemonic/opcode/immediate metadata table rather than separate
  parser and executor lists.  Lane, shuffle, memory, and relaxed operations
  retain their immediate shapes.
- Memory64 and multi-memory select address width and offset limits from the
  referenced memory.  Bulk operations may combine memories with different
  address widths and must validate each operand accordingly.
- GC types retain recursive groups, finality, supertypes, packed storage,
  mutability, defaultability, nullability, and indexed heap identity.  Runtime
  objects retain their dynamic reference type.
- Exceptions are not traps.  Their carrier preserves tag identity, typed
  payload, owner instance, and exception reference across direct, indirect,
  reference, and imported calls.

When a proposal adds syntax, extend shared metadata and structural boundaries
before adding isolated grammar alternatives.  When it adds runtime semantics,
preserve enough decoded information for validation to reject invalid modules
before execution.

## Browser Packaging

Generate browser pages by embedding the exact C-engine Wasm and original WAST
or application source.  The worker instantiates the engine, copies source into
its memory, runs the streaming entry point, and reads structured results from
exported accessors.

Interactive pages wait on ordinary worker events.  Terminal input is copied
into engine-owned state before resume; output is copied to the presentation
layer.  Keep the page self-contained and usable through `file://`.  Do not add
a server or cross-origin-isolation dependency merely to obtain scheduling.

Generated result counts must come from parsed commands, not a lexical count of
the word `assert`, because comments, quoted modules, and annotations can contain
assertion-like text.

## VFS-Backed Executable Loading

For the first process, use `native_process_driver_start` on a fresh store after
mounting its VFS and configuring its runtime adapter. It copies argv/envp,
creates the initial memory/table/mutable stack global in C, and uses the same
executable loader and runtime initialization as guest exec. Invoke the entry
published in the driver's selection only after successful startup. The
resource provider is store-owned, shares immutable exports through reference
counting, and participates in the existing fork alias graph.

Keep initial startup recoverable: retain the original kernel while loading
against a candidate, release partial image/provider/call-binding ownership on
failure, and restore an empty runnable capsule for retry. Preserve descriptor
close-on-exec flags when cloning kernels. Require `() -> ()` process entries
and checked libc initializer signatures/results. Startup constructors and
runtime initializers must complete synchronously; normal entry execution may
yield through the existing process driver. Test a failed start followed by a
valid one in the same store, including failure after loading a real DSO.
`src/cli-rt/test` contains the private native/Wasm ownership
probe and its executable fixtures; ordinary package assertions remain WAST.

Use `native_process_driver_start_with_options` when a file interpreter must
fix its input format, choose an alternative `() -> ()` entry, or load readable
input without execute permission. Keep those options confined to initial
startup; guest `execve` still checks executable permissions. A memoryless
module without a startup hook can execute directly, while a module requesting
the C ABI startup block needs memory. Check these paths in both native and
browser builds of the shared ownership probe.

Treat an executable as an ordinary engine-VFS regular file. `execve` resolves
the pathname, checks execute permission and type, takes a bounded owned byte
snapshot, and validates or compiles that snapshot before constructing a
replacement process image. Production browser execution must not depend on a
parallel JavaScript blob map or executable-byte registry. A compatibility
registry may exist only for focused native lifecycle fixtures that do not
construct a VFS namespace.

Capture the resolved namespace pathname in the same operation as the owned byte
snapshot. Copy that identity into the process capsule when committing the image,
inherit it across fork, and replace it only on successful exec. Fork's evaluator
clone does not borrow the parent's image descriptor. Shared-libc
`waste_get_executable_path` calls the bounded `waste_kernel.executable_path_v1`
copy ABI, so LLVM executable discovery does not rely on argv[0], cwd, dladdr,
or re-resolving a path after rename/unlink. Check exact/short buffers, errno,
invalid pointers and atomic failure at the end of memory. This identity query
does not implement general realpath. Derive LLVM's private PATH_MAX and engine
image storage from the existing VFS_PATH_MAX_BYTES configuration. Private
include_next adapters must follow libc++ wrappers and precede the public SDK;
the native-generator and C++ bootstrap profiles keep their own header order.
See `src/aux/clang/test/executable-path` for the actual-source observer.

When unlink compacts the namespace table, rebase open-description references to
surviving nodes and detach references to the removed node. An open description
retains its file object independently; dup aliases must be adjusted only once,
and fork must accept detached regular descriptions. Captured process records
use explicit file positions because cross-fork shared offsets remain pending.

Use the shared component resolver for filesystem calls and loader snapshots.
Resolve each symbolic link before processing later `..` components, and resolve
relative targets from the containing directory. Preserve final entries for
readlink/unlink/rename and no-follow operations. Follow intermediate links even
with a final no-follow flag; check trailing slashes as directory requirements.
Creation may tolerate only a missing final component. Reject missing parents
and traversal through regular files. Bare language kernels synthesize `/tmp`
alongside the existing initial directories. Keep lexical normalization for
mount/seeding inputs, not guest pathname traversal.

The versioned `symlinkat_v1` import copies and validates both strings before
mutation, returns negative errno and leaves translation to shared libc. Allocate
the target before publishing the node so failure cannot leave an incomplete
link. Store its original bytes and length, independent of cwd or target existence.
When rename replaces a regular/link entry, use the unlink lifetime/compaction
path and re-find the source before moving its name; copying the source record
over the destination would preserve the wrong name and leave a table hole.
See `src/aux/clang/test/symlink` for compiled C++ calls, WAST assertions and
Bash execution through final and directory links. Directory-subtree rename and
symlink transport packaging remain separate capabilities.

Canonical lookup reuses this resolver through `waste_kernel.realpath_v1`; it
must not normalize away a missing, non-directory or inaccessible component
before consuming a later `..`. Check search permission on each traversed
directory and after a trailing slash, using the existing kernel credentials.
Keep span/capacity and injected-credential checks private when guest APIs cannot
reach those boundaries. Copy the complete resolved string only after validating
the caller's writable range, so a protected-page crossing cannot publish a
partial prefix. The import returns length or negative errno; shared libc handles
errno and optional malloc-owned output. Derive canonical storage and LLVM's
private PATH_MAX from `src/config.h`, without introducing a separate inventory
or a public pathconf capability. See `src/aux/clang/test/realpath` for WAST
assertions, compiled C++ calls and native/sanitized/production-worker steps.

Classify binary Wasm by its `\0asm` magic, not by a filename suffix. Textual
files use the explicit `/bin/wat` or `/bin/wast` handlers, a `.wat`/`.wast`
suffix, or a bounded shebang naming one of those handlers. Shebang splitting,
the optional single interpreter argument, recursion limits, and argv rewriting
belong to the loader boundary. Pass a shebang-stripped byte span to the shared
WAT/WAST parser; do not add general `#` comments to the grammar.

Keep loading transactional. Decode, validation, import checks, linking,
instantiation, startup-block creation, and entry lookup must succeed before
the old image is released. Preserve the old image and return the correct
failure status on any earlier error. Test binary files without `.wasm`, direct
and shebang WAT/WAST execution, malformed text, missing and non-executable
paths, repeated handler use, child exit status, and a later usable shell
prompt.

## Source-Built Bash

`make -C src/aux -j4 bash` stages `submodules/bash` under `build/aux/bash/source`,
applies the owned patch and compiles with Clang against the mounted guest SDK.
Host generators and configure outputs stay under `build/aux/bash/configure`.
`make -C src/aux install-bash` checks shared-libc imports, installs
`/usr/bin/bash`, the authored `launch.wast`, and the upstream license. The wizard delegates to
this target; HTML packaging only reads the installed tree.

The initial profile uses single-byte characters and disables NLS, network
redirections and conditional regex support. Configure's libc function answers
come from the actual provider's exports, with explicit package overrides for
unsupported public declarations/layouts. Keep the configure probe's `main`
export: otherwise Wasm section collection can discard the function being
checked and make a missing function appear available. Guest checkpoint and
terminal declarations stay private; host generator headers never supply the
guest ABI. Kernel imports remain in `env`; matching library imports are
rewritten to `libc` after linking.

Indirect calls through imported tables must resolve their function owner
through the caller's process clone bindings, including tail calls. A table
owned by the runtime can contain callbacks defined by the executable; cloning
only references owned by the table's defining module leaves child callbacks
entering the parent instance. Reset per-child exit bookkeeping after each
parent wake, so a later builtin-only child closes its pipe descriptors even
when an earlier child replaced its image with `exec`. The fork-isolation
fixture and the aux Bash pipeline/substitution session cover these paths.

Bind CRT-owned stdin/stdout/stderr through the initialized process-local libc
accessors before calling `__wasm_call_ctors`, not just before main. Engine stdio
initialization alone leaves those executable globals null during constructors.
The compiled [C++ startup fixture](../src/aux/clang/test/startup/README.md)
first reproduced 21 failures, then verifies both main-signature variants through
real prioritized constructors, redirected input/output, environment, existing
sleep continuations and current C++ archive allocation/ownership. Keep local
destruction distinct from the separately verified static-exit callback path. These constructors
run inside the CRT's ordinary `_start` evaluator call; the synchronous library
loader's constructor boundary described above has different yield constraints.
The fixture's global observer is trivially destructible without suppressing
registration flags;
descriptor streams currently write directly, so return output is not buffered
flush acceptance.

`src/vfs/root/test/guest-session/aux-bash.json` explicitly runs the installed source-built
command, including failures, file I/O, child processes and delayed interactive
input. Run `python3 src/engine/test/guest-session/guest-session-check.py --scenario aux-bash --page
build/html-rt/bash.html` after rebuilding the page. Both runtime startup paths
execute the installed `/usr/bin/bash` through the small authored launcher. Its
explicit argv/environment select `--norc -i`, `/root`, `/bin:/usr/bin`, xterm and
the stable `# ` prompt. The dynamic loader initializes shared libc and
stdio through the CRT. Direct kernel adapters resolve errno in the caller or
its process-local libc DSO, with shared-memory identity checks; production exec
must not depend on an `env` registration alias. Invalid arguments to the
synthetic `/bin/wat` and `/bin/wast` entrypoints report EINVAL; reporting
ENOEXEC would let Bash reinterpret their empty VFS nodes as successful shell
scripts. Other executable-format failures retain ENOEXEC and normal shell
fallback semantics. Do not embed executable bytes, bind private FILE slots
or rewrite compiled WAT in the bootstrap. See [the package notes](../src/aux/bash/README.md)
for prerequisites, limitations and the `bash-source-package` release target.

## Clang Cross-Build (In Progress)

The Clang aux port has its own staged LLVM source, host table-generator,
guest C++ runtime and compiler/linker build directories. Its bootstrap SDK
probe uses generated libc++ headers with the mounted guest headers; header
compilation alone does not establish runtime support. Preserve SDK unavailable
annotations and implement real missing capabilities before linking a compiler.
See [the package instructions](../src/aux/clang/README.md) and
[the active Clang plan](active-clang-plan.md) for its current incomplete status.

Freestanding C++ does not give an ordinary `main` C linkage. LLVM's generated
tool driver must explicitly expose C `main` in the guest profile to match the
shared C CRT. Check the actual object symbol and reject an imported `main` in
the linked executable. With dynamic unresolved-symbol imports, a link can
succeed while importing `env:main` and discarding the entire compiler/linker
driver; a small artifact and absence of C++ imports do not prove startup.

Inspect the host compiler driver's verbose command and child process when a
final link appears to wait after writing a valid Wasm file. Some host Clang
builds automatically run Binaryen's `wasm-opt` at `-Os`. The linker may already
have finished while the separate optimizer waits; wasm-ld's thread count does
not control that child. Feature-detect the driver's `--no-wasm-opt` flag and
disable that implicit pass for compiler-tool linking, retaining ordinary source
optimization. Measure optional whole-module optimization separately.

Audit a compiler component's callers against the generated target dependency
closures before enabling a broad C++ runtime feature. A source compiled into a
common archive can be unused by the selected executables. Exclude only a proven
unused guest component, retain native builds and required output paths, and
declare the omitted file in `LLVM_OPTIONAL_SOURCES` so LLVM's source inventory
check remains enabled. Re-audit when changing the target graph.

When one source file owns both required and unused algorithms, retain the
required implementation and exclude only the unused guest options/class/
entrypoints. Check callers across the full dependency source tree and confirm
the actual configured projects, targets and linker drivers. CodeLayout's
CDSort consumers are ELF/BOLT; the Wasm-only guest retains ExtTSP used by
MachineBlockPlacement. Native profiles keep both algorithms. Do not add a
private math approximation or fabricate a libc provider for the excluded
path. Verify complete source profiles, real object imports, option registration
and required API results. See `src/aux/clang/test/code-layout`; expanding
the guest graph requires re-auditing this selection.

For a source-backed output observer, keep LLVM's buffer/formatting implementation
and capture its abstract sink callbacks. Verify complete bytes, pending writes,
positions, flush/destruction and independent ownership in WAST. Vtables can keep
dependencies that the exercised methods never call. When isolating independent
upstream helper fragments for a fixture, preserve their definitions, license and
source hashes, fail when extraction boundaries change, and keep generated sources
under build. Such a fixture does not establish full platform translation-unit
compilation or providers; do not use test fragments as production replacements.

For aligned guest allocation, retain the ordinary boundary-tag header directly
before the returned pointer. Split alignment padding into reusable free blocks;
if a prefix would be too small for a block header/footer, advance to the next
alignment boundary. This keeps free/realloc/coalescing on their usual ownership
path. Round sizes with a wider integer before narrowing to internal block sizes.
Check both padding fragments, adjacent live bytes, failure output/errno and
large requests in WAST against the installed library on both runtimes. libc++'s
aligned-new helper rounds sizes before calling aligned_alloc and releases via
ordinary free; a successful C++ header probe alone does not verify that runtime.

Guest `fenv.h` models Wasm's fixed round-to-nearest arithmetic and absence of
sticky floating-point exception flags. Advertise only supported modes/flags
(FE_TONEAREST and FE_ALL_EXCEPT=0); reject requests to change an unsupported
mode or raise nonempty flags. Saved environments and flag snapshots are guest
four-byte values, with default-environment sentinel handling before dereference.
Do not import host fenv state into either runtime. Verify actual ties-to-even,
NaN/infinity/underflow results, save/restore, rejected requests and output/errno
preservation in WAST alongside C/C++ header/provider checks.

For guest integer conversion, derive limits from the guest C types, rather
than the native host's long width. The twelve shared `strto*`/`wcsto*` integer
providers use one unsigned-magnitude parser. Its input reader retains the full
wchar value; narrowing to a byte would misinterpret non-ASCII low bytes as
digits, signs, whitespace or NUL. Use element indexes and typed end-pointer
wrappers, with no transcoding buffer or pointer-to-pointer type punning. The
accepted whitespace and digits are the C ASCII set; this is not Unicode
classification. Validate bases before division; check the
limit's quotient/remainder before multiplying and latch overflow while still
advancing over all remaining digits. Permit the extra negative signed
magnitude without signed overflow. Unsigned negation wraps only for in-range
inputs; overflow must still saturate. Keep the original input as the no-digit
end pointer, and consume `0x` only when a hex digit follows it. Package WAST
checks results, end offsets, errno, input preservation and end-pointer canaries
for both guest widths and every supported base. Widen each ASCII fixture in
WAST to exercise both representations against the same expectations; also test
non-ASCII wide fixtures and signed wchar extremes. Capture actual C++ calls
through `<cwchar>`/`<cinttypes>` in compiled observers, with value, end offset
and errno expectations remaining in WAST. C++ SDK checks verify C linkage
and namespace return types independently of those runtime assertions.

For guest floating conversion, scan lexical boundaries independently of the
numeric representation. Preserve the original end pointer on no conversion;
roll back an exponent marker without digits, and recognize a hex prefix only
when a significand follows. Read full wchar elements using the same ASCII
scanner. Saturate exponent accumulation beyond every possible guest input-length
adjustment and still consume its digits, without exponent-sized floating loops.
Use bounded integer ratios and powers of five, then extract the target precision
and round by comparing the remainder against half the denominator. Test even
and odd ties and tails just above them; converting first to double can introduce
a second rounding for float and lose binary128 precision/range. Encode all three
IEEE representations directly, without using approximate quad helpers.
Derive the retained prefix and private word-array bounds from the widest guest
format. Every target rounding midpoint has a shorter terminating decimal
expansion; a nonzero discarded tail breaks an exact prefix tie. Keep trailing
prefix zeros in the scale, preserving the sticky tail, rather than expanding
large numerator/denominator arrays unnecessarily. Check the long-tail path in
WAST with nonzero digits after 20,000 zeros. Also test exact/inexact subnormals,
normal/overflow boundaries, signed zeros, infinity/NaN spelling, huge exponents,
non-ASCII wide characters, end-pointer units and input/output canaries.
Compiled C++ observers must capture actual float/double/binary128 return bits,
not narrow values to a common double before WAST assertions. Fixture data in
shared process memory must avoid provider-owned slots: a generated probe that
lands on `__errno_location()` overwrites its own input when setting errno.
Check the live slot address when diagnosing an isolated no-conversion mismatch;
move the probe data rather than changing a correct provider. The current policy
uses fixed nearest/ties-to-even rounding and detects tiny inexact results before
rounding; it leaves exact subnormals and successful conversion errno unchanged.
See the [C11 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
sections 7.22.1.3 and 7.29.4.1.1, for conversion grammar and end-pointer rules.

For bounded wide formatting, keep counts and capacities in wchar elements.
`swprintf` returns a negative result on insufficient capacity; it is not the
count-only/truncation contract of `snprintf`. Terminate the retained prefix
when capacity is nonzero and test exact fit, one/zero elements, errno and all
unwritten canaries. Preserve full `%ls`/`%lc` elements; decode `%s` as complete
UTF-8 characters and count precision in output elements. Reject invalid UTF-8,
unsupported format syntax and decimal field overflow explicitly. Keep derived
fractional precision in 64 bits: `%g` can add leading fractional zeros to an
`INT_MAX` requested precision even though the parsed field fits an int.
For floating output, decode the IEEE significand/exponent without quad
arithmetic. An exact terminating decimal expansion follows by multiplying
the significand by powers of two or five; keep the decimal scale separately.
Bound the private decimal storage from the widest guest format, round discarded
digits with ties-to-even and handle carry before selecting `%g` notation.
Produce hex digits directly from significand bits. This preserves binary128
range and low bits through `%Lf`, `%Le`, `%Lg` and `%La`; casting to double would
lose them. Exercise normal/subnormal limits, negative zero, ties and adjacent
values in WAST, and verify actual C/C++ variadic lowering independently.
The bounded wide provider does not establish narrow floating formatting,
wide stream IO, alternate locales or C++ owning-string startup. See the
[C11 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
sections 7.29.2.1 and 7.29.2.3, for wide formatting and buffer semantics.

Reuse that exact floating conversion for the narrow printf family, with a
private parsed-spec header and separate byte/wchar output contracts. A narrow
sink retains its bounded prefix and counts the full result; a wide sink stops
with an insufficient-capacity error. Emit only actual decimal digits, then
count zero/padding runs directly once narrow storage is full. This lets null
sizing and enormous precision return promptly without a precision-sized loop.
Check INT_MAX before narrowing counts and parsed fields. Preserve vararg
alignment and consumption across double, binary128 and subsequent arguments;
compiled C++ observations supplement raw argument-blob WAST checks. LLVM's
inlining statistics can use its raw sink and `format("%.4g", ...)` with these
shared providers, retaining native/threaded streams. See
`src/aux/clang/test/inlining` and libc's `narrow-floating-format.wast`.

Guest `math.h` classification macros use compiler builtins, preserving the
operand type and evaluating it once. The five `FP_*` codes are defined in the
public header; libc++ uses the same constants in its `<cmath>` overloads.
Keep math provider declarations under C linkage. Run compiled C/C++ observation
probes with volatile float/double/binary128 inputs and assert their classifications,
signs, call counts and unchanged errno in WAST. Include the smallest/largest
subnormals and normal boundaries to expose narrowing inside compiler helpers.
Binary128 comparison must inspect both words, distinguish all fraction bits,
treat signed zeros as equal and give the correct unordered result for each
compiler-rt helper. Narrowing through double incorrectly collapses subnormals
and merges distinct normal values. Exact comparison does not imply correctly
rounded binary128 arithmetic: existing arithmetic/conversion helpers still
use double internally.

When cross-building LLVM against generated libc++ headers, put those wrappers
before the guest C include directories, so `include_next` finds the public C
types. Select libc++ explicitly for the guest LLVM configure, while native
table generators retain their host library choice. Diagnose the source error
in CMake's configure log; a failed standard-library version probe can report
a misleading version error when header lookup actually failed. Never force
such a probe to pass or enable native host-header fallback.

Keep guest-only LLVM options in `src/aux/clang/guest.cmake`, separate from
the native table generators. Set `LLVM_HOST_TRIPLE` to the guest triple;
upstream's configure guess otherwise describes the machine running CMake.
Source-level host detection must agree with that configured guest identity:
LLVM's `LLVM_ON_UNIX` path otherwise includes `sys/utsname.h` and queries a
host release even for `wasm32-unknown-unknown`. Select the configured triple
in `TargetParser/Host.cpp` only for `LLVM_WASTE_GUEST`; preserve the optional
target-override environment setting and the native Unix path. Verify the
actual `LLVMTargetParser` archive's default/process triple and CPU fallback
with source-backed WAST in both runtimes.
The Generic platform cannot configure optional SHARED/MODULE outputs. Disable
the libLTO, libclang and Remarks C API libraries and plugins through their
upstream options, while retaining PIC and the static LLVM LTO/Remarks components
used by LLD. Disabling PIC would alter the intended guest linking profile.

Archive-only CMake probes establish compilation, not provider resolution.
In particular, `check_library_exists` can report a library/function as present
without linking it when `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` is used.
Audit LLVM's resulting capability flags against the actual guest SDK/providers
before accepting the executables. The guest profile explicitly excludes absent
separate pthread/dl/rt/pfm libraries; those flags do not declare individual
functions available or unavailable in shared libc. Configure success does not
establish native OS facilities or a usable guest POSIX support implementation.

Select LLVM's POSIX implementation through an explicit guest-only option,
not by changing CMake's Generic target to Linux or defining host OS macros.
`src/aux/clang/clang-waste.patch` adds `LLVM_WASTE_GUEST`, rejects other
toolchains, and sets `LLVM_ON_UNIX` without enabling native linker version
scripts. Apply the owned patch only to the staged source. Keep all underlying
operations subject to guest declarations, provider audits and runtime tests;
the backend selection itself does not make unsupported APIs available.
Explicitly exclude absent frame/unwinder symbols and unavailable pthread
rwlock initialization from archive-only configure successes.

Audit structure fields separately from their POSIX typedef names. The stable
guest `stat` ABI has 64-bit size/device/inode fields while existing
`off_t`/`dev_t`/`ino_t` remain 32-bit. LLVM's private metadata storage and
constructor parameters derive their types from the actual fields using
`decltype`, preserving all bits without changing installed command ABIs.
Probe the separate `st_mtime_nsec` member and use its matching access/modify
nanosecond fields in Unix status conversion. Verify C linkage in both
`sys/stat.h` and `fcntl.h`, including their shared declarations.
The compiled observer in `src/aux/clang/test/filesystem` uses real guest
status calls and actual upstream inline metadata. Keep synthetic large-size
checks distinct from the VFS's file-size limits, and leave full out-of-line
LLVM path/status acceptance pending until that source compiles and links.
For exact file timestamps beyond 2038, use the existing private kernel-clock
injection and assert the sampled clock bounds as well as status metadata in
WAST. Ordinary public-CLI runs check timestamps against live bounds. Do not
use the current `utimensat`/`futimens` stubs as successful timestamp setters;
they return ENOSYS. `HAVE_FUTIMENS` describes an exported function, not
successful mutation; LLVM's Unix/Path implementation propagates its errno.
Independent access/change timestamp mutation requires kernel support beyond
the compact mtime record and must not be approximated as a successful setter.

Clang's archive-only emulated-TLS check can also succeed without a runtime
definition. The guest profile disables `CLANG_HAVE_EMUTLS_GET_ADDRESS` because
neither shared libc, linked C++/Support archives nor the adapter provides it.
Keep the native generator's upstream probe. Check the actual provider and
consumer rather than treating a compile-only result as a runtime capability.

When LLVM's disabled-thread profile still names a libc++ synchronization type
omitted by the matching runtime, adapt LLVM's own wrapper under the explicit
guest option. Do not fabricate a type in `std` or enable unavailable pthread
capabilities. The guest `SmartMutex` reuses its recursive acquisition counter,
counts successful `try_lock` calls, and retains noncopyable ownership. Native
and threaded branches retain their real recursive mutex. These counters are
valid only for the compiler's single execution thread; engine/kernel scheduling
and synchronization retain their separate semantics. The compiled observer in
`src/aux/clang/test/mutex` exercises both wrapper variants and standard scoped
lock helpers with all functional assertions in WAST. It also links the current
bootstrap `libc++.a` to execute the real `llvm::call_once`; header compilation
alone would not establish that archive-backed initialization works.

Use the member's actual lock type when adapting unguarded LLVM/Clang owners:
the guest single-thread PerThreadAllocator, FileCollector, HashingOutputBackend
and AtomicLineLogger use LLVM's counted recursive mutex, while native/threaded
branches keep `std::mutex`. SmartRWMutex counts reader/writer acquisitions,
rejects conflicting try-locks and releases scoped readers exactly once.
Omit unused BalancedPartitioning pool synchronization members only where its
existing disabled-thread task methods cannot use them. Header/source compiles
do not establish the owners' full filesystem or task-pool runtime behavior.

For DWARF context caches, preserve recursive access and the selected state's
eager DWO parsing when adapting its lock. The upstream `isThreadSafe` label
selects that state; a counted lock in the disabled-thread guest does not prove
cross-thread exclusion. Include the complete actual context source in the
observer to access its anonymous state classes without production test hooks.
Supply valid sections through `DWARFObject`, then check real unit/DIE values,
cache identity, repeated source reads, eager/lazy selection and destruction in
WAST. A section callback can reenter another cache and yield through shared
`usleep` while the outer guard is live. Keep complete native/threaded syntax
checks and the generated guest object rules beside the runtime fixture; see
`src/aux/clang/test/dwarf-context`.

For GSYM creator and transformer locks, use that same guest mutex wrapper and
infer each guard's selected type. Check complete native/threaded sources as
well as the actual guest objects. A source-backed fixture can supply multiple
DWARF units, convert through the production archive, encode both GSYM versions
and byte orders, and decode through the actual reader. Keep expected ranges,
names, ownership and error outcomes in WAST. Overwriting the input encoding
after `GsymReader::copyBuffer` distinguishes copied reader ownership from an
accidental reference to the caller's storage. Exercise the transformer's
task-pool branch without interpreting its disabled-thread backend as evidence
of concurrent compiler execution; see `src/aux/clang/test/gsym`.

For CFG node attributes, replace the omitted string-stream owner with LLVM's
unbuffered `raw_string_ostream` only in the disabled-thread guest. Preserve the
native/threaded stream and the existing insertion/return logic. Test the actual
header method against complete production Analysis objects and real IR owners;
individual generated object rules can supply a fixture while the full archive
is unfinished. WAST should check exact bytes, including embedded NULs, empty
IDs and separators, and retained results after callback-owner destruction.
An ID callback can yield through shared `usleep` while the raw sink is live.
Default block frequencies establish cold/unprofiled formatting only; see
`src/aux/clang/test/cfg-format` for that boundary and complete source gates.
Its heat helper needs the shared guest-libc binary64 `log2` and `round`
providers. Normalize logarithm inputs by their IEEE exponent, including
subnormals, and reduce the mantissa around one before a bounded atanh series.
Test exact powers, high-precision reference vectors, domain/pole errno and
non-finite values. For ties-away rounding, subtract the truncated magnitude
before testing its fractional part; adding 0.5 first misrounds neighbors below
a half-integer. Preserve signed zero and already-integral large inputs. These
providers do not imply correctly rounded transcendental math or support for
additional precision variants; package assertions live in `log2-round.wast`.

The binary32 `log2f` provider explicitly promotes its input exactly, reuses
the binary64 implementation and narrows once to the Wasm binary32 result.
Domain/pole errno handling stays in the shared implementation. Verify float
results and errno independently, including every positive power of two,
subnormal and range-reduction boundaries, near-one inputs and integer-built
signaling NaNs. The selected high-precision vectors in `log2f.wast` use a
one-ULP bound; they do not establish general correct rounding. Check actual
C++ overloads and final function imports: a source spelling `std::log2(float)`
can introduce a `log2f` dependency through builtin lowering.

For LLVM's optional constant folding, distinguish the compiler's evaluation
providers from the generated program's target-library information. A missing
host math provider can return LLVM's ordinary no-fold result, preserving the
original operation. Keep exact APFloat shortcuts and supported computations;
do not replace missing math with guessed values or weaken SDK availability.
Apply the same conservative decision to side-effect queries before deleting
math calls. A null optional host function must be checked before clearing errno
or floating exception state. Native builds retain their host function pointers.
Test complete production objects through public folding APIs: scalar and vector
declines, exact special values, poison, supported providers, target availability,
no-builtin/nondeterminism controls and retained call arguments. See
`src/aux/clang/test/folding` for WAST and full source compatibility gates.

Clang's module cache, dependency filesystem, preamble file owner and driver
worker pool select compiler-local synchronization aliases. Native/threaded
branches keep standard mutexes and condition variables. In the disabled-thread
guest, ready predicates return with ownership intact; unfinished unbounded waits
report a fatal error even under `NDEBUG`. Timed waits release the counted lock,
use the existing LLVM monotonic backoff/shared POSIX sleep, then reacquire and
recheck. Notifications have no queue in this one-thread profile. Do not replace
the deadline with immediate success/timeout or create another scheduler.
Single-mutex scoped guards can use `lock_guard` when this libc++ profile omits
`scoped_lock`. Link actual cache sources against Support and current C++ archives
to verify owning buffers, stable cached reads, invalidation keys and generations
across unsafe unlock and old-owner destruction. Existing controlled clocks and
signals exercise timed predicates; WAST checks child rejection and parent
survival. See the [module-cache fixture](../src/aux/clang/test/module-cache/README.md).

Linking the cache's real pruning implementation also retains LLVM directory
iteration. Check public directory declarations' C linkage in C++: a header that
only passes C preprocessing can emit mangled `opendir`/`readdir`/`closedir`
imports with no provider. Redeclare available APIs with explicit C linkage in
the C++ SDK probe and emit calls. Verify the linked artifact's actual imports
and loading as well as its shared-libc import signatures; the latter does not
by itself reject unrelated unresolved imports.

For Clang's module compilation owner, reuse LLVM TaskGroup only under the
explicit disabled-thread guest profile; it already runs callbacks inline.
Queuing those callbacks until owner destruction would set its stop flag before
recursive module discovery can submit its work. Preserve the original mutex,
thread creation and shutdown joins in native/threaded profiles. Keep the actual
private owner in a staged header shared by production and its observer rather
than copying an implementation into a fixture. Adapt move-only compiler,
diagnostic, controller and advisory-lock captures to TaskGroup's copyable
function with one shared owning callable. Check reentrant capture cleanup,
service-owned locks outliving temporary cache objects and lock/capture lifetime
through the existing interrupted POSIX sleep. WAST owns those assertions;
full worker source compiles establish declaration coverage, not completed
dependency scanning. See [module task ownership](../src/aux/clang/test/module-tasks/README.md).

For LLD's diagnostic owner, adapt the mutex only under that same explicit
profile and let lock guards deduce its actual type. Keep native/threaded
standard synchronization. A localization-disabled libc++ can omit `std::regex`
entirely: use LLVM's already built regex implementation privately for this
guest path. Translate ECMAScript character classes and noncapturing groups to
POSIX ERE with explicit capture indices; exclude CR/newline where native `.`
would not match them, and anchor patterns used with `regex_match`. Check exact
location prefixes, multiline separation and duplicate splitting in WAST rather
than merely checking that the alternative compiles.
For full-string validators, also compare the captured full-match length with
the original string length: anchors alone must not accept a prefix before an
embedded NUL or trailing newline. Compare real guest results with the original
native std::regex implementation, including all byte values for narrow character
ranges. `src/aux/clang/test/spirv-name` checks the actual Driver helper.

Link LLD's real context and CodeGen/MC option initialization when testing
fatal diagnostics; do not stub context methods. Verify actual FileOutputBuffer
discard separately from memory lifetime, recover through LLVM's established
CrashRecoveryContext path, and test ordinary `_exit` in real guest children.
Managed shutdown, ordinary static callbacks and context cleanup have different
exit rules. A yielding cleanup callback must resume after arena destruction
with the context cleared. See the [LLD diagnostic fixture](../src/aux/clang/test/lld-diagnostics/README.md).

LLD's `unlinkAsync` is an optional output-removal optimization. In the explicit
disabled-thread guest, reuse its upstream one-thread fallback and let the
ordinary output writer replace the destination; a requested strategy count
cannot create a thread capability. Keep native/threaded detached-close behavior.
Link the real Filesystem and FileOutputBuffer owners to verify preflight leaves
the original inode intact, commit removes its name while retained descriptors
still read old bytes, and destruction/discard removes only temporary files.
Separate mapped-memory lifetime from temporary-file lifetime. Existing shared
sleep can interrupt this ownership interval; no new scheduler is required.
Exercise ordinary and LTO stream buffering, seek/read/write and device fallback
through those owners. See [LLD filesystem ownership](../src/aux/clang/test/lld-filesystem/README.md).

Public pointer-format macros must follow the compiler's actual pointer integer
aliases. A 32-bit pointer typedef can be `long`, while `uint32_t` is `int`, so
copying `PRIx32` into `PRIxPTR` is not sufficient. Use the compiler's matching
format definitions alongside its `intptr_t`/`uintptr_t` aliases and check real
C/C++ variadic calls with high bits and a following 64-bit argument. Also run
the upstream formatter that exposed the missing macro, rather than only a
header preprocessing check.

For an LLVM error-owner port, retain the real optional Error, executor and
payload lifetime. Centralize shared production aggregation under a guard whose
type follows the owner's field; preserve standard native/threaded locks. Test
queued captures, nested submissions, joined-error order, errors transferred
past backend destruction and reentrant cleanup while the owner is still live.
Suspend a real queued callback through the existing interrupted sleep controls
with both captures and errors alive. The
[ThinLTO error fixture](../src/aux/clang/test/lto-errors/README.md) uses the
actual base owner and both production aggregation callers; it does not establish
bitcode compilation or automatic draining of an unwaited backend.

Use upstream constructors and generated options in source-backed observers.
Compile the actual owning sources and let section collection retain their
required initialization; do not supply empty constructor stubs. Check data
imports as well as function imports: an unresolved `GOT.mem` options singleton
can remain even when every C++ function resolves, and the guest loader correctly
rejects it. For integer-only profile context formatting unavailable through
guest stringstreams, use LLVM's existing owning raw string stream and verify
exact binary bytes, full-width integers and returned-string lifetime. Preserve
the native/threaded stream paths and keep broader floating formatting separate.

Large generated LLVM option switches can exceed small evaluator control bounds
even when their Wasm is valid. Keep the decoder, validator, executor, clone and
continuation capacity coordinated through `EXECUTION_MAX_CONTROL_DEPTH` in
`src/config.h`. Measure the actual source-backed artifact before adjusting the
bound. The fixed 512- and 1024-block `control-depth.wast` regressions use standard binary
WAST to separate evaluator control depth from text-parser nesting, then checks
deep branches, `br_table`, early return and independent active caller/callee
control stacks. Resource-limit rejection remains bounded.
The Clang AST fixture reaches 528 nested blocks; its source-backed inventory
motivates the current 1024 bound. The native WAST context owns a driver with
about 8.5 MiB of inline continuation frames at this bound. Allocate that context
on the heap and release it after driver/parser cleanup, including for scripts
that supply an external invocation callback. The native session and checkpoint
owners already use heap allocation; the browser driver uses static storage.

Large source-backed fixtures can expose runtime allocation cost before guest
execution. Profile the production browser engine before changing memory caps or
adding code caches. Its private host allocator keeps free blocks in size bins,
physical previous/next links for coalescing, and a tail pointer for append and
in-place growth. Allocation searches free blocks rather than every live module
buffer; splitting and coalescing must update both structures. Free tails can
grow without wasting an earlier free span. Overflow or memory-growth failure
must preserve the old allocation and its bytes.

`make -C src/html-rt browser-allocator` runs this actual allocator with native
ASan/UBSan and a bounded-memory adapter; host libc allocation remains separate.
It checks alignment, fragmentation, coalescing, realloc/calloc, retained peers,
and failed growth. This is a private runtime ownership test, alongside WAST
checks through the packaged production worker; it cannot be replaced by guest
libc allocator tests, which exercise a different allocator.
Avoid copying an entire parsed `wast_group` onto the C stack to substitute a
resolved module: it is about 3 MiB at the current AST limits. Both WAST handlers
pass the module and raw-source views directly to the shared encoder. Preserve
binary/quoted-source handling and module-definition resolution while eliminating
the temporary copy; do not raise the host stack limit to hide the problem.

When invoking selected generated CMake object/PCH rules directly, first run
that component's generated `depend` target in a separate Make invocation.
The object build then reads the refreshed header dependencies. Skipping this
normal CMake step can retain a stale PCH after an SDK edit. Build required
generated headers through their actual targets as well. Staged upstream trees
without `.git` can discover the containing repository: pass LLVM's forced VCS
revision/repository options from the checked-out dependency when configuring
native generators and the guest compiler.

`LLVM_TARGETS_TO_BUILD=WebAssembly` selects LLVM code generators, not LLD
linker drivers. Scope guest driver selection explicitly in both CMake library
closure/aliases and the entrypoint table; keep the native default untouched.
Inspect generated link recipes and actual object references to confirm that
selection. Those checks do not establish final executable dispatch or linking.

Audit included headers as well as source-local threaded guards. Parallel.cpp
guards its executor fields, but Parallel.h originally named a standard mutex
and condition variable in its private Latch unconditionally. The explicit
disabled-thread guest boundary keeps an atomic count and checks completion;
TaskGroup already executes callbacks synchronously in that profile. Reject
pending waits/destruction and count wrap through real guest abort even under
`NDEBUG`. Preserve native/threaded blocking synchronization. The source-backed
observer in `src/aux/clang/test/parallel` tests actual TaskGroup/scalar
algorithms, capture lifetime and nested calls. Each invalid latch operation
runs in a real guest child; WAST checks SIGABRT wait status and parent survival
through native, sanitized and packaged browser runtimes. This compiler-local
profile does not change engine/kernel concurrent scheduling.

Preserve static ownership when adapting an LLVM profiler mutex. TimeProfiler's
guest registry selects its actual counted lock type, and scoped guards derive
their type from that field; native/threaded branches keep standard mutexes.
Its function-local vector still requires real `__cxa_atexit`. Do not replace
that registry with a leaked allocation, suppress static destructors or claim
full lifecycle acceptance from entry-storage tests. The observer in
`src/aux/clang/test/time-profiler` includes actual source types and checks
owning names/metadata, instant snapshots and moves with current C++ archives.
Flame-graph helpers truncate each time point to microseconds before subtraction;
truncating the duration instead changes fractional-boundary results. Check
negative and full-width timestamps explicitly. Unused global paths are discarded
by normal section collection; compile the complete source separately. Accept
global initialization, aggregation, finish/cleanup and JSON output through the
separate public-API observer linked against the actual Support archive. Its
six real usleep waits use existing injected clock controls in both runtimes,
so WAST compares exact JSON bytes without host timing tolerances. Check nested
same-name totals, async pairs, instant granularity, owning escaped metadata,
negative cross-instance timestamps and full-width realtime. Compare raw-stream
output with actual preferred/fallback/default file writers and their errors.
Delegate allocation observers to the real allocator and use an earlier C
atexit reporter to check that the actual static vector releases its backing
allocation once. Sequential finished instances exercise this disabled-thread
compiler profile; they do not establish concurrent LLVM thread profiling. See
the [profiler fixture](../src/aux/clang/test/time-profiler/README.md).

The observer in `src/aux/clang/test/synchronization` executes the actual
generic PerThreadAllocator, LLVM's out-of-line atomic ID function and current
C++ archives with an aligned observer allocator. WAST checks cache/move
ownership, forwarding and local destruction. It now retains normal static
registration flags, as does the optional-query fixture's real system_error.cpp.
The [shutdown fixture](../src/aux/clang/test/shutdown/README.md) verifies
compiler-generated globals/local statics, C callbacks, selective/reentrant
finalization and actual cache backing-allocation release. Its malloc/free
observers delegate to real allocation; registration flags and registry ownership
stay intact. Check exact traces and statuses from WAST after each exit mode,
including a callback using the existing sleep continuation. Never accept a
registration acknowledgement as proof that a callback ran.

Retire registrations before calling them and repeat list selection afterwards
so recursive finalization and new callbacks cannot duplicate an entry. Keep C
and C++ callback signatures distinct for Wasm indirect calls. Store registrations
in the shared guest libc allocation domain, not a host dispatcher. Exec's
post-commit allocator bootstrap must discard old callback nodes without freeing
or invoking them in the new heap. A rejected preflight must retain the old list;
fork uses the existing private clone graph. The shutdown regression first caught
an ASan use-after-free when a replacement program inherited a destructor from
its freed predecessor. Explicit DSO tokens do not prove automatic dlclose
finalization, concurrent registration or buffered stream flushing.

Audit the actual formatter provider before replacing a missing C++ stream.
The guest narrow `snprintf` currently lacks floating conversions, and LLVM's
raw double stream uses exponent formatting. Neither reproduces Mustache's
default six-significant-digit general formatting. The explicit guest numeric
helper instead calls the real exact wide formatter, bounds/checks its ASCII
result and returns an owning string; native LLVM retains its original stream.
`src/aux/clang/test/mustache` checks explicit text/length/terminators for
37 binary64 bit patterns and complete byte agreement with independently run
native classic-locale streams. Both observers record results; WAST owns all
assertions. The fixture also checks heap-backed concatenation, copy/move
identity, mutation isolation and substring lifetime using current C++ archives.
Compile the whole actual source separately and keep full template rendering,
general narrow floating printf and C++ startup/static exit acceptance distinct.

LLVM's managed registry is distinct from C++ static-exit registration. Its
global `ManagedStatic` handles are trivially destructible and construct their
objects lazily; explicit `llvm_shutdown()` destroys registrations in reverse
order. Nested constructors register dependencies before their owners, so an
owner's destructor can still use its dependency. Adapt the unavailable recursive
mutex only under the explicit disabled-thread guest profile, retaining the
native/threaded standard backend and upstream registry code. The source-backed
observer in `src/aux/clang/test/managed-static` checks repeated empty
shutdown, reuse, custom/array deleters and claimed-object lifetime with normal
static-registration flags. A claim transfers the object's lifetime but leaves
its registration until shutdown; do not reconstruct the handle before that
cleanup. These checks do not implement `__cxa_atexit` or establish full C++
startup/shutdown.

LLVM's ThreadPoolInterface also names futures even for SingleThreadExecutor.
Its guest-only LLVM-owned result retains a shared deferred callable and value,
reference or void storage. Submission/destruction do not execute it; wait/get
or the executor's queued thunk execute once. Keep native/threaded standard
futures and avoid types in `std`. The actual source-backed observer in
`src/aux/clang/test/thread-pool` verifies move-only capture/result lifetime,
aligned storage, shared references, recursive submissions and draining on
destruction. Preserve upstream queue semantics: a single-thread group wait
drains all groups, while a future get may force its task before earlier queued
tasks. Its later queued thunk must not execute it again. This disabled-exception
profile does not supply timed waits or exception transport. Link-time optimization
of actual ThreadPool/Threading sources removes unused diagnostic paths without
substituting providers; test normal and sanitized engines plus production Bash.

LLVM's disabled-thread guest ExponentialBackoff uses a private LLVM-owned
generator and POSIX deadline helper; native/threaded profiles retain their
standard random/thread backends. Keep the upstream uniform distribution,
exponential multiplier and caps. The current shared `getrandom` provider supplies
process-local xorshift compatibility bytes, suitable only for non-security
jitter; do not infer OS entropy or enable libc++ random-device support from it.
The helper calls actual shared `nanosleep`, yielding through the kernel's timed
readiness ABI. Recompute the remaining interval from the monotonic deadline
after EINTR: the current libc remainder does not accurately describe time left.
Other wait failures and failed random reads terminate through real guest abort.

Compile actual `ExponentialBackoff.cpp` with current C++ archives and the
production CRT/libc. The observer in `src/aux/clang/test/backoff` records
values only; WAST checks range/variation, expired timeouts and a live minimum
sleep. Its controlled scenario advances the injected monotonic clock early,
interrupts a wait and checks exact intervals plus the final deadline clamp.
Verify both raw native/browser API sessions and the packaged production worker;
controlled-clock assertions require that harness rather than standalone live
execution. Keep private helper headers in production build dependencies, and
compile native/threaded declaration branches separately. This boundary does
not establish complete LockFileManager ownership or compiler acceptance.

When a guest LLVM caller uses `getsid` only as a process-existence query, use a
positive-PID `kill(pid, 0)` adaptation under the explicit guest option; keep
native source on its original path. Check the complete error boundary: public
errno constants, libc++'s `std::errc`, engine negative errno and guest `-1/errno`
must agree. A missing public `ESRCH` can silently select libc++'s placeholder
value rather than produce a compile error. Do not remove a lock on unrelated
errors or infer remote process death from the local namespace. Propagate failed
host identity during lock creation; unavailable identity during liveness checks
requires a conservative answer. Do not add a fabricated session-ID provider.

The observer in `src/aux/clang/test/lock-owner` executes the actual private
LLVM helper and real guest imports. Keep assertions in WAST. Use two pipes to
hold a child in READ, query it without interruption, release it and observe EOF
from process exit. Query the still-allocated zombie before reaping; then verify
its status and absence. Include blocked-pending signals and success with stale
errno to distinguish a null query from delivery or an erroneous failure.
Exercise identity failure through the actual provider rather than replacing its
imports. Full source compilation verifies patch integration, but this helper
fixture does not establish complete file-lock acquisition/cleanup or C++ startup.

Keep public math constants separate from function availability. Guest
`FP_ILOGB0` and `FP_ILOGBNAN` derive from `INT_MIN` and `INT_MAX`; they are
integer constant expressions and do not imply an `ilogb` provider. LLVM's
private APFloat helpers consume them in overlay mode. The compiled observer in
`src/aux/clang/test/logb` uses the actual staged exponent helpers with
APFloat's no-errno/no-exception configuration and volatile binary32/64/128
inputs. Assert subnormal exponents and special-value sentinels in WAST without
narrowing long double or substituting a host implementation. Check the actual
source configuration before extending a private helper's error behavior.

When refreshing staged patched source, preserve the previous timestamp only
when the patched file is byte-identical. Changed files still invalidate their
dependents; unrelated unchanged patches do not trigger broad header rebuilds.
Keep applying the complete patch to a fresh staged copy with zero fuzz before
comparing timestamps, and leave the submodule untouched.

For LLVM's bare-Wasm endian fallback, keep the BSD `machine/endian.h` spelling
in the guest compiler's private include directory. Include the existing public
`endian.h` and alias its byte-order definitions rather than adding a second
source of values. Apply that directory through `guest-toolchain.cmake`; native
table generators and bootstrap libc++ use their own original header paths.
Verify the real upstream `llvm/ADT/bit.h` with a compiled observer and WAST:
check the native order, 16/32/64-bit swaps and actual stored bytes. Keep volatile
inputs so the inline operations execute. This header compatibility does not
select a POSIX backend or claim that the C++ runtime is fully usable.

When enabling libc++ wide characters, audit the public C headers as well as
the compiled providers. `wchar.h` must make `FILE` and `struct tm` visible
before libc++ namespace imports, even when localization is disabled. Keep
`wchar.h` and `wctype.h` declarations under consistent C linkage. libc++ supplies
const-correct search overloads above the C ABI; verify both pointer forms.
Check limit macros against the actual SDK typedefs too: Clang does not supply
`__WCHAR_MIN__`, and its default signed `__WINT_TYPE__` does not describe the
SDK's unsigned `wint_t`. Derive wchar minimum from the target maximum/sign and
make wint bounds match the public type. Verify these macros through libc++
include orders rather than assuming successful preprocessing expands them.
Guest wchar values are signed four-byte elements: compare without subtraction
overflow, keep memory counts in elements and choose overlap traversal by integer
addresses. Zero counts must not touch either pointer. Check embedded NULs,
signed extremes, search boundaries, returned pointers and canaries in package
WAST against the installed library. Compiled `wstring_view`/character-traits
observers establish only their exercised header/provider boundary. Do not use
stale wide-disabled archives after a wide-enabled rebuild fails, or claim that
header success establishes owning-string startup/runtime support. Resume the
real runtime build to discover numeric/formatting and other actual dependencies.
If a header observer needs libc++'s diagnostic-abort helper, compile the staged
upstream `verbose_abort.cpp` into a fresh test object and retain its real
shared-libc imports. Keep the library-build annotation flags and dependency
tracking; supplying an abort stub would hide a genuine runtime dependency.

## Coreutils Cross-Build and Distribution

Coreutils commands build through `src/aux/Makefile`. Each command directory
records its upstream objects in `sources.mk`; shared staging, configuration,
private compatibility headers and linking rules live in `src/aux/coreutils`.
The repository patch is `src/aux/coreutils/coreutils-waste.patch`.
Stage the read-only upstream checkout, including Gnulib, under
`build/aux/coreutils/source` before patching or bootstrapping. Configure and
compile under `build/aux/coreutils/configure`. Public headers come directly
from the mounted SDK; package-private gnulib/SELinux headers stay in the aux
profile. No build step writes into a submodule. See
[the submodule policy](submodule-policy.md).

```sh
make -C src/aux -j4 coreutils       # build all selected commands
make -C src/aux cat                # build one command
make -C src/aux install-cat        # explicitly install /usr/bin/cat
make -C src/aux install-coreutils  # explicitly install all selected commands
make -C src/aux coreutils-source-package
./start.sh --html-bash
```

One upstream Make invocation owns shared generators, objects and archives.
A separate archive copy removes gnulib implementations that conflict with the
guest ABI; final utility links can run concurrently. Images are written to
`build/aux/NAME/NAME.wasm`. Binaryen strips debug data, then the shared
`shared_libc.py` helper routes exact matching function signatures to `libc`.
Memory/table and kernel imports remain runtime bindings. Installation checks
actual provider signatures and serializes VFS publication; it does not require
stored source hashes or package approval reports. Generic SDK, ABI and VFS
Python helpers remain shared infrastructure.

Install commands as extensionless `/usr/bin/NAME` files; `/bin` resolves to
`/usr/bin`. Keep the engine-owned `wat` and `wast` handlers. Packaging reads the
current VFS tree, with normalized archive headers and separately recorded guest
mtimes. Build/install actions are explicit so packaging preserves local edits.

Generate corresponding source explicitly for releases and publish it beside
the browser artifact. The bundle records the actual staged source, build
inputs, source revision and utility digests. See
[coreutils-source-distribution.md](coreutils-source-distribution.md).

Retain focused utility tests and an aggregate session matrix across native and
packaged browser workers. The matrix verifies all selected commands plus `wat`
and `wast` sequentially in one Bash lifetime, including statuses, output,
redirection, a later prompt and clean exit. It does not need a browser GUI.

## Fast Native Testing

Build native application frontends and run files directly:

```sh
make -C src/cli-rt applications-native
build/cli-rt/wasm src/vfs/usr/bin/bash -c 'printf "hello\n"'
build/cli-rt/wat --entry probe program.wat
make -C src/cli-rt application-check application-check-sanitize
```

Runtime options precede FILE; everything after it is a guest argument. Use
`--` before a filename beginning with `-`. Set the guest environment with
repeatable `--env NAME=VALUE`, and choose another installation with
`--vfs-root DIRECTORY`. Default root discovery follows the actual executable
location rather than cwd. External files are copied into a reserved guest
staging directory without exposing siblings or changing the host VFS tree.
An existing staging directory is an error. Ordinary guest execution still
checks execute permission; explicit file interpretation requires readable
input. Repeatable `--stage-file /guest/path OCTAL_MODE HOST_FILE` explicitly
copies supporting files into that session; it does not write through to the
host tree. Aux/system Make tests use this option with the public `wasm`
frontend to run authored WAST inside Bash. Limits come from `src/config.h`.

Application output has no default harness JSON or deadline. Add
`--result-file NEW.json`, `--timeout-ms N`, `--trace-process` or `--trace-waits`
when collecting evidence. Applications inherit actual stdio types and access
directions, including pipes/files and mixed terminal streams. Private native
duplicates retain those capabilities while guest dup/close aliases refer to
opaque OFD IDs. Host TTY mode is raw during execution and restored at cleanup;
the kernel/guest own input processing, Readline and output translation.
Host signals reach the guest foreground group. Default termination returns
128 + signal to the host and records the raw signal in waitpid status; caught
handlers run at blocking READ/SELECT/pselect boundaries, with READ restart flags.
Self-signals dispatch before kill/raise returns. Canonical echo, erase and line
kill are shared kernel behavior. Native pump checkpoints service terminal input
during CPU-bound execution; timeout/cancellation retain their separate statuses.
Inherited regular-file stdio is stream transport: seeking and
complete host metadata are not implemented.

Run the host stdio/PTY gates without Node or a desktop browser:

```sh
make -C src/cli-rt terminal-check terminal-check-sanitize
```

Their authored WAT fixture and Python driver live under
`src/cli-rt/test/terminal`. They use private PTYs, compare restored
termios exactly, inspect stream identity and alias routing, and verify binary
input, Readline editing/EOF, dimensions, timeout and host-signal cleanup.
The ordinary gate also leaves Bash idle for 31 seconds before another command.
SIGWINCH checks both kernel ioctl dimensions and updated Bash variables. Use
`--trace-waits` to synchronize with an actual READ/SELECT wait before injecting
the signal: receiving command output alone does not establish that boundary.
The resize and interruption contracts also run against the packaged worker:

```sh
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/cli-rt/test/terminal/resize.json
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/cli-rt/test/terminal/interruptions.json
```

Reports go under
`build/system-tests/cli-runtime/terminal`. The private session and standalone
WAST contexts retain deterministic modeled terminal descriptors for portable
fixtures; real host identity belongs to application startup.

The application host-boundary checks use
authored WAT fixtures and a Python driver under
`src/cli-rt/test/entrypoints`, with generated fixtures/results
under `build/system-tests/cli-runtime/entrypoints`. They require no Node or
GUI browser. Standalone WAST uses the complete streaming conformance runner:

```sh
build/cli-rt/wast --verbose src/vfs/root/test/aux/libc/allocator.wast
build/cli-rt/wast --json src/vfs/root/test/engine-regressions/shared-libc.wast
build/cli-rt/wast --suite --jobs=2 --expected-failures=src/cli-rt/test/native-corpus-expected-failures.txt --results=build/cli-rt/corpus-results.json
make -C src/cli-rt wast-check wast-check-sanitize
```

Successful scripts are quiet by default. `--verbose` prints assertion progress,
totals and the first failure; `--json` emits one complete report per file and
routes guest writes to stderr. Multiple files use isolated stores and retain
an aggregate failure status. Empty scripts pass, setup failures fail without
invented assertions, and truncated scripts preserve earlier assertion evidence.
Report completeness and reaching script EOF are separate properties: a complete
receipt can describe a truncated, failing script.

`--context auto` supplies production resources for ordinary `libc` or
`waste-runtime` imports. It preserves script-owned providers and linkage
assertions by leaving the whole file in language context when either occurs.
Use `--context language` for a deliberately empty provider/descriptor namespace
or `--context runtime` for production libc/stdio; the latter rejects namespace
conflicts. Context memory/table sizes follow declared import minima, within
`src/config.h` limits. Declare callback table slots in the imported table
minimum; relying on an oversized table inherited from Bash hides missing
requirements. Context setup uses the shared production loader and initializer,
without a Bash bootstrap or static test libc.

Language scripts can resume finite SELECT deadlines; external terminal input
requires runtime context. Standalone scripts and batch children cannot launch
nested host batches. The private host boundary driver and authored WAST fixtures
live under `src/cli-rt/test/wast`, with reports under the matching
`build/system-tests` path. It verifies CLI, evidence, namespace and isolation
contracts; package assertions remain in package WAST files.

Cache common engine/runtime objects per native linking/sanitizer profile.
Declare generated inputs through the engine generation goal so fresh parallel
builds wait for their headers. Use grouped targets for generators producing
paired files; independent rules for the pair can run the same generator twice.
Check both a fresh build and a repeat build that schedules no C compilation.

Use the mmap/native CLI path for parser, encoder, decoder, linker, validator,
and executor iteration.  It avoids Node and browser startup while running the
same platform-neutral engine code:

```sh
make -C src/cli-rt wast
build/cli-rt/wast --parse-only FILE.wast
build/cli-rt/wast --verbose FILE.wast
```

Use focused fixtures for one grammar or runtime boundary, including a matching
negative fixture when failure classification matters.  Then run the broader
gate:

```sh
./start.sh --cli-test
```

Native unit tests compile with warnings as errors, AddressSanitizer, and
UndefinedBehaviorSanitizer.  LeakSanitizer is disabled only where the ptrace
environment prevents it from operating reliably; this is not permission to
ignore ownership leaks.

Move guest-observable C driver assertions into authored WAST with a recorded
argument/result/trap mapping. Keep direct C checks for embedding APIs, real host
callbacks, source/instance ownership, clone/page identity and teardown. The
executor smoke gate runs both retained C probes and the portable
`engine-regressions` group with ASan/UBSan; it compares installed fixture bytes
against authored inputs before using distribution snapshots. WAST instance
isolation complements the C decode-once/instantiate-twice lifetime check rather
than establishing that private ownership contract.

Use real guest imports for caller-memory and continuation counterparts: a
pipe round trip observes the executing module's bytes, and a positive timed
SELECT supplies a production wait boundary. Guest entry/completion counters
can detect repeated side effects after resume, but cannot establish the C
snapshot API's capture/restore/replay contract. Keep that sanitizer gate.
Private C memory probes must use bounded `exec_memory_read` and initialized
sparse-page observations rather than a removed contiguous `memory->data` field.
The native WAST adapter services finite SELECT deadlines with a monotonic clock
and bounded host sleeps; external input and indefinite waits use the session
driver. Verify POSIX fixtures through C native/browser parity and the retained
sanitizer/session contracts.

Path regressions can create private files with real guest imports instead of
depending on installed executable bytes or host filesystem writes. Assert the
guest return value and errno separately for failed operations. Preserve the
versioned import's negative-errno contract and perform libc's translation in
the guest wrapper where needed. Check public stat fields at the documented
Wasm32 offsets; keep private codec round trips, explicit byte-span bounds,
seeding ownership, mtime and snapshot ownership under direct C sanitizers until
equivalent guest capabilities exist. Compare only the initialized pathname
and terminator when inspecting getcwd buffers.

Use zero-timeout SELECT to expose pipe readiness through the guest ABI, while
retaining raw HUP/ERR bits and reference ownership in C probes. Dup2 tests
should transfer bytes through the replaced descriptor and verify same-fd flag
preservation, rather than checking only that a descriptor appears open. For
zero-count I/O, validate the guest range and delegate descriptor bounds to the
kernel with a non-NULL stack byte. Keep that byte separate from the owned heap
buffer so ordinary cleanup never frees stack storage. Record existing import
error conventions explicitly; raw kernel statuses and libc-translated errno
must not be silently interchanged during fixture migration.

Pipes start in blocking mode. An isolated language regression that expects
EAGAIN must explicitly set `O_NONBLOCK` with `F_SETFL`; an empty blocking read
belongs in the full process driver and must yield until data, EOF or a signal.
Do not make guest semantics depend on whether a terminal was attached. Raw
kernel blocking EAGAIN registers a wait; nonblocking EAGAIN does not. Guest
adapters yield only for the registered wait. Keep `F_GETFL`/`F_SETFL` state on
the shared OFD and `FD_CLOEXEC` on the descriptor. Check status changes through
aliases and fork, immutable access mode, append placement, and both fixed and
variadic import arguments. `src/vfs/root/test/system/scheduling/pipe-modes.wast` checks
restored blocking reads, EOF wakeup and append aliases; `cpu.wast` checks
blocking transfer backpressure. Test closed descriptors owned by the fixture;
initial stdio differs between language and production terminal contexts, and
closing the WAST command's own input can disrupt handler completion.
For terminal reads, copy the effective VMIN threshold into the READ wait record
and honor it during registration and polling. An ordinary readiness query can
report buffered bytes while that particular read still requires more; do not
cancel its wait or expose raw EAGAIN. Nonblocking reads bypass that threshold.
Native session input belongs to the terminal capability, so locate a surviving
terminal alias before delivering host input or EOF; fd 0 may now name a pipe.
Regular/directory descriptions are cloned to rebase namespace pointers. Retain
their status object separately so `F_SETFL` changes remain visible across fork,
release it on final description teardown, and reject saturated reference counts
before publishing a clone. This does not establish shared regular-file seek
cursors across fork; that remains a separate ownership change.

Check WAST completion both as a forked command and after root Bash replaces
itself with `exec wast`. Bash can also exec its final command implicitly.
The root invocation must release the handler context and preserve success or
failure status without entering the child-only notification/reap API. Retain
an intentional failing report alongside the successful fixture to verify its
diagnostic and nonzero status in native, sanitizer and packaged-worker runs.

Hard-link aliases must retain the same inode object, rather than copying file
bytes or treating an open-description reference count as `st_nlink`. Store
regular-file attributes and symlink target bytes with the retained inode so
chmod/write/truncate and an unlinked descriptor observe the same state. Allocate
new inode numbers from a kernel-owned sequence shared by fork; deletion and
child creation must not reuse existing identities. Rebase directory-description
pointers into a cloned namespace, including descriptor aliases, before the
parent can be destroyed. Keep the namespace's current link count separate from
references held by fds, mappings and cloned kernels. The current clone/reap
namespace model still needs immediate mutation visibility and deletion handling.

Preserve existing wire records when adding metadata fields. `fstatat_v1` and
`path_stat_v1` retain their 48-byte metadata record; `fstatat_v2` returns the
existing 128-byte guest stat layout with namespace link counts. Validate the
complete destination before copying a successful result, and leave it untouched
on failure. The [hard-link fixture](../src/aux/clang/test/hardlink/README.md)
checks both layouts and canaries in WAST, with ordinary-CRT C++ observations
including fork/wait. Opaque inode ownership and clone teardown remain private
sanitizer checks.

Record locks must follow retained inode identity across independent opens,
hard/symbolic links, rename/unlink and path reuse. Keep process lock ownership
separate from fd/open-description ownership. Transactional clone teardown must
preserve an owner's records; actual fork needs a fresh owner before publishing
the child. An exec retains its owner, but closing any CLOEXEC descriptor releases
all its records on that inode, even if another alias survives. Retain inode
references for records and copied wait interests; release them on cancellation,
exit and failed startup. Bound record admission/deadlock traversal through the
shared config and prepare replacement/split/coalesced records before publishing.

Use the existing SELECT wait record and polling scheduler for F_SETLKW. Save
absolute byte ranges before yielding; SA_RESTART must preserve that range even
when a signal handler changes the descriptor's offset. Revalidate the fd on
resume, report EINTR without restart and detect process wait cycles as EDEADLK.
Guest struct flock bytes and commands share `waste/abi/fcntl.h`. Shared libc
reads pointers for record commands, integer arguments for descriptor commands,
and no third argument for queries. Fixed `fcntl_v1`/legacy env calls receive
the record pointer directly; compiled variadic calls pass a slot containing it
through libc or `fcntl_varargs_v1`. Validate complete input/output spans before
copying, including output crossing a protected page. The
[record-lock fixture](../src/aux/clang/test/locks/README.md) checks actual
LLVM blocking methods and ordinary processes in WAST; opaque owner/wait/refcount
and admission invariants remain native sanitizer checks. Timed tryLockFile and
full Path/C++ shutdown need separate acceptance.

## Test Boundary Selection

Choose the layer that can observe the invariant. Moving guest behavior into
WAST does not remove the need to test private state or the browser boundary:

| Boundary | Retained verification |
| --- | --- |
| Language and guest-visible POSIX outcomes | Authored engine/package/system WAST and installed corpus execution |
| Post-yield input, clocks, signals, fork/exec and terminal transcripts | Shared `src/vfs/root/test/guest-session/*.{wast,json}` contracts with host checkers in `src/engine/test/guest-session/`; see [native sessions](native-guest-session.md) |
| C API and ownership | Direct sanitizer gates for callback identity, decode-once instances, snapshot/replay, clone binding, page aliases, atomic exec transitions, handler cursors and teardown |
| Compiler ABI | Compiled guest C probes for public header widths, offsets, alignment, varargs and callback signatures; do not translate away the compiler being checked |
| Host adapters | Worker/DOM tests for message ordering, malformed controls, cancellation, FileReader, Blob downloads and allocation failures |
| Distribution | Inventory/hash/metadata and exact-byte checks, tar extraction, offline references and mounted-path shell probes |
| Real browser | Offline `file://` boot, DOM/worker behavior, rendering and downloadable reports; worker VM tests alone cannot establish these |

Keep private NULL/overflow guards, raw readiness bits, injected credentials or
clocks, object/reference identity and independent-kernel checks even where
guest calls cover corresponding visible effects. Direct mapping reads reject
ranges beyond EOF; ordinary POSIX reads may return partial bytes. Preserve
these distinct contracts. [Retained kernel checks](posix-kernel-retained-coverage.md)
and the compiler/private boundaries above retain these distinct contracts.

Tests must call the production implementation to establish its coverage. The
legacy `c-engine-shared-lib-dylink.c` probe duplicates a parser, and its checked-in
PIC fixture's metadata differs from a fresh source build; it is not evidence
for the production loader. Likewise, building/import-auditing a CRT fixture
does not establish that `_start` ran. Preserve these limits when evaluating
shared-library and executable startup acceptance.

For setup-report regressions use `tests/test-suite-setup-*.wast`: empty/valid
zero-assertion streams, invalid/unlinkable/start-trapping/unknown-definition/
encoding failures, and recovery followed by passing assertions. Also check
truncated tails, XFAIL/XPASS and diagnostic capacity failures. Diagnostics own
their strings and survive store teardown; reset them between runs. Do not let
allocation failure silently truncate the report or turn a failed setup into PASS.

## Installed Corpus Workflow

Authored guest tests live in `src/vfs/root/test/` as the single authored
source. The pinned spec submodule continues to supply the external corpus and
remains read-only. The in-progress test tree migration in
`docs/active-test-migration.md` is still consolidating per-package
`src/aux/NAME/tests` directories; during that slice, edit tests at whichever
of the two possible homes is authoritative, since the corpus collector
prefers the legacy source and falls back to the canonical VFS location.
Package and system WAST mount under `/root/test/aux/NAME` and
`/root/test/system`, require Bash process context, are listed as skipped in
isolated language batches and are run with `/bin/wast --verbose` from Bash.

```sh
make -C src/html-rt vfs-tests-install
make -C src/html-rt vfs-tests-check
make -C src/cli-rt corpus-native
build/cli-rt/private/test-suite --vfs-root=src/vfs --list
build/cli-rt/private/test-suite --vfs-root=src/vfs --group=engine-regressions --json
```

Installation verifies candidate test/companion consistency, then publishes test
files, manifest and license. An explicit refresh replaces managed corpus files,
including local edits; it needs no selection approval flag. Packaging discovers
the current VFS tree without rebuilding tests or comparing them to sources.
The explicit `vfs-tests-check` gate still checks corpus selection, test bytes and
policy against authored/upstream inputs; unrelated source hashes are not gates.
Native `corpus-native` writes
`build/cli-rt/corpus-results.json` and does not refresh the distribution.

Repeat `--group`, `--exclude` and `--exclude-group`, or select positional
identities, mounted paths or filenames. Positive selections combine before
exclusions. Use `--jobs`, `--timeout-ms` and `--timeout-group=NAME:N` to bound
runs; `--results` saves a host report for native `wast --suite`. Runtime limits
come from `src/config.h` and policy files, not dated documentation counts.

In `bash.html`, **Diagnostics → Installed tests** lists/selects/runs/cancels
the same corpus and downloads JSON without Node. Interactive renderer fixtures
are skipped in batches; run them with `wast` as described in terminal verification.
The guest command also runs from native or browser Bash:

```sh
/bin/waste-test --list --group=engine-regressions
/bin/waste-test --jobs=2 --results=/tmp/results.json setjmp-buffer-copy.wast address.wast
/bin/download /tmp/results.json
```

Guest `--results` and stdout redirection write guest files. Parent directories
must already exist. Exit codes are 0 for accepted outcomes, 1 for failures,
XPASS or timeouts, 2 for invalid options/unavailable capability, and 130 for
Ctrl-C cancellation. Reports preserve assertion/setup details and manifest
order; Bash remains usable after a batch. Refresh the launcher explicitly with
`make -C src/cli-rt guest-test-install`, rebuild the page, and use
`make -C src/cli-rt guest-test-check` for native/production-worker parity.

Keep page generation, focused checks and browser acceptance separate:

```sh
./start.sh --html-bash
./start.sh --html-check
python3 src/html-rt/test/test-corpus-bash.py build/html-rt/bash.html
node src/html-rt/test/vfs-browser.cjs build/html-rt/bash.html
make -C src/cli-rt vfs-check
```

The shell sweep checks guest redirection (`--limit N` bounds diagnosis); the
compiled-engine VFS probe checks exact mounted bytes and metadata. `--html-check`
uses Node without launching Chromium. `./start.sh --html-browser-full` explicitly
launches Chromium; manual offline-browser evidence is separate from worker VM
results and is useful on hosts where browser automation crashes.

The packaging harness extracts the embedded tar via `readOfflinePackage`
without opening a browser. Its synchronous checkpoints append to
`build/html-rt/frontend-packaging.log`, overridable with
`WASTE_FRONTEND_PACKAGING_LOG`. Compare large buffers with `Buffer.equals` and
concise path/length diagnostics to avoid constructing enormous failure diffs.
The last checkpoint narrows a crash location but does not establish its cause.

## Wasm32 Varargs and Callback Adapters

Decode C varargs through the guest ABI before calling a fixed-width host import.
Guest `open(path, flags, ...)` extracts mode only for `O_CREAT` and calls
`waste_kernel.open_v1(path, flags, mode)`; the kernel applies the creation mask
(initially `0022`). Bash's variadic `fcntl` passes a guest argument-area pointer.
`waste_kernel.fcntl_varargs_v1` reads the integer there, while `fcntl_v1` and
compatibility `env.fcntl` take a direct integer. Mistaking the pointer for an
`F_DUPFD` minimum produces descriptor exhaustion during shell redirection.
Verify guest redirection as well as lower-level VFS access.

Check a public formatter declaration against both the provider's Wasm signature
and real compiler-generated variadic calls. `vsnprintf.wast` covers bounded
shared-libc output; `src/aux/clang/test/stdio` compiles C and C++ `<cstdio>`
clients that record observations, with all assertions in WAST. Test count-only
calls, truncation and exact fit, untouched canaries, errno and reusing the same
`va_list` before reading its original first argument. Put libc++ headers before
the guest C SDK headers so its `include_next` wrappers find the public ABI.
Define public integer-format macros from the actual guest types: small unsigned
fixed-width arguments promote to int, uint32_t uses unsigned int, and uint64_t
uses unsigned long long. Verify the macros through compiled C/C++ calls, with
zero/maximal values, high bits, mixed-width alignment, truncation and case
selection; a header-only compile cannot check the shared formatter's ABI.
Use the ordinary provider/import assignment for C++ namespace aliases: a late
attribute redeclaration can leave `std::vsnprintf` bound to an earlier raw
import. Explicit C linkage for a fixture main matches the C CRT under
`-ffreestanding`; the full C++ driver entry and constructors still need separate
startup acceptance. Do not infer complete printf conformance from this subset.
`src/aux/clang/test/wide-format` adds real `<wchar.h>`/`<cwchar>` calls,
including mixed 4/8/16-byte variadic arguments and direct `swprintf` calls.
Keep the beginning of synthetic WAST argument areas aligned to the widest
argument: an eight-byte-aligned area can pass double cases while every
binary128 case reads the wrong bytes. Compiler observers only capture buffers,
counts, errno and argument cursors; all output expectations remain in WAST.

Function-table calls must retain exact Wasm signatures. Fix callback types in
owned build inputs when adapting a package; do not weaken validation or patch
compiled function indices in a launch script. Compiled guest ABI/layout probes and
shared native/browser read-to-prompt sessions cover different parts of this
boundary; use both when changing it.

## Browser and Differential Testing

When measuring a guest linker, distinguish LTO used to build the executable
from LTO support compiled into it. The latter embeds LLVM's optimizer and code
generator to turn bitcode inputs into objects. An ordinary Wasm object/archive
linker can omit that support while still accepting code compiled with per-file
optimization. Keep a separate comparison artifact, reject direct and archived
bitcode explicitly, inspect the map after section collection, and test both
successful linking and execution of the emitted module. See the
[non-LTO LLD trial](../src/aux/clang/lld-no-lto.md). This does not remove the
optimizer from Clang itself or implement deferred loading of LTO support.

A compiler's hosting format and output target are separate. Wasm-hosted Clang
can emit AMD64 COFF when its X86 backend is built. Bypass Wasm-specific
`clang.cfg` defaults with `--no-default-config` for a freestanding cross-target
compile; do not pass the guest Wasm CRT or libc to the Windows link. Keep the
COFF linker in its own package/profile, rejecting unsupported LTO/PDB requests,
and reuse LLVM's existing deferred-task ownership for guest-only future adapters.
Host object preflights do not prove guest compilation; inspect real guest
outputs with bounded WAST checks, then verify the downloaded executable on the
target OS. See the [minimal COFF package](../src/aux/lld-link/README.md).

Guest linking must also produce the runtime's library namespaces. WASTE
registers a `.so.wasm` provider under its basename without that suffix. The
private LLD named-import profile resolves actual shared-provider symbols and
signatures before assigning that module name to shared function imports.
Fixed-address commands import process memory and can use this binding without
PIE relocation globals. Verify that local definitions and raw kernel imports
retain their existing bindings, reject incompatible provider signatures, and
execute the emitted file without a host rewrite. The
[SDK link fixture](../src/aux/clang/test/lld-link/README.md) tests this
compiler/linker/runtime boundary; host-compiled inputs do not prove guest
compiler execution.

Keep a source-built guest compiler's resource headers separate from the host
bootstrap header snapshot. Clang's upstream `core-resource-headers` and
`webassembly-resource-headers` CMake install components provide the portable
and Wasm-only headers without installing headers for unconfigured backends.
Stage them under `build/`, derive the resource version from generated
`clang/Basic/Version.inc`, and publish them through an explicit install target.
Use Clang's ordinary sibling `clang.cfg` lookup for guest defaults; its
`$`-prefixed options apply only to link actions. This keeps the guest CRT and
shared-provider inputs out of `-c` and `-fsyntax-only` commands. Verify real
guest compile/link/run behavior separately from file copying or host-driver
configuration preflights. See the [Clang package](../src/aux/clang/README.md).

After native gates pass, run focused WAST worker tests from their generated
payload and exercise the unified offline Bash/test page:

```sh
node src/html-rt/test/browser-runtime.cjs
./start.sh --html-bash
node src/html-rt/test/offline-browser.cjs --page=bash
```

Interactive runtime changes also run:

```sh
./start.sh --html-bash
node src/html-rt/test/bash-browser-runtime.cjs build/html-rt/bash.html
```

Supported official Wasm/WAT/WAST language tests may be cross-checked against
the OCaml reference interpreter via `./start.sh --ocaml-reference`; see
[OCaml reference interpreter build](#ocaml-reference-interpreter-build). Use native/browser C
checks for kernel, shared ABI, scheduler, signal, process and libc behavior.

Each scheduled test needs a fresh store and kernel.  Run isolation-sensitive
fixtures in different orders and concurrency settings.  Imported-memory tests
must still observe intentional aliases inside one sandbox.

### OCaml reference interpreter build

The OCaml reference interpreter in `submodules/wasm-spec/interpreter` was used
as a language reference while implementing the WAT/WAST portions of the C
engine. Use it only to cross-check Wasm/WAT/WAST language behavior. WASTE
runtime and kernel development belongs in C; no additional kernel development
is planned in OCaml. The procedure below reproduces upstream reference behavior.

The upstream interpreter requires:

- `opam`
- `dune`
- `menhir`
- OCaml &ge; 4.12 (per the upstream README)

Install them through your system package manager and `opam` as usual.

From the repository root:

```sh
./start.sh --ocaml-reference
```

That subcommand invokes `make -C submodules ocaml-test`, which:

1. Copies `submodules/wasm-spec/interpreter` to `build/ocaml-interpreter/`
   (via the `ocaml` target).
2. Runs `make` in the staged copy.
3. Runs `make test`, which executes the upstream spec test suite.
4. Logs output to `build/engine/logs/ocaml-reference.log`.

The individual targets are also available directly:

```sh
make -C submodules ocaml         # stage and build without the test suite
make -C submodules ocaml-test    # stage, build, and run the spec tests
make -C submodules ocaml-clean   # remove the staged copy
```

The checked-out submodule remains read-only; every generated build output
belongs under `build/`. The staged copy under `build/ocaml-interpreter/` is
disposable.

## Diagnostics and Error Quality

Use structured errors with a category, source location when available, and a
short stable message.  Never replace a useful decoder, validation, linker, or
trap result with a generic “module load failed.”  Freestanding formatting must
retain diagnostics needed by browser results.

For a failure cluster, identify the earliest phase that diverges:

1. command framing;
2. text parsing and deferred resolution;
3. binary encoding;
4. binary decoding;
5. semantic validation;
6. import resolution;
7. instantiation and initializers;
8. invocation; or
9. result comparison.

Use independent tools such as Binaryen validation only as a triage signal.
They do not replace the project validator, especially for proposal and WAST
assertion semantics.

## Common Failure Patterns

- Do not special-case a known fixture's exact source or argument values.
- Do not silently accept unsupported syntax or opcodes.
- Do not discard invalid expressions that an `assert_invalid` command needs to
  observe.
- Do not let one malformed WAST command terminate the rest of the file.
- Do not maintain duplicate LEB, import, comment, annotation, or string
  scanners.
- Do not compare module-local type indexes across instances.
- Do not copy explicitly imported memories or tables during ordinary linking.
- Do not retain guest-memory pointers across `memory.grow`, yield, or a browser
  callback.
- Do not locate the active process or module through mutable global state.
- Do not put POSIX descriptor or process policy in JavaScript.
- Do not send `close` or another descriptor-lifecycle operation to a host shim
  when `open`, `pipe`, `read`, and `write` use the engine kernel; all operations
  on one descriptor must update the same open-file description.

After shell or Python changes, run `bash -n start.sh`, bytecode checks for
changed `src/html-rt/tools/*.py`, and `git diff --check`.

Guest batch launchers delegate scheduling to the runtime while keeping file I/O
in the guest. `waste_kernel.test_suite_v1` is an opt-in shell capability; its
versioned, bounded arguments never authorize guest-selected host paths. The
engine owns copied request/reply buffers and validates the future reply range
before returning `EXEC_YIELD_HOST_IO`. Runtime adapters run isolated batches
from the installed directory or extracted package, supply a reply, and resume through ordinary C returns.
Batch workers leave the capability disabled. Keep enumeration cancellation in
the browser controller's state as well as the active-worker state; an interrupt
can arrive before a catalogue exists. Sequence IDs guard delayed replies and
cancels, and unrelated input/resize wakeups must wait for a batch reply.

Parse signed integer literals as unsigned magnitudes and negate in unsigned
arithmetic before converting the bit pattern to the signed storage field.
Negating an already-cast `INT64_MIN` invokes C undefined behavior even though
`-9223372036854775808` is a valid WAT i64 literal. Keep decimal and hexadecimal
minimum-literal probes and run UBSan with `halt_on_error=1` so a recovered
sanitizer diagnostic cannot be mistaken for an assertion pass.


## Installed VFS Inputs

Use the current `src/vfs` tree as the filesystem definition. Native adapters
enumerate directories and open children with `openat`/`O_NOFOLLOW`; browser
packaging scans the same tree and the browser reuses the tarballjs file map.
Reject unsafe paths, symlinks, special files, oversized inputs and inconsistent
reads. Entry and byte bounds come from `src/config.h`. Derive inode IDs from
sorted paths, use guest root ownership, and preserve current modes and
nanosecond mtimes. Synthesize missing boot directories and WAT/WAST interpreter
nodes. Generate browser metadata and content hashes into the package for bounded
transport validation. Never require a checked-in inventory, source hashes or
SDK refresh before accepting file changes, and never wrap this tree in a second
custom archive. Build the page with `make -C src/html-rt bash-html` or
`./start.sh --html-bash`; both package the tree without reinstalling guest files.

Mount complete inputs into a fresh kernel. Guest writes modify private kernel
files and cannot alter the installed tree or another test's filesystem. Native
batch children inherit a validated in-memory catalogue. The native shell holds
an open root directory and passes that capability to its batch companion,
retaining directory identity across rename/replacement without serializing
file contents to a temporary image. The companion revalidates current files;
this directory capability does not freeze external file-content edits.

### Guest signal-mask operations

Use the fixed-width four-word guest signal-set codec, not a native host
`sigset_t` or an assumed compiled C handler pointer. Copy input before writing
an old-mask output so aliased buffers remain valid. Validate complete guest
ranges before writes and commit process-mask changes only after output succeeds;
rejected operations must preserve both output canaries and kernel state. Mask
updates exclude SIGKILL/SIGSTOP; pending queries observe blocked queued signals
without consuming them. Caught handlers run at blocking READ, SELECT and pselect
boundaries. READ consumes a pending signal only when it would block; ready
data and EOF retain their ordinary behavior. Free scratch input before invoking
the callback and revalidate the guest buffer on SA_RESTART. SELECT/pselect return
EINTR even when SA_RESTART is set.

Compiled C function pointers identify shared table slots, not function indices.
Resolve the slot's owning process instance and validate `(i32) -> ()` before
invocation. Table-free legacy WAT probes retain their local function-index ABI
and 20-byte sigaction prefix; production C actions have flags at offset 20 in
the 24-byte structure. Decode the entire input before aliased old-action writes.
Public libc uses the fixed 24-byte `waste_kernel.sigaction_v1` boundary and
translates negative kernel errors into -1/errno. Reject unknown flags and
SIG_ERR before modifying old-action output or process state; support ordinary
handlers and SA_RESTART without advertising siginfo callbacks or alternate
stacks. Normalize legacy kernel SIG_IGN (-2) to the public C sentinel (one).
Public `signal` installs a persistent restartable action instead of discarding
registration. Application classification uses the capsule's inherited
`is_application` flag: a fork owns its evaluator clone without retaining the
parent's executable-image descriptor. Drain queued signals unblocked when a
handler's mask is restored before returning to application code, iterating
without growing the host callback stack and preserving any nonrestart decision.
Handler traps, exits and guest longjmp propagate through ordinary C returns;
restore action masks and callback policy on every path. Synchronous handler
scopes suppress browser pump yields and explicitly reject blocking operations
instead of leaving a second saved invocation to resume accidentally.

Use one process-owned monotonic deadline for `alarm`, independent of a syscall
wait. Reuse the existing scheduler's minimum-deadline query for native polling
and browser wakeups, including an alarm while its process waits in READ/waitpid
or another process is selected. Expiry queues SIGALRM once; masks/dispositions
go through ordinary signal handling. Round fractional remaining seconds up and
retain pending signals when replacing or cancelling an expired timer. Deliver
pending unblocked signals before a successful application `sigprocmask` returns,
using the ordinary synchronous delivery path. A deferred old notification can
otherwise interrupt the next READ and hide that READ's intended alarm deadline.
Clear only a fork child's alarm: generic clones also prepare image transactions and
must retain the deadline. Exec retains it while resetting caught actions.

Caught signals during CPU work use the same bounded opcode checks as default
termination and cancellation. Invoke the ordinary handler with live evaluator
frames, preserving the synchronous-callback guard. While restoring a saved
blocking call chain, let the import own its EINTR/restart decision; do not
consume its signal at an earlier caller opcode checkpoint. Keep unsigned
alarm results separate from negative errors with the widened versioned ABI.
The full upstream `Watchdog.cpp` fixture under `src/aux/clang/test/watchdog`
checks ordinary libc, CPU work, fork/exec and concurrent timer delivery; its
controlled WAST checks exact below/on-deadline behavior and masked pending
signals through existing injected clocks. No host alarm or LLVM-local timer
is needed. These probes do not establish the rest of POSIX timers or Clang
execution acceptance.

Public guest headers must expose the signals a package uses: omitting SIGWINCH
caused Bash to compile out its resize handlers. Rebuild the package after header
changes and verify actual guest behavior alongside low-level ioctl state. The
shared `cli-runtime` probe tests real callbacks, ownership, masks and canaries
against native and browser C builds. Default process termination must honor
masks and preserve a signal wait status, independently of host cancellation.
Exec drops caught actions and retains ignored actions and the process mask.
Guest libc `abort()` calls the versioned `waste_kernel.abort_v1` operation.
It unblocks SIGABRT, invokes a caught handler synchronously, and terminates
with signal 6 if the handler returns or the signal is ignored. Recursive
abort from a handler forces termination without invoking the handler again.
The legacy `env.abort` import uses the same implementation. Preserve raw
signal status for waitpid; never call host abort or terminate the browser.
`src/engine/test/abort` compiles the C handler/fork probe (`probe.c`,
`run.sh`); `src/vfs/root/test/system/abort/abort.wast` captures the
observations and asserts them from the surviving Bash on both runtimes.
Stop/continue lifecycle states and asynchronous caught handlers during CPU-only
work still require implementation beyond runnable process selection.

## Retain segment declarations or reject the resource bound

Correct unsigned segment operands cannot compensate for dropped declarations.
Take data/element capacities from `src/config.h`, with the same bound in parser
metadata, names, loader and evaluator state. Every append form checks capacity;
excess declarations fail parsing and unretained data payloads are freed. Resource
bounds remain hard parse errors inside `assert_invalid`, rather than satisfying
semantic-invalid expectations with truncated syntax.

Verify initialization and drop behavior as well as setup acceptance: the portable
segment probe checks distinct values at 31/32/63/64/127, numeric and deferred names,
32/64-bit memory/table operands, post-drop traps and zero-length operations.
Capacity overflow is a C implementation boundary. The existing fixed capacities
do not promise arbitrary segment counts. Generate overflow probes from the
current configured bound instead of fixing the fixture size to a historical
limit.

## Preserve Element Segment Nullability

Bare function-index vectors and `elemkind func` declare non-null `(ref func)`;
explicit `funcref` declares nullable `(ref null func)`. Keep the distinction in
active, passive and declarative WAT metadata. Synthesized table-shorthand
segments inherit the table's declared type, including its nullability.

Binary element modes 0–3 imply `(ref func)`, mode 4 implies nullable `funcref`,
and modes 5–7 carry an explicit reference type. Store that declared type for
both active initialization and instruction validation. Do not infer a narrower
segment type because every current item happens to be a non-null function.
The text encoder may use expression vectors with an explicit non-null type;
it need not produce byte-identical encodings.

Verify table contents and lifetime as well as setup: bare/empty vectors,
nullable null slots, passive `table.init`, dropped active/declarative segments,
post-drop traps, table64 and all eight binary modes. Reject nullable segments
when targeting non-null tables even if their vectors contain only `ref.func`.


## Standard language-test host tables

`spectest.table` and `spectest.table64` are distinct nullable funcref tables,
with initial size 10 and maximum 20. Preserve address width separately from
reference type: standard table64 imports use 64-bit indices. Resolve names to
store-owned objects and let the existing loader enforce width/type/limits;
do not grow host tables to satisfy incompatible imports. Zero-initialized
slots have no owner and represent null references.

Aliases within one script share size, contents and cross-module function
owners; separately scheduled scripts receive fresh host objects. Include both
host tables in checkpoint capture even before a module imports them. Store
teardown releases entries after engines, and snapshots restore object identity
and original bounds/contents. Keep private checkpoint ownership checks as well
as portable WAST bounds/growth/import and isolation probes. Run both manifest
orders sequentially and concurrently, and verify C-encoded modules in source
order so prior actions establish shared table state.


## Plain and folded bulk instructions

Give plain bulk operators grammar productions that emit their opcode and
immediates; permissive generic instruction handling must not silently omit
`data.drop` or `elem.drop`. `table.init` abbreviates the table index to zero,
or accepts explicit table/element indices in text order. Binary encoding places
the element index first. Keep table and element fixups in their own index spaces
and retain their original source locations, including forward names and mixed
numeric/named forms. Folded numeric/numeric and numeric/named productions must
emit the same operation as their plain counterparts. Preserve instructions with
missing stack operands so binary validation sees them; reject missing text
immediates or unresolved symbols through normal text parsing.

Compare effects using distinct source functions/data and an untouched peer
table; successful setup alone cannot prove the instruction was emitted. Check
both address widths, repeated drops, post-drop traps and zero-length operations.
Pair invalid-module assertions with quoted malformed-text assertions.
`src/engine/test/test-suite/flat-bulk.wast` supplies the portable checks; keep ordinary
rejection probes and resource-bound failures explicit too.


`table.copy` accepts no table indices (both zero) or two explicit indices;
`table.fill` accepts no index (zero) or one. Copy immediates stay in
**destination/source** order in both text and binary, unlike initialization's
reversed table/segment encoding. Preserve table references through deferred
index-space resolution, including forward names and all mixed numeric/named
forms. Give folded numeric forms explicit emission paths; even operand-free
fill/copy forms must reach binary operand validation.

Copy addresses use each table's width. The count is i64 only when both tables
are table64, otherwise i32; fill uses its table's width for address and count.
Check both overlap directions with distinct entries, no writes after a bounds
trap, zero-length boundary behavior, nulls, reference identity/nullability and
cross-module function owners. `src/engine/test/test-suite/table-copy-fill.wast` covers
portable valid/invalid/malformed cases; retain ordinary rejection probes
and sanitizer/leak gates.

## Exported table shorthand

Table element-list shorthand may carry an inline export and infer its minimum
size from the element list. Accept both table32 `(table (export "name")
funcref (elem ...))` and table64 `(table (export "name") i64 funcref (elem
...))` forms. Preserve the reference type, attach the synthesized active
element segment to the just-declared table, and emit the inline export through
the ordinary table export metadata. Test both address widths with indirect
calls, then import the exported table from a second module to verify aliasing
and shared contents. `src/engine/test/test-suite/exported-table-shorthand.wast` covers
these paths in native C and the production browser runtime.

When increasing the control nesting bound, keep the validator's temporary
control workspace off the C stack. Size its owned heap buffer to the smaller
of the function's instruction count and the configured depth, plus the outer
control. Initialize each entered frame and free the workspace on every outcome.
A large fixed stack array can break ordinary browser Bash startup before the
deep regression runs, even when the native tests pass. Check production-worker
startup as well as deep execution, invalid bodies and the resource rejection.
Indexed reference encodings must also leave disjoint ranges for real nullable
and non-null references, parser-only recursive fixups and validation bottom;
check high GC indices and typed function references after widening the type
capacity. Resource limits do not make otherwise valid Wasm malformed.
