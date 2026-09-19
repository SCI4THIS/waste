# C Engine Browser Command-Not-Found Plan

This is an active implementation plan. Durable runtime ownership rules are in
[architecture.md](architecture.md), and reusable implementation practices are
in [techniques.md](techniques.md).

## Purpose

Make an unavailable external command in the C-engine Bash page follow Bash's
normal command-search path. While no `ls` executable exists in the sandbox,
entering `ls` must print:

```text
bash: ls: command not found
```

Bash must set `$?` to `127`, return to its prompt, accept another command, and
remain alive until the user explicitly exits. The message and status must come
from Bash. JavaScript must not recognize command names or fabricate shell
output.

## Current Failure

The interactive Bash module imports pathname queries such as `stat`, `lstat`,
`access`, `eaccess`, and `faccessat`. The browser adapter currently handles
these with broad placeholders:

- `stat` and related calls report success for every path and return an empty
  status structure;
- `access`, `eaccess`, and `faccessat` return success unconditionally; and
- `fork` returns `-1` without arranging for guest `errno` to be updated.

Consequently Bash incorrectly concludes that a PATH candidate such as
`/bin/ls` exists and is executable. It advances to the external-command
process path, reaches the unsupported `fork`, sees a stale zero in guest
`errno`, prints `bash: fork: errno 0`, and leaves the interactive invocation in
a failed state.

The command-not-found fix is therefore a pathname/VFS and errno-boundary fix,
not a special Bash parser rule and not initially a `fork` implementation. If
all PATH candidates truthfully return `ENOENT`, Bash emits its canonical
diagnostic before it needs to create a child.

## Required Behavior

The first complete slice must provide:

- bounded decoding of every guest pathname;
- engine-owned path normalization relative to the sandbox working directory;
- a minimal rooted namespace containing the directories Bash needs to search,
  without claiming that absent executables exist;
- coherent `stat`, `lstat`, `access`, `eaccess`, and `faccessat` results;
- `ENOENT` for a missing final component, `ENOTDIR` for an invalid prefix,
  `EACCES` for an existing but inaccessible candidate, and `EINVAL`/`EFAULT`
  for invalid arguments as applicable;
- guest-libc wrappers that translate negative kernel errno values into `-1`
  and update guest `errno`;
- an explicit `ENOSYS` result for process creation while `fork` is genuinely
  unsupported, so no process error can surface as `errno 0`;
- Bash status `127` for an unresolved command; and
- continued terminal waiting and successful execution of subsequent builtins.

The durable regression should use a name guaranteed not to enter the VFS,
such as `waste-command-that-does-not-exist`. The acceptance test should also
exercise `ls` while the sandbox intentionally contains no `ls` executable.
When an executable registry is added later, `ls` may become runnable without
invalidating the generic missing-command test.

## Ownership and Interfaces

### Engine-owned VFS lookup

Path existence, node type, permissions, working directory, and executable
availability belong to the per-sandbox kernel. Add a small path-query layer to
the kernel rather than extending the current return-value stubs. Its initial
namespace may contain only `/`, `/bin`, `/usr`, and `/usr/bin`, but it must use
the same node/lookup contract intended for future in-memory files and embedded
executables.

The lookup API should accept an explicit byte span and flags. It must not keep
guest pointers, depend on the host filesystem, or send a pathname to
JavaScript. Relative paths resolve from engine-owned `cwd`; repeated slashes,
`.` and `..` are normalized without escaping the sandbox root.

### Versioned guest ABI

Expose versioned internal imports under `waste_kernel`, for example:

- `path_access_v1(path, mode, flags)`;
- `path_stat_v1(path, follow, compact_metadata)`; and
- `fork_v1()` for an explicit negative `ENOSYS` until process cloning exists.

Guest-libc exports retain the POSIX names expected by Bash. They invoke the
versioned imports, translate negative errno results, and populate the guest
ABI's `struct stat`. This keeps `errno` in guest memory and lets Bash link to
ordinary libc functions through the existing `env` registration.

Freeze the Wasm guest layout of `struct stat` with compile-time assertions and
a generated fixture. Do not copy the build host's `struct stat` or write a
guessed 128-byte buffer from the host adapter. Prefer a compact internal
metadata structure that the guest wrapper converts to its public ABI.

### Browser boundary

The browser worker remains responsible only for terminal byte delivery and
output presentation. It must not perform PATH lookup, decide whether a command
exists, inject `command not found`, or restart Bash after an error. The same C
kernel behavior must be testable through the native runner and browser Wasm.

## Execution Flow

For an input such as `ls`:

1. Bash expands the command and searches each PATH candidate.
2. Its libc `eaccess`/`stat` wrapper calls the versioned kernel import.
3. The host adapter bounds-checks and copies the pathname from the calling
   instance's shared linear memory.
4. The sandbox kernel normalizes and looks up the path.
5. Each absent candidate returns negative `ENOENT`; libc returns `-1` and sets
   guest `errno`.
6. Bash exhausts PATH, prints its own `bash: ls: command not found`, and sets
   status `127` without calling `fork`.
7. Bash reenters its existing terminal read wait. The worker observes the
   normal read yield and waits for more input.
8. A later builtin executes in the same shell, proving that shell state and
   the evaluator continuation survived.

## Implementation Stages

### Stage 1: Freeze the failing behavior and syscall trace

- Extend the C-engine Bash browser harness with an unknown-command scenario,
  initially recording the current `bash: fork: errno 0` failure.
- Add optional `WASTE_PROFILE` tracing for pathname queries and process calls,
  or an equivalent native test hook, to record the exact Bash lookup order.
- Confirm whether Bash consults `eaccess`, `stat`, or both for the generated
  Bash artifact; retain tests for all imported variants regardless.
- Add assertions that the worker posts no terminal `done` result before the
  test deliberately sends `exit`.

Gate: the regression reliably distinguishes the current fork error, a
canonical command-not-found result, premature shell termination, and a hung
shell.

### Stage 2: Define the pathname and guest-stat ABI

- Add portable engine errno constants needed for pathname lookup.
- Define a bounded internal metadata record containing node type, mode, size,
  and the minimum stable fields needed by Bash.
- Record and assert the guest Wasm `struct stat` size, alignment, and field
  offsets; add a generated libc ABI fixture.
- Define versioned `waste_kernel` imports and negative-errno conventions for
  access/stat queries.

Gate: native codec tests and generated guest probes agree on every field Bash
will read, under warnings-as-errors and ASan/UBSan.

### Stage 3: Add minimal engine-owned VFS lookup

- Add per-kernel `cwd` and a minimal rooted directory tree.
- Implement path normalization and component lookup with explicit bounds.
- Implement access checks for existence and executable permission.
- Return `ENOENT`, `ENOTDIR`, and `EACCES` accurately; distinguish malformed
  or overlong paths from ordinary absence.
- Keep the representation ready for embedded executables without adding one
  merely to satisfy this slice.

Gate: native tests cover absolute and relative paths, PATH directories,
missing leaves, missing/intermediate non-directories, `.`, `..`, repeated
slashes, root confinement, permission denial, and separate-store isolation.

### Stage 4: Replace browser pathname placeholders with libc wrappers

- Add guest-libc `stat`, `lstat`, `access`, `eaccess`, and `faccessat`
  wrappers over the versioned kernel imports.
- Translate negative errno values in libc and populate guest `struct stat`
  only on success.
- Add instance-aware C host adapters that copy bounded pathname data into the
  kernel and never retain guest pointers.
- Remove the unconditional-success entries from `native_posix_function` once
  every caller resolves through the libc/kernel path.
- Add a libc `fork` wrapper that reports `ENOSYS` correctly until the process
  model is implemented, eliminating `errno 0` for any remaining fork path.

Gate: native and browser integration fixtures observe matching results and
errno for missing, existing-directory, inaccessible, and invalid paths.

### Stage 5: Make command-not-found an interactive continuation gate

- Extend `tests/c-engine-bash-browser-runtime.cjs` to send `ls` and a unique
  missing command.
- Require Bash's exact diagnostic, status `127`, and another prompt.
- Send a builtin afterward and require its output, proving Bash stayed alive.
- Exit normally and retain the existing constructor/main result checks.
- Verify that no JavaScript code contains command names or diagnostic text.

Gate: the self-contained page prints the canonical diagnostic, continues
accepting input, executes a subsequent builtin, and still passes its complete
browser smoke test through `file://`-compatible plumbing.

### Stage 6: Differential and regression closure

- Compare the interactive sequence with native Bash and the OCaml Bash page,
  allowing only documented prompt-format differences.
- Run the native pathname/VFS sanitizer suite, caller-instance tests, full
  sequential and threaded libc suites, 97/97 core CLI suite, targeted browser
  libc tests, and C-engine Bash smoke test.
- Regenerate `bash.html` and confirm the saved build log records the extended
  interaction.
- Update `architecture.md` if the new VFS contract adds durable ownership or
  ABI details not already captured there.

Gate: all scoped tests pass and the old `bash: fork: errno 0` text cannot be
reproduced for a missing command.

## Likely File Boundaries

- `src/engine/lib/include/kernel.h` and `src/engine/lib/kernel.c`: sandbox VFS,
  cwd, normalization, lookup, and portable errno values;
- a focused engine path/VFS source split if kernel growth warrants it;
- `src/html-rt/lib/unistd.c` and a focused stat source: POSIX-facing wrappers
  and guest errno translation;
- `src/html-rt/lib/include/helper.h`: guest ABI definitions and assertions;
- `src/html-rt/posix_stubs.c`: bounded instance-aware kernel adapters;
- `src/html-rt/tools/libc_sources.py`: any new guest-libc source registration;
- `tests/libc-test/`: generated ABI and pathname fixtures;
- focused native VFS tests under `tests/`; and
- `tests/c-engine-bash-browser-runtime.cjs`: interactive acceptance sequence.

## Completion Criteria

The plan is complete when:

1. missing PATH candidates return `ENOENT` from engine-owned lookup;
2. guest `errno` is correct for pathname and unsupported process operations;
3. Bash, rather than JavaScript or the engine, formats `command not found`;
4. an unresolved command returns status `127` and does not call `fork`;
5. Bash resumes its terminal read wait and successfully handles later input;
6. native, OCaml, sequential/threaded libc, and browser results have no
   unexplained differential in the scoped behavior; and
7. the generated page remains a self-contained `file://` artifact.

## Explicit Non-Goals

This slice does not implement `fork`, `execve`, `waitpid`, a complete
filesystem, external coreutils, host-filesystem passthrough, or a browser-side
command registry. It must not special-case `ls`, return `ENOENT` for every path,
patch the generated Bash WAT, or hide evaluator failure by restarting the
worker. Those later capabilities should use the same VFS and process
boundaries established here.
