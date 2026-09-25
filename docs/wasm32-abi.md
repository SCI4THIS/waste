# WASTE wasm32 application ABI

This is the initial ABI used by external wasm32 programs loaded by the C
engine. It is intentionally small and does not depend on Asyncify, JSPI, or
JavaScript re-entry.

## Image contract

- The module is a normal wasm32 module with one linear memory owned by the
  process image. Mutable globals, tables, and memory are never shared between
  processes.
- `_start` is the preferred exported entry point and has type `() -> ()`.
  An image may instead export `main` with the same type while the loader
  supplies process arguments through the libc startup layer.
- Constructors, when present, run before the entry point in module order.
- Returning from the entry point is equivalent to `exit(0)`. Explicit process
  termination uses the engine `exit` boundary and preserves the exit status.

## Host boundary

The versioned `waste_kernel` boundary is the only process/environment ABI.
Guest libc may provide its own ordinary functions, but unresolved imports must
be one of the documented kernel calls. The probe uses the small `env.write`,
`env.exit`, and `env.fcntl` surface so image startup, status, and descriptor
inheritance can be validated independently of the full libc port; production
images use the versioned kernel names.

The kernel boundary uses wasm32 integer handles and `(ptr, length)` byte spans.
Pointers are validated against the calling image before the engine accesses
memory. Errors return `-1` and set the image-local `errno`; successful calls
return a nonnegative result appropriate to the operation.

`waste_kernel.startup_v1() -> i32` returns the active image's startup-block
pointer. It returns `-1` with `ENOENT` when the image has no startup block. The
guest libc startup shim can use this import to construct its conventional
`main(argc, argv, envp)` call without a JavaScript callback or host pointer.

The libc helper `__waste_startup_call(entry)` performs that decoding and calls
the supplied guest `main` function pointer. Application-specific `_start`
shims can use it while retaining normal Wasm table/call-indirect semantics.

The external-image descriptor subset exposes `env.fcntl(fd, command, argument)`
for `F_GETFD` and `F_SETFD` with `FD_CLOEXEC`. Descriptor numbers are engine
owned; only integer results cross the boundary.

## Process startup

The engine copies `argc`, `argv`, and `envp` strings into the new image before
`execve` replaces the old image. An optional exported
`__waste_startup(i32 block)` hook receives a pointer to a fixed-width startup
block at the top of linear memory. The block contains, at offsets 0, 4, 8, and
12, `argc`, `argv`, `envc`, and `envp`; offsets 16 and 20 contain the process
PID and a pointer to the NUL-terminated cwd string. The argv/envp vectors are
32-bit guest pointers terminated by zero. Images without this hook retain the
minimal `_start` ABI, allowing the original probe to remain valid.

## Process memory contract

The image's process memory is an engine-owned virtual address space exposed to
all modules in that process through one imported `exec_memory`. Guest pointers
are wasm32 offsets in that process and are valid only after bounded range and
protection checks. They are never host pointers and must not be shared
directly between independent processes.

The engine may map anonymous pages, file-backed pages, or named shared-memory
pages at process virtual addresses. `fork` preserves explicit shared-page
aliases and applies copy-on-write to private writable pages. `execve` creates
and validates a replacement image before committing it. A process checkpoint
preserves page mappings, protections, backing identity, and aliases rather
than assuming that memory is one contiguous host allocation.

An access into a mapped `PROT_NONE` page, an unmapped address, or a truncated
file mapping is reported through the engine memory-fault record. The record is
an internal input to process signal translation; it does not expose host
signals or host addresses through the ABI.

## Validation requirements

An executable manifest records the absolute path, mode bits, ABI version,
module bytes/template, and entry metadata. Loading rejects malformed Wasm,
unsupported ABI versions, unknown imports, and missing entry points before a
process image is changed. This validation is the prerequisite for implementing
successful `execve` without returning to the old image.
