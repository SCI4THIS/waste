# Browser POSIX runtime architecture

The POSIX layer is part of the interpreter, not a collection of JavaScript
stubs. JavaScript supplies browser capabilities and an asynchronous control
channel; the OCaml runtime owns process semantics.

## Control-page ABI (version 1)

The controller creates a `SharedArrayBuffer` and exposes its `Int32Array` as
`globalThis.waste_control_page` before starting the interpreter with
`--control-page`.

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
Pause uses `Atomics.wait` in the execution worker, so resume does not depend on
that worker's blocked JavaScript event loop.

This page requires a cross-origin-isolated browser context (COOP/COEP) when the
browser restricts `SharedArrayBuffer`.

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

## Current boundary

Implemented imports are `sigsetjmp`, `siglongjmp`, `sigemptyset`, `sigaddset`,
`sigdelset`, `sigismember`, `sigprocmask`, `sigaction`, `kill`, `killpg`, and
`raise`. This is sufficient to validate control transfer and signal entry, but
not to instantiate `src/bash.wat`: that module has 231 imports, including the
filesystem, stdio, allocator, terminal, locale, process, pipe, wait, timer, and
network families.

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

The next layer should introduce a process table (PID/PGID/session, parent and
children, lifecycle and wait state), descriptor objects shared by `dup`, pipe
wait queues, a virtual filesystem, and a terminal/foreground-process-group
model. `fork` must copy evaluator/process state with copy-on-write memory, while
threads share the process address space and descriptor table but own evaluator
stacks and signal masks. Those objects should be implemented before adding the
corresponding libc imports, so browser adapters cannot accidentally define
POSIX semantics.

Until that process table exists, `kill` only accepts the current synthetic PID,
the current process group, or POSIX broadcast forms and rejects other targets.
It deliberately does not redirect an unknown child PID back to the caller.

## Verification

Build both runtimes, then run the shared-memory integration probe:

```sh
./start.sh --compile
node tests/posix-control-runtime.cjs
node tests/posix-control-runtime.cjs threaded
```

The probe uses a second JavaScript worker to pause and resume a busy guest and
then inject `SIGINT`. It passes only if the guest handler runs and returns to
the interrupted instruction stream.
