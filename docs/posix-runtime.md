# Browser POSIX runtime architecture

The POSIX layer is part of the interpreter, not a collection of JavaScript
stubs. JavaScript supplies browser capabilities and an asynchronous control
channel; the OCaml runtime owns process semantics.

## Browser control channel and control-page ABI (version 1)

The self-contained dashboard uses the CPS interpreter and yields to the worker
event loop after each configured instruction quantum. Pause and resume are
worker messages which gate the next continuation slice. A signal message is
written by that same worker into a local `Int32Array` exposed as
`globalThis.waste_control_page`; the interpreter runs with `--control-page` and
checks the ring once per guest instruction. Because the producer and consumer
run alternately in one worker, this static-file path does not require shared
memory or hosting headers.

| Word | Purpose |
| ---: | --- |
| 0 | Published command sequence |
| 1 | Pause state (`0` running, `1` paused) |
| 2 | Reserved for interpreter status |
| 3 | ABI version (`1`) |
| 4–259 | 256-word command ring |

A command word contains a 16-bit operation and 16-bit argument. Operations are
`1` pause, `2` POSIX signal, and `3` terminate. The producer writes a ring slot
before publishing word 0. The interpreter performs one atomic load of word 0
at every guest instruction and reads a slot only when the sequence changes.
The same ABI also accepts a `SharedArrayBuffer` for non-dashboard controllers.
In that mode ring publication uses atomics and pause can use `Atomics.wait`.
The static dashboard deliberately uses scheduler yields and ordinary worker
messages instead, preserving copy-and-open deployment and offline analysis.

## Process state

Each scheduled interpreter process owns:

- its control-page read sequence;
- blocked and pending signal sets;
- complete `sigaction` records (handler, 128-byte glibc mask, and flags);
- a map of jump-buffer addresses to explicit evaluator snapshots.

Module memories and globals remain live when an evaluator snapshot is made.
`siglongjmp` restores copied operand/control stacks, frames, locals, the saved
signal mask when requested, and the clang ABI stack-pointer global. It does not
roll back heap or ordinary global state. A saved jump can be used more than
once; it does not rely on OCaml's one-shot effect continuation.

Signal delivery happens only in a guest frame. A table function is invoked as
the handler, the action mask and implicit self-mask are installed, and the old
mask is restored when the handler returns. `SA_NODEFER`, `SA_RESETHAND`, and
one- or three-argument handlers are represented. `SIGKILL` and `SIGSTOP` cannot
be caught or ignored.

## Kernel object model

The interpreter now owns a shared kernel object for each isolated top-level
execution. A kernel contains the process table, VFS root, controlling terminal,
PID and inode allocators, and monotonic clock. Each process contains PID, PPID,
process group, session, lifecycle state, current directory, descriptor table,
signal state, and timers.

Descriptor entries refer to shared open-file descriptions. Consequently,
`dup`, `dup2`, inherited fork descriptors, offsets, status flags, and pipe
endpoint reference counts have POSIX sharing behavior. Pipes use bounded
buffers and expose readiness objects to the cooperative scheduler. Empty reads,
full writes, child waits, stopped processes, and deadlines leave the ready
queue instead of consuming instruction quanta in a polling loop. An unblocked
signal also makes a blocked process runnable so the interrupted syscall can
observe signal delivery.

The VFS currently provides a rooted in-memory namespace with `/tmp` and
`/dev/tty`, regular files, directories, path normalization, open-file offsets,
create/truncate/append behavior, rename/unlink, metadata, and descriptor-based
I/O. No browser storage policy is embedded in it; persistent storage will be a
mount backend under this namespace.

The terminal tracks its controlling session and foreground process group.
`tcgetpgrp`, `tcsetpgrp`, opaque termios state, and background-read `SIGTTIN`
behavior live in the kernel rather than JavaScript. Default stop, continue,
terminate, and ignored signal actions update process lifecycle and produce
waitable parent events.

Realtime and monotonic milliseconds enter through a narrow runtime primitive.
`time`, `gettimeofday`, `alarm`, `setitimer`, and scheduler deadlines are then
implemented in OCaml. The primitive transfers the clock as four 16-bit words,
avoiding JavaScript-number and Wasm-GC `i31` truncation.

`fork` clones the evaluator continuation, jump buffers, module-instance graph,
memories, mutable globals, tables, element/data state, and function ownership.
The child gets `0`; the parent gets the allocated PID. Kernel objects which
POSIX defines as shared—VFS nodes and open-file descriptions—remain shared.
Guest linear memories are currently copied eagerly through `Memory.clone`.
That function is the deliberate replacement boundary for page-granular
copy-on-write; evaluator and kernel semantics do not depend on the copy
strategy.

## Current boundary

Implemented imports now cover the signal functions plus `getpid`, `getppid`,
`getpgrp`, `setpgid`, `setsid`, `fork`, `waitpid`, process exit, `kill`,
`killpg`, `open`, `close`, `read`, `write`, `pipe`, `dup`, `dup2`, `fcntl`,
`lseek`, `chdir`, `getcwd`, `unlink`, `rename`, access checks, stat calls,
`fchmod`, `umask`, terminal group/attribute calls, `time`, `gettimeofday`,
`alarm`, `sleep`, and `setitimer`.

This is not yet enough to instantiate `src/bash.wat`: that module has 231
imports and still needs the allocator/stdio ABI, directory streams, locale and
wide-character functions, command execution, resource limits, networking, and
several libc helpers. The current `stat` encoder uses this runtime's documented
wasm32 layout rather than guessing compatibility with an unidentified libc;
the libc shim must own and test that ABI before Bash relies on it.

`sys.js` is retained as a behavioral reference for libc-style memory helpers
and browser output. It currently overlaps 26 of the 231 `env` imports in
`bash.wat`, and several of those functions are explicitly partial. It cannot be
installed directly: it expects the executed module's linear memory as a
JavaScript typed array, while this project executes guest memory inside the
OCaml interpreter. Pure memory operations should be ported to OCaml over the
interpreter `Memory` API. Browser capabilities (terminal rendering, persistent
storage, clocks, entropy, and networking) should use narrow JavaScript adapters
whose results are copied through the OCaml syscall layer. This keeps process,
descriptor, permission, blocking, and signal semantics in one place.

The next kernel work is mount backends and permissions, directory streams,
symlinks, terminal line discipline and window sizing, descriptor readiness for
`select`/`pselect`, resource accounting, `execve`, and page-granular COW.
Threads must then share an address space and descriptor table while retaining
their own evaluator stacks and signal masks; they must not be represented as
process-table entries merely because the cooperative scheduler uses the same
execution mechanism.

Guest `fork` and blocking calls currently require `--schedule`; an unhandled
blocking effect in direct interpreter mode is intentionally an error. Browser
POSIX applications should therefore launch through the cooperative scheduler.

## Verification

Build both runtimes, then run the shared-memory integration probe:

```sh
./start.sh --compile
node tests/posix-control-runtime.cjs
node tests/posix-control-runtime.cjs threaded
node tests/posix-kernel-runtime.cjs
node tests/posix-kernel-runtime.cjs threaded
```

The control probe uses a second JavaScript worker to pause and resume a busy guest and
then inject `SIGINT`. It passes only if the guest handler runs and returns to
the interrupted instruction stream. The kernel probe executes
`tests/posix-kernel.wast` and checks process identity, VFS and pipes, forked
memory isolation, zombie reaping, stop/continue events, group signals, and
foreground terminal transfer in both compiled runtime variants.
