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
be one of the documented kernel calls. The first probe deliberately imports
only `env.write` so that the image format can be validated independently of
the full libc port; production images use the versioned kernel names.

The kernel boundary uses wasm32 integer handles and `(ptr, length)` byte spans.
Pointers are validated against the calling image before the engine accesses
memory. Errors return `-1` and set the image-local `errno`; successful calls
return a nonnegative result appropriate to the operation.

## Process startup

The eventual libc startup shim owns `argc`, `argv`, and `envp` storage inside
the new image. Strings and pointer vectors are copied from the caller before
`execve` replaces the old image. The initial environment is inherited unless
the caller supplies a non-null `envp`; the current probe does not yet exercise
this path.

## Validation requirements

An executable manifest records the absolute path, mode bits, ABI version,
module bytes/template, and entry metadata. Loading rejects malformed Wasm,
unsupported ABI versions, unknown imports, and missing entry points before a
process image is changed. This validation is the prerequisite for implementing
successful `execve` without returning to the old image.

