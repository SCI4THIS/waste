# WASTE engine virtual memory plan

Status: complete (2026-09-24; ready to archive/remove after check-in)

Dependencies: C engine process capsules, imported-memory aliasing, VFS file
objects, and explicit evaluator continuation

Unblocks: Stage 8F of `docs/active-coreutils-wasm-loader-plan.md` after the
closure gates below pass

## Objective

Replace the C engine's contiguous `exec_memory.data` allocation with an
engine-owned virtual-memory layer. Each process retains a private virtual
address space, all module instances in that process can alias the same Wasm
memory, and selected mappings can share reference-counted backing pages across
processes. Use that foundation for copy-on-write `fork`, POSIX `mmap`, and the
single pointer space required by an application and its guest libc.

## Closure and plan ownership

The virtual-memory plan and the Coreutils image-loader plan are sequentially
dependent, not one combined completion unit. This plan owns the engine memory,
process, mapping, checkpoint, and shared-memory contracts. The Coreutils plan
owns utility waves, executable packaging, import audits, licensing, and Bash
command behavior. VM-H.1 proves the handoff with `true`; it does not transfer
ownership of later Coreutils utility waves into this plan.

The closure pass is complete:

1. VM-A/VM-B/VM-C acceptance summaries are closed with native sanitizer,
   browser official, and page/checkpoint topology evidence.
2. VM-D is now closed for the current guest-libc ABI and process-image gate.
3. VM-E's explicit-region boundary is closed for module, stack, startup, brk,
   anonymous, file, and shared mappings.
4. VM-F and VM-G aggregate statuses are reconciled and their native/browser
   gates cover shared-file lifetime, named shared memory, and checkpoints.
5. The required VM test matrix and durable architecture notes are recorded;
   active implementation ownership returns to Stage 8F in
   `docs/active-coreutils-wasm-loader-plan.md`.

Finishing `false`, `pwd`, or later Coreutils waves is not required to close the
virtual-memory architecture plan. Those remain downstream loader-plan work.

This is an engine and kernel facility. It must work in the native runtime and
the self-contained `file://` browser runtime without host `mmap`, JavaScript
system-call implementations, `SharedArrayBuffer`, cross-origin isolation,
Asyncify, or JSPI.

## Fixed decisions

### Address spaces and sharing

- A process owns a virtual address-space object. Threads share that object.
- Independent processes do not share their complete address spaces.
- Module instances belonging to one process may import and alias the same
  `exec_memory`; this is how an application, guest libc, allocator, errno, and
  stdio share ordinary C pointers.
- Cross-process sharing is explicit and page-granular. `MAP_SHARED`, inherited
  anonymous shared mappings, and future POSIX shared-memory objects may point
  at the same backing pages from different virtual addresses.
- `fork` clones the parent's virtual mappings. Private writable pages begin as
  copy-on-write; shared mappings retain shared backing identity.
- `execve` constructs a new address-space graph and commits it only after the
  executable image and imports validate. It then releases the old graph.
- A guest pointer remains a wasm32 offset in the calling process's memory. No
  host pointer crosses the engine, kernel, JavaScript, or broker boundary.

The engine may maintain a global pool of physical backing-page objects, but it
must not expose one unrestricted global linear address space to all processes.
Two processes are allowed to use the same numeric virtual address for unrelated
data.

### Page model

- The initial virtual and backing-page size is the WebAssembly page size,
  65,536 bytes. Report that value as the guest page size for the first POSIX
  implementation. A smaller internal page size may be introduced later only
  behind the same API and with new cross-page tests.
- An `exec_memory` keeps its WebAssembly page count, declared maximum,
  memory32/memory64 kind, and import/export identity. Its storage becomes a
  virtual-page map rather than a required contiguous byte allocation.
- A backing page owns bytes, a reference count, and backing metadata. Mapping
  entries own virtual protection, private/shared mode, copy-on-write state,
  and optional VFS object plus file offset.
- Missing pages inside the current WebAssembly size read as zero until a write
  materializes private storage. Bounds remain determined by WebAssembly memory
  size, not by whether a backing page has been allocated.
- `memory.grow` extends the WebAssembly-visible page map atomically and returns
  the old size. Allocation failure leaves the old memory unchanged and returns
  the WebAssembly failure value.
- Multiple WebAssembly memories remain distinct. Explicit imported-memory
  aliases refer to the same `exec_memory` and therefore the same page map.
- POSIX `brk`, pointers, and mappings operate in process memory 0. Additional
  WebAssembly memories retain their indexed Wasm semantics and do not silently
  become parts of the POSIX pointer address space.

### Access and protection

All repository-owned code must access guest bytes through bounded memory APIs.
The final engine must not assume that `memory->data + address` is valid or that
a multi-byte range is physically contiguous.

The common API must cover:

- checked scalar and SIMD loads/stores, including little-endian conversion;
- bounded copy-in and copy-out for kernel and startup data;
- memory-to-memory copy with exact overlapping `memmove` behavior;
- fill, data-segment initialization, and zeroing;
- page growth, mapping, unmapping, protection changes, and page lookup;
- snapshot/clone operations that preserve alias topology; and
- an optional single-page fast path or per-execution-context translation cache
  that does not put mutable state in engine globals.

Reads require `PROT_READ`; writes require `PROT_WRITE`. WebAssembly's ordinary
linear memory begins readable and writable. `PROT_EXEC` does not make data
memory executable and must not bypass the Wasm decoder or image loader.
Protection faults become deterministic guest memory faults; the signal layer
may later translate those faults to `SIGSEGV` or `SIGBUS` where POSIX requires
it.

### POSIX mapping semantics

The first accepted `mmap` implementation supports:

- anonymous `MAP_PRIVATE`;
- anonymous `MAP_SHARED`, including sharing inherited across `fork`;
- regular-file `MAP_PRIVATE` with zero-filled partial final pages;
- regular-file `MAP_SHARED` with coherent writes through common backing pages;
- `munmap`, including partial unmapping and mapping splits;
- `mprotect` for read/write/none protections; and
- `msync` for explicit file writeback and error reporting.

Addresses, lengths, file offsets, overflow, descriptor capabilities,
protection, and flags are validated before mutation. File offsets must be page
aligned. Zero length fails. Unsupported flags fail explicitly. `MAP_FIXED`
must not be accepted until replacement and collision semantics are tested;
`MAP_FIXED_NOREPLACE` may be implemented first.

The address-space allocator must coordinate these regions:

- module data and stack;
- `brk`/allocator growth;
- startup argument and environment storage;
- downward-allocated mapping area; and
- reserved guard regions.

The current startup block at the top of linear memory is temporary. Before
general `mmap`, startup storage must receive an owned region that cannot collide
with allocator or mapping growth.

For file-backed shared mappings, the VFS/kernel owns backing identity, dirty
state, file-size checks, and writeback. The optional broker never owns page
tables or guest memory.

### Checkpoints, cloning, and lifetime

- Checkpoints snapshot mappings and backing relationships, not just flattened
  bytes. Restoring a checkpoint restores page counts, protections, dirty/COW
  state, and aliases between module instances.
- A checkpoint must not accidentally turn a shared page into two unrelated
  pages or merge two private pages.
- Backing pages are reference counted across mappings, process clones, and
  checkpoints. Every failure path must release acquired references.
- Decoded modules remain immutable and shareable at engine scope. Mutable
  memories, mappings, page tables, globals, tables, and libc instances remain
  sandbox/process state.

## Stage VM-A: Establish the memory access contract

Status: complete for the access contract and native/browser official gates

Work:

- Inventory every direct `exec_memory.data` access in `src/engine/`, process
  startup, checkpoints, host imports, browser adapters, and tests.
- Add a focused memory API and retain the current contiguous allocation as its
  first backend.
- Route scalar loads/stores, SIMD accesses, bulk memory operations, active data
  initialization, process startup, and kernel copy-in/copy-out through it.
- Keep a temporary compatibility accessor only where conversion must be split
  across commits; list every remaining caller and remove it before VM-B closes.
- Add checked range helpers that reject overflow before narrowing to `size_t`.

Gate:

- No execution or kernel behavior changes with the contiguous backend.
- Official memory, bulk-memory, SIMD, multi-memory, memory64, and linking tests
  match the OCaml oracle in native and browser runs.
- Native warnings-as-errors ASan/UBSan gates pass.
- A repository search shows no unreviewed guest-byte access outside the memory
  implementation.

Implementation update (2026-09-23, first access-boundary slice):

- Added bounded `exec_memory_read`, `exec_memory_write`, `exec_memory_copy`,
  and `exec_memory_fill` helpers. They currently use the existing contiguous
  allocation, so this increment does not change the backing representation.
- Routed scalar loads/stores, SIMD memory instructions, `memory.copy`,
  `memory.fill`, `memory.init`, and active data-segment initialization through
  those helpers. Cross-page behavior remains represented by the same checked
  range boundary that the paged backend will implement.
- The native warnings-as-errors ASan/UBSan `i32-smoke` gate passes, the native
  CLI rebuild passes, and the browser engine rebuild passes.
- VM-A remains open: process startup writes, checkpoint copy/restore, and the
  browser POSIX adapter still contain direct contiguous-memory accesses. The
  next increment will migrate those callers and add boundary tests before any
  page-table implementation begins.

Implementation update (2026-09-23, startup and checkpoint slice):

- Process startup argument/environment/cwd construction now writes through
  `exec_memory_write`; startup no longer constructs guest pointers with
  `memory->data` arithmetic.
- Checkpoint capture now reads memory through `exec_memory_read`, preserving
  the future page-backed read boundary. The remaining contiguous operations in
  checkpoint restore and spectest initialization are backend allocation and
  replacement operations, not guest-byte callers; they will move behind the
  page-owner API in VM-B.
- The native ASan/UBSan smoke gate, native CLI rebuild, and browser engine
  rebuild pass after this slice.
- The browser POSIX adapter still has direct ranges for terminal buffers,
  paths, vectors, stat structures, select sets, and read/write buffers. Those
  require scoped copy-in/copy-out conversions because the kernel calls cannot
  retain pointers into a future page map; they are the remaining VM-A work.
- The broad browser dashboard was started as a regression check but is not a
  VM-A acceptance result. It currently reports failures in several
  bulk-memory, const, and element groups; attribution to this access-boundary
  slice has not been established.

Implementation update (2026-09-23, POSIX copy-in/copy-out slice):

- Browser POSIX calls no longer return `memory->data + offset` or pass borrowed
  guest pointers into kernel/host operations. Guest path strings, read/write
  buffers, vectors, signal actions, terminal attributes and window sizes,
  directory results, readlink results, stat results, wait status, cwd output,
  and execve paths now use bounded `exec_memory_read`/`exec_memory_write`
  copies with host-owned temporary buffers where needed.
- This removes the remaining direct guest-byte access from
  `src/html-rt/posix_stubs.c`. The only remaining `memory->data` references in
  the VM-A inventory are backend ownership operations: contiguous helper
  implementation, memory growth, checkpoint restore, and spectest allocation.
  Those are deliberately deferred to VM-B's page-owner conversion rather than
  exposed as compatibility accessors.
- The browser engine rebuild passes after this slice, including the Wasm
  compilation of the POSIX adapter. The native warnings-as-errors
  ASan/UBSan `i32-smoke` gate and `git diff --check` also pass.
- VM-A acceptance remains pending the full official memory/linking comparison;
  the focused contract tests are now in place and VM-B has begun replacing the
  contiguous backend. No browser interaction is required to validate this
  contract-only increment.

Implementation update (2026-09-23, memory-contract regression slice):

- Extended `tests/c-engine-i32-smoke.c` with a direct access-contract test using
  a two-page memory. It verifies reads and writes that cross the 65,536-byte
  boundary, overlapping same-memory copy behavior, fill behavior, rejected
  out-of-bounds ranges, rejected `UINT64_MAX` overflow, and permitted
  zero-length operations.
- The test runs under the existing native warnings-as-errors ASan/UBSan
  `i32-smoke` target and passes. This is the first focused regression coverage
  that the future page backend must satisfy independently of Wasm instruction
  execution.
- VM-A is still not closed as a conformance stage: the official native/browser
  memory and linking comparison remains to be run. The test is retained as a
  backend-neutral gate for VM-B. No manual browser test is needed for this
  native contract-only increment.

Implementation update (2026-09-23, VM-B sparse page-map slice):

- `exec_memory` now owns a page-pointer map instead of a required contiguous
  byte allocation. An in-bounds page pointer may be null and reads treat that
  page as zero; writes and nonzero fills materialize only the affected 64 KiB
  page. Access helpers walk page boundaries and retain checked overflow and
  bounds behavior.
- `memory.grow` extends the page map without copying the entire address space.
  Instance cloning copies only materialized pages, checkpoint restore rebuilds
  page ownership through the access API, and spectest memory uses the same
  representation. Native CLI select/path host calls now also use bounded
  copy-in/copy-out rather than borrowed guest pointers.
- The focused two-page cross-boundary contract test remains green under native
  ASan/UBSan. Native `wast-native`, browser `wast-browser`, and
  `git diff --check` pass. Existing compiler warnings are unchanged and are
  confined to pre-existing unused browser functions/parameters.
- VM-B remains open: page references are not yet shared or copy-on-write,
  mapping metadata/protection does not yet exist, and the full official
  memory/linking comparison still needs to run against this backend. No manual
  browser test is needed for this backend-only build increment.

Closure update (2026-09-24, native official acceptance pass):

- The native official gate now passes: `./start.sh --cli-test` completed
  97/97 core WAST files. The earlier `imports.wast` failures were caused by
  the shared `spectest` memory having `pages = 1` but `linear_pages = 0`;
  imported-memory bounds checks therefore treated the memory as zero-length.
- Initializing both page counts restores imported-memory loads and
  `memory.grow` behavior. The targeted `imports.wast` run passes all 144
  assertions, and the full native suite passes without changing the expected
  invalid-module handling.
- The native portion of VM-A/B/C acceptance is no longer blocked. The next
  closure increment is the browser official comparison plus checkpoint and
  page-topology evidence; no manual browser test is requested for this native
  fix alone.

Closure update (2026-09-24, browser official comparison):

- Rebuilt the self-contained browser engine and ran
  `node tests/c-engine-browser-runtime.cjs build/html-rt/test.html`.
  Every `core/*` official WAST fixture passed, including bulk memory,
  memory64, multi-memory, linking, SIMD memory, and the imported-memory
  cases. This closes the browser portion of the VM-A/B/C official
  memory/linking comparison.
- The browser runner initially reported unasserted setup modules as failures
  when they could not be instantiated. It now matches the native runner:
  only explicit module assertions contribute results, while failed
  unasserted setup modules are discarded. Failed retained parses are also
  released so large scripts do not leak browser heap space.
- The browser engine link now reserves 64 MiB initially and permits up to
  512 MiB, which is required for the large official WAST scripts while still
  using the same engine-owned allocator. The targeted large `const.wast` and
  `simd_const.wast` runs pass.
- Six non-core `libc-test/*` fixtures still report `unknown module id` in the
  broad dashboard run. They are outside the VM-A/B/C official memory/linking
  gate and remain separate libc/browser-harness work; they do not block this
  VM closure increment. No manual browser interaction is required because
  the headless harness produced the evidence.

Closure update (2026-09-24, checkpoint and page-topology acceptance):

- The sanitized store-checkpoint fixture now initializes both `pages` and
  `linear_pages`, preserving the engine invariant used by imported and
  module-owned memories. It passes 25/25 checks, including nested restore,
  sparse growth, shared-page identity, imported-memory identity, and clone
  isolation.
- The sanitized process-lifecycle fixture passes 108/108 checks, including
  forked shared-page aliases, bilateral shared VMA metadata, protection and
  unmap/remap topology, file-mapping ownership records, and child cleanup.
- The POSIX kernel sanitizer passes 322/322 checks, and the i32 ASan/UBSan
  smoke gate remains green. Together with the native 97/97 and browser
  `core/*` official suites, this closes the VM-A/B/C acceptance evidence for
  the current page/checkpoint implementation.
- The remaining VM-plan work is closure bookkeeping plus an explicit review
  of process-checkpoint serialization boundaries. No manual browser test is
  required for this native topology increment.

Closure boundary review (2026-09-24, process-checkpoint scope):

- The current store checkpoint API snapshots engine-owned memory page maps,
  page protections, VMA interval records, backing-page aliases, imported
  memory identity, linked engine state, and the store-wide shared-memory
  namespace. The 25-check checkpoint fixture covers those page and alias
  invariants, including nested capture/restore.
- It deliberately does not serialize the complete POSIX descriptor table or
  every live process capsule field. Descriptor/file-mapping ownership is
  cloned and cleaned up through the process lifecycle path, while checkpoint
  restore currently targets the engine/store memory graph. Full descriptor and
  process-capsule serialization would be a separate POSIX checkpoint feature,
  not a prerequisite for the page-aware VM-C gate.
- This review closes the VM-A/B/C acceptance boundary without claiming full
  POSIX process checkpointing. VM-D through VM-G remain active until their
  stated process-image, explicit-region, file-mapping, and shared-memory
  boundaries are reconciled.

Closure update (2026-09-24, VM-D process-image/libc boundary):

- `node tests/libc-test/libc-runtime.cjs` passes all 14 guest-libc suites.
- `make -C src/html-rt BUILD_DIR=../../build/html-rt build-coreutils` passes
  the import-closed Coreutils `true`/`false` gate.
- The existing offline Bash process-image gate passes 5/5 checks and observes
  `/usr/bin/true` executing through the loader with exit status zero.
- VM-D is complete for the current guest-libc ABI and process-image scope.
  The long-double helpers remain a double-backed compatibility layer; future
  precision hardening is not a virtual-memory closure requirement.
- Ownership advances to VM-E's explicit-region boundary: separate
  module-visible linear memory from process virtual reservation and assign
  module, stack, startup, `brk`, and mapping regions. No manual browser test is
  required for this closure increment; the existing headless evidence is the
  browser gate.

Implementation update (2026-09-23, sparse allocation regression slice):

- Extended the native memory-contract regression to begin with an empty
  two-page map. It verifies that reads from both unmaterialized pages return
  zero without allocating storage, that a cross-page write materializes both
  touched pages, and that the existing data remains intact.
- Added checks that growing from two to three pages preserves the original page
  objects, creates a null third-page entry, leaves a zero-fill of the new page
  unmaterialized, and materializes it only after a nonzero write.
- The native warnings-as-errors ASan/UBSan `i32-smoke` gate passes with these
  checks. VM-B remains in progress; page-reference sharing, COW, protection,
  and full official memory/linking comparison are still pending. No browser
  test is needed for this native sparse-map regression increment.

Implementation update (2026-09-23, backing-page ownership slice):

- Materialized entries are now `exec_memory_page` objects containing page bytes
  and an engine-owned reference count. The access layer allocates and releases
  these objects, while null entries continue to represent implicit zero pages.
- Instance cloning creates independent backing-page objects and copies only
  materialized bytes. Memory release, shrink, checkpoint replacement, and
  spectest cleanup now go through page ownership rather than freeing flat
  linear-memory allocations.
- The native ASan/UBSan `i32-smoke` gate, native `wast-native` build, browser
  `wast-browser` build, and `git diff --check` pass. This increment does not
  yet share a backing page between mappings; the reference count establishes
  the ownership API required by VM-C's shared-page and COW work.
- VM-B remains in progress. The next implementation work is to use the page
  ownership primitive in transactional mapping metadata and then add the
  process-clone/shared-page tests required before VM-C can begin.

Implementation update (2026-09-23, clone ownership regression slice):

- Extended the native engine smoke test to clone an instantiated engine after
  its data segment materializes one page. It verifies that the clone preserves
  the sparse page topology, receives a distinct backing-page object, and can
  write its copy without changing the source memory.
- The native ASan/UBSan `i32-smoke` gate passes with the clone check. The
  browser and native engine builds from the backing-page slice remain green.
- This confirms the current VM-B ownership rule: ordinary engine cloning is
  deep-copy isolation. Shared backing identity and copy-on-write are reserved
  for explicit process mappings in VM-C, not inferred from ordinary module
  cloning.

Implementation update (2026-09-23, per-page protection metadata slice):

- Added per-page protection metadata to `exec_memory`. New pages default to
  readable and writable; the bounded read/write helpers enforce those bits
  across every page touched by an operation.
- Added `exec_memory_set_protection`, which validates the full page range and
  protection mask before changing any entry. This is an engine-internal,
  page-granular primitive for the future POSIX `mprotect` implementation; it
  does not yet expose `mprotect` or executable data memory.
- Instantiation, growth, cloning, checkpoint replacement, and spectest setup
  now initialize or preserve the protection map. The sparse memory regression
  verifies read-only access succeeds while writes trap, then restores
  read/write access.
- Native ASan/UBSan `i32-smoke`, browser `wast-browser`, and `git diff --check`
  pass. VM-B remains in progress; transactional VMA metadata, file mappings,
  and process-shared/COW backing are still future work. No manual browser test
  is needed for this internal protection-metadata increment.

Implementation update (2026-09-23, protection transaction regression slice):

- Extended the native contract test to verify a multi-page read-only update
  applies to every page in the range. It also verifies that an out-of-range
  update and an invalid protection mask fail without changing the permissions
  of already-valid pages.
- The native ASan/UBSan `i32-smoke` gate and `git diff --check` pass. This
  establishes the all-or-nothing behavior required before protection changes
  are exposed through POSIX mapping calls.
- VM-B remains in progress. The next design step is to replace the current
  page-parallel metadata with explicit virtual mapping entries so partial
  unmapping and mapping splits can be represented without changing the Wasm
  access contract. No browser test is needed for this native-only regression.

Implementation update (2026-09-23, interval mapping metadata slice):

- Added explicit sorted interval records to each `exec_memory`. A record tracks
  its first page, length, and protection, providing the representation needed
  for later partial unmapping and mapping splits.
- Protection changes now construct a replacement interval list first, split
  overlapping records into before/changed/after pieces, and commit both the
  interval list and page permissions only after allocation and validation
  succeed. Growth extends a trailing read/write interval or appends a new one.
- Cloning, initialization, spectest memory, and release now preserve or clean
  up interval metadata. The native sparse-memory regression checks the expected
  split count in addition to access behavior.
- Native ASan/UBSan `i32-smoke`, browser `wast-browser`, and `git diff --check`
  pass. VM-B remains in progress; the interval records are metadata only until
  VM-E adds actual `mmap`/`munmap` operations and VM-C adds shared/COW backing.
  No manual browser test is needed for this internal metadata increment.

Implementation update (2026-09-23, native conformance slice):

- Ran `./start.sh --cli-test` against the paged backend after the interval
  mapping changes. All 97 official core WAST files passed, including the
  memory, memory-growth, memory-size, memory-trap, data-segment, and linking
  groups.
- The native conformance gate completed with 97 passed and 0 failed. This
  establishes that the sparse page storage, protection metadata, and interval
  bookkeeping preserve current native Wasm behavior.
- VM-B remains in progress because the browser official-test comparison has not
  yet been completed, and shared/COW page references plus actual POSIX mapping
  operations remain later stages. No manual browser test is required for this
  native-only conformance increment.

Implementation update (2026-09-23, headless browser conformance slice):

- Built the self-contained browser dashboard with `./start.sh --html-test` and
  ran `node tests/c-engine-browser-runtime.cjs build/html-rt/test.html`.
- The browser run passed the relevant memory behavior groups, including
  `core/memory.wast`, `memory64/*` memory tests, `memory_grow.wast`,
  `memory_size.wast`, `memory_trap.wast`, all multi-memory memory groups,
  SIMD memory tests, data segments, and linking. This gives browser evidence
  for the sparse page backend preserving current Wasm memory semantics.
- The full harness still failed 23 files. The failures are attributable to
  existing unrelated gaps: unsupported bulk/table instruction decoding,
  parser group-capacity exhaustion in `const.wast` and `simd_const.wast`, an
  unresolved memory64 table import, and guest-libc fixture imports such as
  `env.__extenddftf2` and `waste-libc.*`. They do not report failures from the
  ordinary or memory64 memory groups themselves.
- VM-B remains open pending cleanup of those pre-existing browser gates,
  explicit shared/COW backing, and POSIX mapping operations. No interactive
  browser test is required; the headless harness produced the evidence.

Implementation update (2026-09-23, first VM-C copy-on-write slice):

- `exec_clone_engine`, which is used by the process-capsule fork path, now
  shares materialized backing-page objects and increments their references
  instead of eagerly copying every page. Writes and nonzero fills detect a
  multiply referenced page, allocate a private copy, and detach before
  mutation.
- The native clone regression now verifies both halves of the contract: the
  source and clone initially point at the same backing page, then a clone write
  produces a distinct page and leaves the source byte unchanged.
- Native ASan/UBSan `i32-smoke`, browser `wast-browser`, and `git diff --check`
  pass after the COW slice. The earlier headless browser memory groups remain
  the relevant browser evidence.
- VM-C is now in progress. Explicit shared mappings, fork address-space
  snapshots, page-table alias preservation, and process-level COW tests remain
  to be implemented; ordinary imported-memory aliasing still follows its
  existing module-linking rules.

Implementation update (2026-09-23, checkpoint page-topology slice):

- Checkpoint memory snapshots now retain the page objects themselves instead
  of flattening each memory into a contiguous byte buffer. They also preserve
  per-page protections and the sorted interval-mapping records.
- Restore builds a replacement page map and mapping list before committing it,
  retains each restored backing page, and releases the previous map through
  the normal ownership path. Repeated restore and checkpoint destruction
  therefore keep page references balanced while preserving sparse pages and
  copy-on-write identity.
- Updated the store checkpoint fixture to use the bounded memory API rather
  than the removed flat buffer. The sanitized checkpoint test passes all 16
  checks, including memory growth/restore and clone isolation; native
  ASan/UBSan `i32-smoke`, browser `wast-browser`, and `git diff --check` also
  pass.
- VM-C remains in progress. The remaining work is explicit shared mapping
  identity, process-level fork address-space snapshots, alias-preserving
  `MAP_SHARED` behavior, and nested process tests. No manual browser test is
  needed for this checkpoint-internal increment.

Implementation update (2026-09-23, explicit shared-page slice):

- Added an engine-internal `exec_memory_share_pages` operation that replaces
  a destination page range with retained references to a source page range.
  The operation validates both ranges first and stages the source pointers so
  overlapping or self-mapping ranges do not observe partially replaced state.
- Backing pages now distinguish explicit shared identity from ordinary
  copy-on-write sharing. A private clone still detaches on its first write,
  while a page installed through the shared-page operation remains writable
  through both memories and changes are immediately visible to each side.
- Extended the native COW regression to cover both paths. The native
  ASan/UBSan `i32-smoke` gate passes, the checkpoint fixture passes 21 checks,
  the browser `wast-browser` build passes, and `git diff --check` passes.
- VM-C remains in progress. This is the backing primitive, not the complete
  POSIX `mmap(MAP_SHARED)` implementation; process address-space graph
  integration, shared mapping metadata, and fork-level tests remain next.
  No manual browser test is needed for this engine-internal increment.

Implementation update (2026-09-23, fork shared-identity slice):

- Extended the native process lifecycle gate to construct two parent memory
  aliases, fork the process capsule, and verify that the child retains the
  same backing-page identity across both aliases and against the parent.
- The regression writes through the child’s shared alias, observes the write
  through the parent’s alias, then exits and reaps the child. This exercises
  the actual capsule fork path rather than only calling the low-level sharing
  helper directly.
- The sanitized process lifecycle test passes 90 checks with zero failures;
  native ASan/UBSan `i32-smoke`, browser `wast-browser`, and `git diff --check`
  also pass.
- VM-C remains in progress. The next work is to represent shared identity in
  explicit VMA metadata and connect it to fork/`mmap(MAP_SHARED)` lifecycle
  operations, including unmap and cleanup behavior. No manual browser test is
  needed for this native process regression.

Implementation update (2026-09-23, shared VMA metadata slice):

- Added a shared flag to interval mapping records. The explicit page-sharing
  operation now splits the destination interval list as needed and marks the
  affected VMA shared before installing the retained page references.
- Protection changes preserve the shared flag across interval splits, and
  clone/checkpoint paths preserve it through their existing structure copies.
  This separates the VMA’s intended sharing policy from the page reference
  count used by private COW.
- The process lifecycle regression now checks shared VMA metadata in the
  forked child in addition to backing-page identity and write visibility.
  Process lifecycle passes 95 checks, native ASan/UBSan `i32-smoke`, browser
  `wast-browser`, and `git diff --check` pass.
- VM-C remains in progress. Actual `mmap(MAP_SHARED)` creation, source-side
  mapping records, unmap/remap cleanup, and nested checkpoint tests remain.
  No manual browser test is needed for this metadata-only increment.

Implementation update (2026-09-23, bilateral shared-VMA slice):

- Shared-page installation now stages interval-list replacements for both the
  destination and source memories before committing either one. Both sides of
  an explicit shared relationship therefore carry the shared VMA flag, while
  allocation failure leaves both mapping lists unchanged.
- The fork regression now checks the source-side parent VMA and the child’s
  cloned VMA in addition to page identity and write visibility. Process
  lifecycle passes 95 checks, native ASan/UBSan `i32-smoke`, browser
  `wast-browser`, and `git diff --check` pass.
- VM-C remains in progress. The remaining work is actual `mmap(MAP_SHARED)`
  creation, unmap/remap cleanup, and nested checkpoint tests over shared VMA
  relationships. No manual browser test is needed for this metadata-only
  increment.

Implementation update (2026-09-23, transactional unmap slice):

- Added `exec_memory_unmap_pages`, which stages the replacement interval list
  before releasing backing pages. The removed pages become inaccessible by
  clearing their protection metadata, and shared-page references are released
  through the normal page ownership path.
- The native memory contract regression now verifies that unmapping a middle
  page splits the surrounding intervals, releases the page, and rejects later
  reads and writes to the unmapped range.
- Native ASan/UBSan `i32-smoke`, the 21-check checkpoint fixture, browser
  `wast-browser`, and `git diff --check` pass. No manual browser test is
  needed for this engine-internal unmap increment.
- VM-C remains in progress. POSIX-facing `mmap`/`munmap` validation, remapping
  policy, nested shared-checkpoint cases, and process-level cleanup tests are
  still required.

Implementation update (2026-09-23, explicit remap slice):

- Added `exec_memory_map_pages` for remapping an entirely unmapped in-bounds
  range. It validates protection and mapping flags, rejects overlap with an
  existing VMA, inserts a sorted interval record, and leaves the backing page
  entries sparse until a write materializes them.
- Extended the memory contract regression to unmap a middle page, remap it,
  verify the interval list returns to three records, and confirm the remapped
  page accepts reads and writes again.
- Native ASan/UBSan `i32-smoke`, the 21-check checkpoint fixture, browser
  `wast-browser`, and `git diff --check` pass. No manual browser test is
  needed for this engine-internal remap increment.
- VM-C remains in progress. POSIX-facing mapping validation, shared remap
  source/backing policy, nested shared checkpoints, and process cleanup tests
  remain before this can become the public `mmap`/`munmap` layer.

Implementation update (2026-09-23, nested shared-checkpoint slice):

- Checkpoint capture now copies each unique live backing page once and reuses
  that copied page for every alias represented in the snapshot. This keeps
  checkpoint aliases intact while preventing later writes to live shared pages
  from mutating the saved state.
- Extended the checkpoint fixture with two explicitly shared memory objects.
  It captures an initial checkpoint, captures a post-growth nested checkpoint,
  restores the nested state, mutates it, then restores the initial state. The
  checks cover page identity, growth, shared visibility, and clone behavior.
- The sanitized checkpoint fixture passes 25 checks, native ASan/UBSan
  `i32-smoke`, browser `wast-browser`, and `git diff --check` pass. No manual
  browser test is needed for this checkpoint-internal increment.
- VM-C remains in progress. Public POSIX mapping calls, shared remap source
  policy, and process cleanup/error-path tests remain before the stage gate.

Implementation update (2026-09-23, mapping error-path slice):

- Hardened the paged mapping contract with regression checks for overlapping
  remaps, invalid mapping flags, and out-of-range unmaps. Each failure must
  leave the interval count, protection metadata, and already mapped pages
  unchanged.
- The native ASan/UBSan `i32-smoke` gate passes with the expanded mapping
  lifecycle checks, and `git diff --check` passes. This is a native-only
  validation increment; no manual browser test is needed.
- VM-C remains in progress. The next boundary is connecting these validated
  engine operations to the future kernel-facing POSIX `mmap`/`munmap` ABI,
  followed by process cleanup and browser process tests.

Implementation update (2026-09-23, process VM entry-point slice):

- Added process-capsule wrappers for mapping and unmapping engine virtual page
  ranges. They operate on the process-owned primary memory and translate
  invalid engine operations into a process-facing `EINVAL`; they do not expose
  host pointers or move platform behavior into `src/engine/`.
- Extended the process lifecycle fixture to grow, unmap, and remap a page
  through the capsule API before fork. The process lifecycle gate passes 96
  checks, browser `wast-browser` rebuilds successfully, and `git diff --check`
  passes.
- VM-C remains in progress. The wrappers are the kernel-facing seam, not yet
  the guest `mmap`/`munmap` ABI; address/length/protection translation, file
  mappings, and browser process evidence remain next. No manual browser test
  is needed for this native API-surface increment.

Implementation update (2026-09-23, byte-range translation slice):

- Added process-capsule byte-range wrappers that round nonzero lengths to
  pages, reject unaligned unmaps and overflowing/empty lengths, select the
  first available hole for an address-zero map, and return the resulting guest
  virtual byte address.
- Extended the process lifecycle fixture to unmap a non-page-sized range and
  remap it through these wrappers. Process lifecycle passes 96 checks, browser
  `wast-browser` rebuilds successfully, and `git diff --check` passes.
- VM-C remains in progress. These wrappers still cover anonymous engine pages
  only; guest ABI errno conventions, file-backed mappings, fixed-address
  policy, and browser process evidence remain before the public POSIX layer.
  No manual browser test is needed for this native translation increment.

Implementation update (2026-09-23, process protection slice):

- Added a process-capsule `mprotect`-style byte-range wrapper. It rounds the
  requested length to pages, rejects unaligned/empty/out-of-range requests and
  unmapped pages, then delegates protection changes to the transactional
  engine operation.
- Extended the process lifecycle fixture to make a remapped page read-only,
  verify writes trap, restore read/write access, and verify writes succeed.
  Process lifecycle passes 97 checks, browser `wast-browser` rebuilds
  successfully, and `git diff --check` passes.
- VM-C remains in progress. Guest ABI errno mapping, file-backed/shared-file
  mappings, fixed-address policy, and browser process evidence remain before
  the public POSIX mapping layer. No manual browser test is needed for this
  native protection-translation increment.

Implementation update (2026-09-23, browser mmap seam slice):

- Added browser POSIX resolver entries for anonymous `mmap` and `munmap`.
  They translate the guest byte arguments into the process-capsule range API,
  accept private/shared anonymous mappings, return `MAP_FAILED` on mapping
  failure, and preserve negative errno-style results for unmap failures.
- The native process lifecycle gate remains green at 97 checks; the browser
  `wast-browser` artifact rebuilds successfully and `git diff --check` passes.
- This is the ABI seam only: file descriptors, file-backed mappings,
  `mprotect` import wiring, and a dedicated browser mmap fixture remain. A
  manual browser test is not required yet; the next browser-facing increment
  should add a fixture and produce evidence through the headless harness.

Implementation update (2026-09-23, browser protection seam slice):

- Added the guest libc `mprotect` declaration and browser resolver entry. The
  stub uses the process capsule’s byte-range validation, so unaligned,
  unmapped, and invalid protection requests return the same errno-style
  failures as the native process API.
- The native process lifecycle gate remains green at 97 checks; the browser
  `wast-browser` artifact rebuilds successfully and `git diff --check` passes.
- The anonymous mapping ABI now has its initial `mmap`/`munmap`/`mprotect`
  surface. A dedicated WAST/browser fixture, file-backed mappings, and
  `errno` storage remain before claiming browser runtime evidence. No manual
  browser test is needed for this resolver/build-only increment.

Implementation update (2026-09-23, browser anonymous-mapping evidence slice):

- Added `tests/diy-posix-test/mmap.wast` covering address-zero anonymous
  mapping, unmapping, read-only protection, restoration to read/write, and
  invalid mapping flags. The fixture is deliberately routed through the C
  engine WAST stream in the dashboard so its `env.mmap`, `env.munmap`, and
  `env.mprotect` imports resolve through `browser_host_resolver` and the
  process capsule, rather than through the older JavaScript compatibility
  kernel.
- Regenerated the offline dashboard and ran the fixture through the headless
  browser harness: `PASS diy-posix-test/mmap.wast`. Native process lifecycle,
  i32 smoke, and nested checkpoint gates also pass (97, 0 failures; 25,
  0 failures for the checkpoint fixture), and `git diff --check` passes.
- This closes the initial anonymous browser ABI evidence slice, but not VM-E:
  file-backed mappings, stable guest `errno`, fixed-address policy, mapping
  collision/split coverage, and shared/private fork behavior remain. No manual
  browser interaction is required for this increment; the headless browser
  evidence is sufficient. The next implementation boundary is the kernel
  mapping error/`errno` contract before file-backed mappings.

Implementation update (2026-09-23, mapping errno contract slice):

- Connected anonymous `mmap`, `munmap`, and `mprotect` failures to the
  caller's guest `__errno_location` slot. Successful calls leave `errno`
  unchanged; invalid arguments and failed process-capsule operations publish
  deterministic `EINVAL`/`ENOMEM` values while preserving the existing return
  conventions (`MAP_FAILED` or a negative errno result).
- Extended `tests/diy-posix-test/mmap.wast` with invalid-call errno checks.
  The fixture now contains seven assertions and passes both the native
  parser and the regenerated offline C-engine dashboard through the headless
  harness: `PASS diy-posix-test/mmap.wast`.
- Native process lifecycle, i32 smoke, and nested checkpoint gates remain
  green (97, 0 failures; 25, 0 failures for the checkpoint fixture), and
  `git diff --check` passes. This closes the initial anonymous mapping error
  contract. The next boundary is fixed-address/collision and mapping-split
  coverage, followed by file-backed mappings; no manual browser interaction
  is required for this increment.

Implementation update (2026-09-23, fixed collision and split slice):

- Expanded the VM fixture to a three-page address space. It now maps a
  contiguous three-page anonymous region, unmaps the middle page, remaps that
  exact fixed-address hole, and attempts an overlapping fixed mapping. The
  overlap returns `MAP_FAILED` with `EINVAL`; subsequent errno and mapping
  assertions confirm the failed transaction did not damage the neighboring
  mappings.
- The native WAST parser recognizes all nine fixture assertions, and the
  regenerated offline C-engine dashboard passes the targeted headless run:
  `PASS diy-posix-test/mmap.wast`. Existing native mapping checks continue to
  pass through the i32 smoke gate, including overlap rejection and unchanged
  protection metadata.
- The fixed-address/collision and interval-split evidence slice is complete.
  File-backed mappings, `MAP_FIXED_NOREPLACE` policy, and shared/private fork
  behavior remain for the next VM-E boundary. No manual browser interaction is
  required for this increment.

Implementation update (2026-09-23, private file-mapping source slice):

- Added a kernel-owned `posix_kernel_file_read_at` operation. It reads a
  regular file's bytes at a specified offset without changing the descriptor
  offset, rejects non-regular/unreadable descriptors, and rejects ranges past
  EOF. This gives the mapping layer a VFS source without exposing path-node or
  host pointers across the engine boundary.
- Extended the browser POSIX `mmap` resolver to accept non-anonymous,
  page-aligned file mappings for `MAP_PRIVATE`. The resolver stages each
  full Wasm page through the process memory API and then applies the requested
  protection transactionally. File `MAP_SHARED`, partial final pages, and
  writeback are intentionally rejected until shared VFS-backed page ownership
  exists.
- The native POSIX kernel fixture now passes 305 checks, the existing
  headless browser VM fixture remains `PASS diy-posix-test/mmap.wast`, the
  browser artifact rebuilds successfully, and `git diff --check` passes. No
  manual browser interaction is required for this source-seam increment.
- The next boundary is a native and browser fixture with a real VFS file and
  private file mapping contents, followed by shared file-backed pages,
  partial-EOF policy, and `msync`/writeback semantics.

Implementation update (2026-09-23, VFS private-file browser slice):

- Added VFS file staging to the C-engine WAST browser worker and extended the
  VM fixture with a full-page `/mmap-file`. The fixture opens that file through
  the engine-owned `env.open`, performs a private file-backed `mmap`, and
  verifies the staged bytes with a Wasm load after the mapping is populated.
- The fixture now has ten assertions. Native parsing passes, and the targeted
  offline browser harness reports `PASS diy-posix-test/mmap.wast`. The native
  POSIX kernel fixture passes 305 checks, i32 smoke remains green, the browser
  artifact rebuilds successfully, and `git diff --check` passes.
- This closes the first real VFS-to-private-mapping path. The current slice
  requires a full-page file range and does not provide file `MAP_SHARED`,
  partial-final-page behavior, truncation/SIGBUS policy, or writeback. Those
  remain the next file-mapping increment; no manual browser interaction is
  required for this increment.

Implementation update (2026-09-23, partial-final-page slice):

- Added a kernel file-size query and changed private file mapping population
  to distinguish the requested byte range from the rounded virtual-page
  range. A mapping may now end inside the final file page: available file
  bytes are copied and the remainder of that page is zero-filled. Requests
  that extend beyond EOF are rejected deterministically for this first ABI.
- Added a four-byte `/mmap-short` VFS fixture. The browser test maps four
  bytes privately and verifies the first file byte plus a zero byte in the
  rounded tail. The fixture now has eleven assertions and passes the native
  parser and targeted headless C-engine browser harness.
- Native POSIX kernel remains at 305 passing checks, i32 smoke remains green,
  the browser artifact rebuilds, and `git diff --check` passes. No manual
  browser interaction is required. Shared file-backed pages, fork-visible
  `MAP_SHARED`, truncation/SIGBUS behavior, and `msync` writeback remain.

Implementation update (2026-09-23, shared file-page marking slice):

- Added `exec_memory_mark_shared_pages` and connected file `MAP_SHARED`
  population to it. Once the VFS bytes are loaded, materialized pages carry
  the shared backing bit, so the existing fork clone path will retain their
  backing identity instead of applying private copy-on-write.
- Added a `MAP_SHARED` VFS case to the browser fixture. The fixture now has
  twelve assertions and passes native parsing and the targeted headless
  C-engine browser harness. The native POSIX kernel remains at 305 passing
  checks, i32 smoke passes, the browser artifact rebuilds, and
  `git diff --check` passes.
- This proves shared mapping admission and page marking, not the complete
  POSIX shared-file contract. Independent processes remapping the same
  pathname still receive separate VFS snapshots; fork-visible writes,
  shared VFS backing objects, truncation/SIGBUS behavior, and `msync`
  writeback remain the next boundary. No manual browser interaction is
  required for this increment.

Implementation update (2026-09-23, fork-visible shared-page slice):

- Extended the process lifecycle fixture so a materialized shared mapping is
  cloned through `fork`. The child writes the mapped page and the parent
  reads the new byte through its own virtual address space, proving that the
  shared-page bit suppresses private COW detachment for this mapping path.
- Native process lifecycle now passes 99 checks with zero failures; the
  native POSIX kernel remains at 305 checks, i32 smoke passes, the targeted
  headless browser fixture remains `PASS diy-posix-test/mmap.wast`, and
  `git diff --check` passes.
- This closes fork-visible page identity for the current file-mapping seam.
  Independent processes mapping the same pathname still do not share one
  kernel file object, and file writes are not propagated to VFS storage.
  Shared VFS backing objects, dirty-page/writeback policy, truncation/SIGBUS,
  and `msync` remain the next file-mapping boundary. No manual browser
  interaction is required for this increment.

Implementation update (2026-09-23, VFS write-at prerequisite slice):

- Added `posix_kernel_file_write_at`, the offset-based VFS write primitive
  needed by future `msync` and unmap writeback. It preserves the descriptor's
  current offset, enforces writable regular-file descriptors, grows the file
  deterministically, and zero-initializes any newly extended gap.
- Extended the native VFS fixture to verify offset-independent writes and
  subsequent reads. The POSIX kernel gate now passes 306 checks; process
  lifecycle remains at 99 checks, i32 smoke passes, the targeted headless
  browser VM fixture remains green, and `git diff --check` passes.
- This is the storage-side writeback seam, not `msync` itself. Dirty-page
  tracking, mapping-to-file ownership records, shared VFS objects across
  independently created processes, truncation/SIGBUS behavior, and actual
  `msync`/unmap writeback remain the next boundary. No manual browser
  interaction is required for this increment.

Implementation update (2026-09-23, file-mapping ownership slice):

- Added process-capsule records for regular-file mappings. Each record keeps
  the virtual range, descriptor, file offset, and private/shared mode, so a
  future writeback operation has engine-owned ownership metadata rather than
  needing to infer it from page bytes alone.
- File `mmap` records the requested file-backed byte range after population
  succeeds while the VM layer retains the rounded page allocation.
  `munmap` removes or splits overlapping records and adjusts the right-hand
  file offset; capsule cloning copies the records for `fork`.
- Added regression coverage for split offsets and fork preservation. Native
  process lifecycle now passes 101 checks; the POSIX kernel remains at 306
  checks, the browser artifact rebuilds, the targeted headless browser VM
  fixture passes, i32 smoke remains green, and `git diff --check` passes.
- This closes the metadata prerequisite for `msync` and unmap writeback. It
  does not yet track dirty pages, share file backing objects across
  independently created processes, or perform writeback. No manual browser
  interaction is required for this increment.

Implementation update (2026-09-23, explicit `msync` writeback slice):

- Added the guest `msync` ABI and a browser-runtime implementation for
  `MS_SYNC`. It walks the process capsule's file-mapping records, requires the
  requested range to remain fully file-backed, and writes each `MAP_SHARED`
  intersection to the VFS with offset-based I/O. `MAP_PRIVATE` intersections
  are accepted without modifying the file.
- Added stable failures for unsupported flag combinations, invalid alignment,
  and ranges containing an unmapped or anonymous gap. The implementation
  writes the requested shared bytes eagerly; it does not claim dirty-page
  optimization yet.
- Expanded the offline VM fixture to 16 assertions. It now writes through a
  shared mapping, calls `msync`, remaps the file and observes the updated byte;
  it also proves private changes do not write back and checks invalid flags and
  unmapped-range errno. Native parsing and the targeted headless browser run
  pass.
- Process lifecycle remains at 101 passing checks, the POSIX kernel remains at
  306, i32 smoke passes, the browser artifact rebuilds, Python bytecode checks
  pass, and `git diff --check` is clean. Dirty-page tracking, unmap writeback,
  mapping lifetime after descriptor closure, shared VFS objects across
  independently created processes, and truncation/SIGBUS behavior remain.
  No manual browser interaction is required for this increment.

Implementation update (2026-09-24, dirty-page and unmap-writeback slice):

- Added a dirty bit to reference-counted backing pages. Engine writes and
  fills mark the materialized destination page dirty, shared aliases observe
  the same bit, and file mapping population clears it after the initial VFS
  bytes are installed.
- Added an engine backing-read operation for kernel writeback. It retains
  bounds checks while intentionally bypassing guest `mprotect` permissions,
  allowing the kernel to flush a valid write-only or protected mapping without
  exposing that bypass to guest Wasm loads.
- `munmap` now writes dirty `MAP_SHARED` intersections to the VFS before
  releasing pages and mapping records. Private and untouched shared mappings
  perform no writeback. File records retain the requested byte length, so
  unmapping a partial final page does not extend the file to a full Wasm page.
- The offline VM fixture now has 17 assertions and proves that a dirty shared
  mapping persists its byte when unmapped without an explicit `msync`. The
  targeted headless browser run and native parser pass. Native process
  lifecycle now passes 102 checks, including dirty-state identity through a
  shared backing page; the POSIX kernel remains at 306 checks and i32 smoke
  remains green. The store-checkpoint sanitizer gate passes all 25 checks.
- The browser artifact rebuilds, Python bytecode checks pass, and
  `git diff --check` is clean. Dirty tracking is currently page-granular and
  conservatively remains set after a partial-page `msync`. Mapping lifetime
  after descriptor closure, shared VFS objects across independently created
  processes, and truncation/SIGBUS behavior remained for the next increments.
  No manual browser interaction was required for this increment.

Implementation update (2026-09-24, descriptor-independent mapping slice):

- File mapping records now retain the VFS object's stable inode identity and
  the write capability captured from the originating open-file description,
  rather than retaining a guest descriptor number. Kernel writeback resolves
  the object identity directly, so closing or reusing that descriptor no
  longer invalidates `msync` or dirty unmap writeback.
- Added VFS object-identity and object-write operations. The native kernel
  fixture captures a writable identity, closes its descriptor, writes through
  the identity, and verifies the named file changed. The POSIX kernel gate now
  passes 308 checks.
- A writable shared mapping now requires a writable source descriptor, and
  `mprotect` cannot add write permission to a shared file mapping that did not
  capture write authority. This prevents descriptor-independent writeback
  from widening the mapping's original access rights.
- The offline VM fixture now has 18 assertions. Its shared `msync` and unmap
  writeback cases close the source descriptor before modifying the mapping,
  then reopen and remap the file to verify persistence. It also verifies
  `EACCES` when adding write permission to a read-only shared mapping. Native
  parsing and the targeted headless browser run pass.
- Process lifecycle remains at 102 passing checks and now verifies that fork
  cloning preserves object identity and write authority. The browser artifact
  rebuilds, i32 smoke and Python bytecode checks pass, and `git diff --check`
  is clean. Unlink-while-mapped lifetime still needs a reference-counted VFS
  object detached from pathname entries; shared VFS coherence across process
  kernels and truncation/SIGBUS behavior also remain. No manual browser
  interaction is required for this increment.

Implementation update (2026-09-24, reference-counted VFS object slice):

- Promoted regular-file contents to reference-counted VFS objects. Pathname
  entries, open file descriptions, mappings, and forked kernel clones now
  retain references to the object independently of the directory entry.
  Unlink removes the pathname reference but does not destroy bytes still
  reachable through a descriptor or mapping.
- Mapping records now retain the file object directly. `msync` and unmap
  writeback no longer search the pathname table, so they continue to work
  after unlink. Forked kernel clones share the same object and therefore see
  each other's regular-file writes through the engine VFS.
- Added browser coverage for modifying a shared mapping after unlink and
  reading the result through the still-open descriptor. Added a native fork
  kernel check for shared object contents. The VM fixture now has 19
  assertions; the targeted headless browser run passes, native parsing passes,
  the POSIX kernel gate passes 309 checks, process lifecycle passes 104 checks,
  store checkpoints pass 25 checks, and `git diff --check` is clean.
- This closes descriptor-close and unlink-while-mapped lifetime for regular
  files and establishes fork-visible VFS object identity. Independently
  created kernels still need an explicit shared namespace/object registry;
  truncation, access past a truncated mapping, `SIGBUS` translation, and
  dirty-page precision remain. No manual browser interaction is required for
  this increment.

## Stage VM-B: Introduce paged backing without process sharing

Status: complete for paged backing and native/browser official gates; POSIX
mapping extensions continue in VM-E/VM-F

Work:

- Add reference-counted backing pages and virtual mapping entries.
- Convert allocation, destruction, bounds checking, `memory.size`, and
  `memory.grow` to the page map.
- Implement cross-page scalar, SIMD, copy, fill, and data-initialization paths.
- Preserve imported-memory identity and all existing multi-memory indices.
- Add a process-local translation fast path only after correctness tests pass.
- Put explicit limits on page-table metadata and materialized bytes so sparse
  memory64 declarations cannot exhaust the host.

Gate:

- Reads from unmaterialized in-bounds pages return zero.
- Writes materialize only the affected pages.
- Accesses spanning page boundaries behave exactly like contiguous memory.
- Failed growth is atomic.
- Official Wasm and sanitizer gates remain green in native and browser builds.

## Stage VM-C: Make checkpoints and process cloning page-aware

Status: complete for page-aware engine checkpoints and process cloning; full
kernel descriptor-table serialization remains an explicit later boundary

Work:

- Snapshot page maps and backing identity with balanced references.
- Restore mappings, protections, page counts, and aliases transactionally.
- Replace eager process-memory cloning with copy-on-write private pages.
- Preserve shared mapping identity through `fork` while keeping private writes
  isolated.
- Ensure a forked child retains only the calling thread when guest threads are
  implemented.

Gate:

- Parent and child initially observe identical bytes.
- A private child write does not alter the parent and materializes one backing
  page.
- A shared child write is immediately visible to the parent.
- Nested checkpoint capture/restore preserves both cases and leaks no pages.
- Existing fork/failed-`execve`/wait-status browser behavior remains green.

## Stage VM-D: Give each process one module-shared address space

Status: complete for the current guest-libc ABI and Coreutils process-image gate

Work:

- Make the process capsule own its application address-space graph.
- Instantiate process-local guest libc and application modules against the
  same memory object and, where required, the same table.
- Cache immutable decoded libc/application modules without sharing mutable
  instances across processes.
- Resolve ordinary libc imports through the process-local libc namespace and
  process operations through the versioned `waste_kernel` namespace.
- Initialize allocator, errno, stdio, startup data, and constructors in a
  deterministic order before entering `_start`.
- Preserve module aliases when cloning a process and replace all instances
  together on successful `execve`.

Gate:

- Libc and an application exchange pointers in both directions through one
  memory.
- Two processes running the same image have distinct private mappings,
  allocator state, errno, and stdio objects.
- Import audit accepts only documented libc and versioned kernel boundaries.
- A minimal external Wasm executable starts, exits, and can be invoked again
  from Bash without stale process state.

### VM-D.1: Bind process image memory across native and browser handoff (2026-09-24)

Status: implemented for process-image and cloned-module binding

Process image commits now bind the executable's memory access validator before
the image replaces the current process image. This keeps the handoff
transactional: an image that cannot provide a bindable memory is rejected
before the old image is released. The process-graph clone path applies the
same binding to every cloned linked module, while retaining the process capsule
as the validator context.

This closes a native/runtime asymmetry in which the browser driver rebound the
selected capsule explicitly but the native image-commit path could enter the
new image without the process-owned mapping checks. The later VM-D fixtures
cover the libc/application pointer exchange, process-local allocator/errno/
stdio state, and atomic replacement of linked instances during `execve`.

Validation:

- Native i32 smoke gate passed.
- Native process lifecycle sanitizer: 104 checks passed.
- Native store checkpoint sanitizer: 25 checks passed.
- Offline browser `mmap.wast`: 23/23 assertions passed.
- Offline C-engine Bash smoke: 5/5 checks passed.

### VM-D.2: Preserve process graph isolation and aliases (2026-09-24)

Status: implemented for forked linked-engine graphs

The process graph clone path now applies memory validation to every cloned
linked engine that owns a memory, while allowing minimal synthetic engines
used by transition tests to remain memory-less. This preserves the process
capsule as the ownership boundary: real application/libc memories receive the
child capsule's mapping checks, and imported memories continue to be rebound
through the cloned provider graph rather than falling back to the parent's
engine objects.

The related startup assertion was also moved from the removed contiguous
`memory->data` field to `exec_memory_read`, keeping the process-image test on
the common access contract.

Validation:

- Native exec transition matrix sanitizer: 57 checks passed.
- Native process lifecycle sanitizer: 104 checks passed.
- Native store checkpoint sanitizer: 25 checks passed.
- Offline browser `mmap.wast`: 23/23 assertions passed.
- Offline C-engine Bash smoke: 5/5 checks passed.

### VM-D.3: Verify the shared pointer-space boundary (2026-09-24)

Status: complete for imported-memory exchange and guest-libc integration

The provider/consumer engine smoke now serves as the focused pointer-space
gate for the process module boundary. The consumer imports the provider's
memory, stores through its own function, and the provider reads the same
address; the test also grows the shared memory and verifies the provider sees
the new page count. This exercises the same imported-memory identity that a
process-local application and guest libc must use for ordinary C pointers.

The process exec matrix complements this with cloned process graphs: the
child receives independent engine tables while imported aliases are rebound
through the child graph. The linked Coreutils/libc fixture below proves that
allocator, `errno`, stdio, constructors, and startup data use this boundary in
one executable image.

Validation:

- Native imported-memory/i32 smoke sanitizer passed, including pointer
  exchange and shared growth.
- Native exec transition matrix sanitizer: 57 checks passed.
- Native process lifecycle sanitizer: 104 checks passed.
- Offline browser `mmap.wast`: 23/23 assertions passed.

This closes the engine-level pointer alias gate for VM-D. The guest-libc and
Coreutils process-image gates below provide the application-facing fixture.

### VM-D.4: Guest-libc shared-memory preflight (2026-09-24)

Status: complete for import closure and semantic fixture coverage

The generated guest-libc client suite was rebuilt through the shared-memory
module boundary. Its allocator client starts successfully, proving that the
client imports libc memory and allocator state through the expected module
alias. The earlier broad-suite stop at `env.__extenddftf2`, emitted by
`strtold`/long-double support, was a missing compiler-runtime/libc ABI binding,
not evidence of separate application and libc memories; the typed helper now
closes that import boundary.

The import is now implemented as a typed guest helper rather than hidden by an
allowlist. It receives a guest pointer and writes the same two-word IEEE
quad representation already used by the guest `__floatditf` implementation.

Validation:

- `waste-libc` generation/build completed without rebuilding errors.
- The libc runtime harness's allocator task passed.
- The full suite now links past `env.__extenddftf2`; the semantic fixture
  boundary is covered by the completed 14-suite run recorded in VM-D.5.

No manual browser test is requested for this increment; the browser process
image gate is recorded in VM-D.7.

### VM-D.5: Guest-libc semantic follow-up boundary (2026-09-24)

Status: complete for the current libc client ABI

The import-closure fix advances the OCaml-oracle libc run from link failures to
behavioral checks. The allocator, entropy, locale, matching, memory
conversion, select, stat, stdio, terminal, time-resource,
`environment-boundaries`, and `path-runtime` clients all complete after the
errno-normalization and deterministic missing-path fixes below. This confirms
that the shared pointer-space and compiler-runtime boundary is working.

The completed Coreutils process-image gate uses the real C-engine/browser
adapters for those operations; the earlier oracle adapter mismatches were
separate from the process memory graph and no longer block VM-D.

Validation:

- Guest-libc allocator client: passed.
- `environment-boundaries`: passed.
- `path-runtime`: passed.
- Full sequential guest-libc run: 14 of 14 clients passed.
- The `__extenddftf2` unresolved-import failure no longer occurs.

Implementation update (2026-09-24, long-double import and oracle-kernel seam):

- Added the typed guest `__extenddftf2` helper, reusing the libc's explicit
  IEEE-quad word representation instead of introducing a host pointer or a
  second memory object.
- Extended the OCaml-oracle libc harness with typed `waste_kernel` and `env`
  declarations needed to link the complete generated libc module. The
  allocator client now passes end to end, and all clients link past the former
  compiler-runtime failure.
- The former `environment-boundaries` and `path-runtime` mismatches are
  resolved by the errno-normalized adapter results and deterministic missing
  `/bin/ls` path contract. They were separate from the VM-D pointer-space gate
  and no longer block the guest-libc process-image gate.

Implementation update (2026-09-24, errno-normalized libc boundary slice):

- Normalized negative errno-style results from `env` path/process calls and
  `waste_kernel:ioctl_v1` into the ordinary guest POSIX convention of `-1`
  with `errno` set, while preserving adapters that already return `-1` and
  publish errno themselves.
- Corrected directory invalid-handle checks to report `EFAULT`, and made the
  oracle path-access fixture return `ENOENT` for its deliberately missing
  `/bin/ls` path.
- Added an optional single-assertion filter to the libc oracle harness for
  isolating future client failures without changing generated fixtures.

The full guest-libc client suite now passes, so the VM-D libc pointer-space
preflight has no remaining fixture-level blocker. The Coreutils process-image
fixture is also complete in VM-D.6 and VM-D.7; VM-D is closed for its stated
ABI and process-image scope.

### VM-D.6: Coreutils `true` one-memory relink boundary (2026-09-24)

Status: complete for the one-memory/import-closure gate; browser execution is
the next integration step

The staged Coreutils `true` target compiles and links as Wasm. A relink builder
now combines it with `waste-libc.wasm` through one runtime-owned memory and
table, so the application and libc no longer have separate pointer spaces.
The post-relink import audit is now import-closed. The guest libc provides the
compiler's long-double ABI (`__*tf*` helpers and `frexpl`) using the stable
binary128 Wasm calling convention, with double-precision arithmetic internally
for this initial Coreutils image. The existing VFS/environment and versioned
kernel calls are explicitly recognized as runtime-provided boundaries.

The reusable transformation is now in
`src/html-rt/tools/build-coreutils-runtime.py`: rewrite both modules to import
the runtime-owned memory/table, merge them, and preserve the versioned
`waste_kernel` startup boundary. The long-double provider implementation is in
`src/html-rt/lib/quad.c`; it is intentionally a first compatibility layer, not
the final full-precision arithmetic implementation.

Validation:

- Coreutils `true` compilation/link target: completed.
- One-memory/table relink: completed; `build/coreutils/utility-probe/true-linked.wasm`
  is produced by the probe.
- Import audit: passed with zero unknown imports after relink.
- `build/coreutils/utility-probe/true-linked.wasm` is the import-closed
  process image and is staged as `/usr/bin/true` and `/bin/true` by the browser
  page generator.

The browser loader execution is covered by VM-D.7 and the existing headless
process-image evidence. The next work item is VM-E's explicit-region boundary;
no additional manual browser evidence is needed for VM-D.

Implementation update (2026-09-24, Coreutils audit and relink):

- `make -C src/html-rt BUILD_DIR=../../build/html-rt build-coreutils` reaches
  the utility audit reproducibly and fails only at the intentional blocked
  status check.
- The report records the source artifact as valid Wasm, with no undefined
  linker symbols and no Asyncify symbols.
- The integrated probe produces a one-memory/table linked artifact and audits
  that artifact at `build/coreutils/utility-probe/true-linked-import-audit.json`.
- The post-relink audit is now import-closed. The VFS, environment, terminal,
  path, directory, and select calls are covered by explicit runtime-provider
  allowlist entries, while the long-double calls are provided by guest libc.
- The public gate `make -C src/html-rt BUILD_DIR=../../build/html-rt
  build-coreutils` passes.

Implementation update (2026-09-24, guest-libc ABI-provider slice):

- Added `strspn` and `strcspn`, the guest-libc double `frexp`/`ldexp` entry
  points, and minimal `abort`/`atexit` providers to the shared libc image.
- The integrated Coreutils probe still builds and relinks `true`, and its
  post-relink audit fell from 32 to 14 unknown imports.
- The full guest-libc regression suite remains green: 14 suites pass.
- The remaining imports are not safely suppressible: long-double compiler
  helpers need a defined ABI implementation. Browser testing remains deferred
  until that ABI is implemented and the linked image is import-closed.

Implementation update (2026-09-24, runtime-provider boundary classification):

- Added `strerror_r` to guest libc.
- Added explicit audit allowlist entries for the existing `env` VFS calls and
  versioned `waste_kernel` terminal/path/select calls already dispatched by
  the native and browser runtimes.
- The post-relink audit now has exactly 14 unknown imports, all long-double
  compiler ABI functions. No VFS or kernel import was broadly suppressed.

Implementation update (2026-09-24, long-double ABI closure):

- Added `src/html-rt/lib/quad.c` with binary128-compatible Wasm ABI helpers for
  conversion, comparison, arithmetic, and `frexpl`.
- Enabled the non-trapping-float-to-int feature when assembling and merging the
  guest libc image.
- The relinked `true` image now has zero unknown imports and no Asyncify
  symbols. The Coreutils gate passes, and all 14 guest-libc suites pass.
- The compatibility helpers use double arithmetic internally; a future
  precision-hardening stage must replace that approximation before claiming
  full long-double conformance.

### VM-D.7: Browser `/usr/bin/true` process-image execution (2026-09-24)

Status: complete

The generated Bash page stages the import-closed image at both `/usr/bin/true`
and `/bin/true`, and `start.sh --html-bash` builds
`build/coreutils/utility-probe/true-linked.wasm` before copying it into the
offline page. The focused browser harness reaches `execve` and observes the
expected zero exit status.

The failed run was caused by an ABI mismatch in the guest CRT, not by virtual
memory ownership or VFS loading. GNU Coreutils `true` defines the permitted
two-argument C entry point `main(int, char **)`, while the shared WASTE CRT had
declared and invoked `main` as a three-argument function. That produced a
three-argument `call_indirect` targeting Coreutils' two-argument table entry.
`waste-crt.c` now has an explicit `WASTE_MAIN_TWO_ARGS` mode, and the
Coreutils probe compiles the CRT in that mode; the existing three-argument
startup ABI remains available for guests that use it.

Validation:

- Coreutils public build/import gate: passes.
- `/usr/bin/true` and `/bin/true` browser VFS staging: reaches the loader.
- Browser execution of `true`: passes the local offline harness and emits
  `__C_ENGINE_COREUTILS_TRUE_STATUS_0__`.
- The harness reports all five C-engine Bash bootstrap checks passing and
  records an image return of `s7-e0` for the Coreutils process.

Follow-on status:

- VM-E mapping and protection work is now active below this handoff, with
  anonymous reservation, `PROT_NONE`, startup-region, and linear-memory
  promotion slices recorded there.
- Keep the Coreutils browser harness as a regression gate while the remaining
  explicit-region and process-checkpoint boundaries are reconciled.

## Stage VM-E: Implement anonymous mappings and protection

Status: complete for the current engine-owned region allocator; future POSIX
signal and independent-backing extensions remain outside this closure slice

Work:

- Add kernel-owned virtual-region allocation and collision checks.
- Implement anonymous private/shared `mmap`, partial/full `munmap`, and
  `mprotect`.
- Coordinate `brk`, guest allocator growth, startup storage, stack guards, and
  the mapping area.
- Add stable errno results for invalid flags, alignment, ranges, permissions,
  and address-space exhaustion.
- Define deterministic memory-fault records for future signal translation.

Gate:

- Anonymous private and shared mappings pass page-boundary, overlap,
  fork-inheritance, unmap-split, protection, and exhaustion tests.
- Unmapped and protected accesses fail without corrupting adjacent mappings.
- The offline browser uses no host shared-memory primitive or server feature.

Implementation update (2026-09-24, `PROT_NONE` mapping slice):

- Added an engine-owned `exec_memory_page_is_mapped` query based on VMA
  metadata rather than the protection byte. This keeps a mapped `PROT_NONE`
  page distinct from an unmapped page.
- Anonymous `mmap(PROT_NONE)` regions can now be created, remain reserved
  against later allocation and `MAP_FIXED_NOREPLACE`, and can be restored by
  `mprotect` without being mistaken for an unmapped range.
- The existing project address-placement contract is preserved: a nonzero
  address remains an exact placement request unless the later POSIX hint
  semantics slice explicitly changes that ABI.
- Added native ASan/UBSan coverage for mapping a `PROT_NONE` page, rejecting
  access to it, restoring read/write protection, and writing after restore.

Validation:

- `make -C src/cli-rt BUILD_DIR=../../build/cli-rt i32-smoke`: passed.
- `make -C src/cli-rt BUILD_DIR=../../build/cli-rt process-lifecycle`: 104
  checks passed.
- Offline browser `diy-posix-test/mmap.wast`: 23/23 assertions passed.

Remaining VM-E work is the allocator/address-space boundary: growable
engine-owned virtual page capacity, explicit reservation for module/stack/
startup/`brk` regions, and exhaustion tests that do not rely on a protection
byte as the allocation marker.

Implementation update (2026-09-24, virtual-page reservation slice):

- Added `exec_memory_reserve_virtual_pages`, which grows the engine page table
  up to the declared memory maximum while leaving new pages unmapped. This is
  separate from Wasm `memory.grow`, which still creates ordinary read/write
  module memory.
- Anonymous `mmap` can now consume newly reserved pages when no existing hole
  is available, and an exact nonzero placement can extend the virtual page
  table when it remains within the memory maximum.
- Added native sanitizer coverage for reserving pages, confirming that the
  reserved range is inaccessible until explicitly mapped, then mapping the
  range, and rejecting a reservation beyond the declared maximum without
  changing the current page count.

Validation:

- i32 ASan/UBSan smoke passed.
- C-engine process lifecycle sanitizer passed all 104 checks.
- Offline browser `diy-posix-test/mmap.wast` passed 23/23 assertions.

The remaining VM-E boundary is to separate module-visible linear-memory size
from the process virtual address-space reservation and assign explicit regions
for module data, stack, startup storage, `brk`, and mappings. Until that
separation is complete, this reservation slice stays bounded by the current
memory maximum and is not yet the final POSIX address-space allocator.

Implementation update (2026-09-24, startup-region guard):

- Process-image startup storage is now treated as an engine-owned reserved
  range for unmapping purposes. A guest `munmap` that overlaps the active
  `argc`/`argv`/`envp` block fails before page-table mutation, preserving the
  exec image's startup ABI.
- The guard is capsule-owned and follows the active process image; it does not
  use browser globals or host pointers. It remains intentionally narrow:
  future region allocation will place startup storage outside the module and
  `brk` regions instead of protecting an in-image range after the fact.

Validation:

- Native process lifecycle sanitizer: 104 checks passed.
- Offline browser `diy-posix-test/mmap.wast`: 23/23 assertions passed.
- `git diff --check` passed.

Implementation update (2026-09-24, linear-memory/growth boundary):

- Added a separate `linear_pages` count to engine memories. `pages` now tracks
  allocated page-table capacity, while `linear_pages` tracks the range visible
  to ordinary Wasm bounds checks and `memory.size`.
- `memory.grow` maps only the newly visible range and cannot silently consume
  an already mapped virtual region. A reservation can therefore exist beyond
  the current linear-memory end without changing the reported old size.
- Checkpoint snapshots preserve both counts. A successful `mmap` promotes the
  visible range through the end of the mapping so guest loads/stores can reach
  the mapped address, while intervening reserved pages remain governed by the
  engine page table.

Validation:

- i32 ASan/UBSan smoke passed.
- C-engine process lifecycle sanitizer: 104 checks passed.
- Offline browser `diy-posix-test/mmap.wast`: 23/23 assertions passed.

Implementation update (2026-09-24, explicit linear-memory promotion):

- Added `exec_memory_promote_linear_pages` so the loader/process layer records
  when a mapped virtual range becomes visible to guest Wasm memory. This keeps
  page-table reservation, mapping, and linear-memory visibility as separate
  engine operations.
- `mmap` now promotes only through the end of its successful mapping, and
  `memory.grow` maps subsequent pages without replacing an earlier mapping.
- Native coverage now reserves and maps a range, promotes it, grows again, and
  verifies that both the existing mapping and the newly grown page remain
  mapped.

Validation:

- i32 ASan/UBSan smoke passed.
- Offline browser `diy-posix-test/mmap.wast`: 23/23 assertions passed.
- `git diff --check` passed.

Implementation update (2026-09-24, process-region metadata slice):

- Added process-owned page-region records for module, stack, startup, `brk`,
  mapping, and guard roles. These records are separate from `exec_memory`'s
  linear-page count and VMA protection metadata.
- Region insertion rejects overflow and overlap, region membership reports the
  owning role, and forked capsules clone the region metadata with the process
  address-space state.
- The active startup block is now recorded as a startup region and the
  `munmap` path rejects ranges that overlap it through the region contract.
  The old image-based check remains as a compatibility guard during migration.
- The startup block remains inside the contiguous Wasm-visible range, but its
  occupied top pages are now explicitly split from the lower module region.
  This makes the module and startup ownership disjoint without inventing a
  second Wasm memory. Stack, `brk`, and mapping placement remain the next
  region-allocation slices.

Validation:

- Native process lifecycle sanitizer: 111 checks passed, including region
  overlap rejection and fork metadata preservation.
- Native i32 ASan/UBSan smoke passed.
- Browser engine build passed and every `core/*` official fixture passed.
  The six existing non-core `libc-test/*` `unknown module id` results remain
  outside this VM-E engine-region gate.
- No manual browser test is required for this metadata-only increment; the
  existing headless browser gate is sufficient.

Implementation update (2026-09-24, disjoint module/startup layout slice):

- Startup construction now uses `linear_pages` as the visible-memory bound,
  and the process-image commit records the lower pages as a module region and
  the occupied top pages as a startup region.
- The two regions cannot overlap, while startup pointers retain their existing
  Wasm ABI and remain valid to the guest. This is the process-region split
  needed before assigning independent `brk`, stack, and mapping ranges.
- This slice does not create a second Wasm memory or alter `memory.size`; it
  only gives the process address-space metadata distinct ownership for the
  already-visible pages.

Validation:

- Native exec transition matrix sanitizer: 57 checks passed.
- Native process lifecycle sanitizer: 111 checks passed.
- Native i32 ASan/UBSan smoke passed.
- `./start.sh --html-bash` rebuilt the browser image, Coreutils `true`, and
  both C-engine Bash smoke/continuation gates successfully.
- No manual browser test is required; the generated offline page was exercised
  by the headless browser harness.

Implementation update (2026-09-24, process layout allocator slice):

- Added a process virtual-page limit independent of the Wasm memory's current
  `linear_pages` value. Region allocation can now place ranges low-to-high or
  top-down while rejecting every recorded collision.
- A committed process image now reserves an initial `brk` page, a downward
  stack region, and a guard page in addition to its disjoint module/startup
  regions. These are address-space reservations only; page materialization
  remains owned by the engine mapping layer.
- Forked capsules preserve the process virtual-page limit and all layout
  regions, so future COW and mapping operations inherit the same address-space
  topology without sharing process metadata.

Validation:

- Native exec transition matrix sanitizer: 57 checks passed.
- Native process lifecycle sanitizer: 111 checks passed, including low and
  top-down region allocation.
- Native i32 ASan/UBSan smoke passed.
- `./start.sh --html-bash` rebuilt and headlessly passed the browser Bash smoke
  and process-continuation gates.
- No manual browser test is required for this metadata-only allocator slice.

Implementation update (2026-09-24, mapping allocator integration slice):

- Anonymous automatic `mmap` placement now consults process-region metadata,
  skips module/startup/`brk`/stack/guard reservations, and chooses only a free
  page interval within the engine's declared memory maximum.
- The selected interval is reserved as a mapping region after successful page
  materialization. Partial and full `munmap` remove or split mapping-region
  records without touching unrelated process regions.
- Exact mappings reject collisions with process-owned regions before changing
  the page table. Virtual-page growth remains bounded by the Wasm memory
  maximum until the independent process backing store is implemented.

Validation:

- Native process lifecycle sanitizer: 111 checks passed.
- Native i32 ASan/UBSan smoke passed.
- Browser `diy-posix-test/mmap.wast`: 23/23 assertions passed.
- Browser `core/*` official fixtures all passed. The six existing non-core
  `libc-test/*` `unknown module id` results remain outside this VM-E mapping
  gate.
- `git diff --check` and `bash -n start.sh` passed. No manual browser test is
  required; the headless harness exercised the mapping path.

Implementation update (2026-09-24, independent virtual-capacity slice):

- Added `exec_memory.virtual_max_pages`, distinct from the Wasm
  `max_pages`. Ordinary memories retain their declared Wasm limit; a
  process-bound memory receives the process address-space limit when its
  access validator is attached.
- Virtual-page reservation, instance cloning, and checkpoints now preserve
  this independent capacity. A process can therefore reserve and materialize
  page-table entries beyond the current Wasm maximum without changing
  `linear_pages` or `memory.size`.
- A mapping beyond the Wasm maximum is deliberately not promoted into the
  Wasm-visible linear range yet. It is available to the engine-owned process
  mapping layer; the process-aware guest address decoder must be completed
  before ordinary Wasm load/store instructions can reach such a mapping.

Validation:

- Native i32 ASan/UBSan smoke passed, including reservation beyond a Wasm
  maximum with unchanged `linear_pages`.
- Native process lifecycle sanitizer: 112 checks passed.
- Native store checkpoint sanitizer: 25 checks passed.
- Browser WAST engine rebuilt successfully. Existing browser `mmap.wast`
  and `core/*` headless results remain green for the VM gate.
- No manual browser test is required for this engine-capacity increment.

Implementation update (2026-09-24, process-aware guest access slice):

- Process-bound memories now use their engine page-table capacity for guest
  load/store bounds checks, while ordinary memories continue to use
  `linear_pages`. Protection and mappedness checks still reject reserved or
  unmapped pages.
- `memory.size` and `memory.grow` remain defined by the Wasm-visible linear
  range and declared Wasm maximum. A process mapping beyond that maximum is
  therefore reachable through the process image's guest pointer space without
  changing Wasm memory-size results.
- The process-virtual access mode is preserved by instance cloning and store
  checkpoints. This is the first complete decoder slice for engine-owned
  `mmap` pages beyond the module's Wasm maximum; signal/fault records remain a
  later POSIX boundary.

Validation:

- Native i32 ASan/UBSan smoke passed, including read/write access to a mapped
  page beyond `linear_pages` while an unmapped page remains protected.
- Native process lifecycle sanitizer: 112 checks passed.
- Native store checkpoint sanitizer: 25 checks passed.
- Native exec transition matrix sanitizer: 57 checks passed.
- Browser WAST and C-engine Bash artifacts rebuilt successfully, and the Bash
  headless smoke/continuation gates passed. No manual browser test is needed.

Implementation update (2026-09-24, deterministic memory-fault record slice):

- Added structured fault details to `exec_error`: out-of-range, unmapped, and
  protection fault kinds, the guest byte address, requested length, and the
  required read/write access.
- Engine memory reads, writes, and instruction-address checks now classify a
  failing access before returning `EXEC_ERROR_TRAP`. A reserved page remains
  distinct from an unmapped page, and a mapped page with insufficient
  permissions is reported as a protection fault.
- This is diagnostic metadata only. POSIX signal translation (`SIGSEGV`/
  `SIGBUS`) and process signal delivery remain a later boundary, including
  file-backed mapping faults in VM-F.

Validation:

- Native i32 ASan/UBSan smoke passed, including structured unmapped-page fault
  assertions.
- Native process lifecycle sanitizer: 112 checks passed.
- Native store checkpoint sanitizer: 25 checks passed.
- Native exec transition matrix sanitizer: 57 checks passed.
- Browser WAST engine rebuild passed.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required for this engine metadata slice; existing
  headless browser evidence covers the unchanged browser mapping behavior.

Implementation update (2026-09-24, mapping commit-order slice):

- Process `mmap` now reserves its mapping-region metadata before materializing
  pages. Fixed-no-replace collisions, page-map failures, and linear-range
  promotion failures remove that provisional region before returning.
- This prevents a failed mapping from leaving process-region metadata that
  claims ownership of pages which were never successfully mapped. The change
  also makes the fixed-collision path explicitly non-destructive.
- The operation remains bounded by the current cooperative scheduler: the
  engine does not yield between region reservation and page materialization.
  Preemptive transaction locking is not introduced until a concurrent
  scheduler exists.

Validation:

- Native process lifecycle sanitizer: 113 checks passed, including failed
  fixed-mapping metadata rollback.
- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 184 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the shared process mapping path was
  exercised by the headless harness.

Implementation update (2026-09-24, unmap commit-order slice):

- `munmap` now allocates and builds its replacement mapping-region metadata
  before removing pages. If metadata allocation or validation fails, the page
  map and process-region records are unchanged; if page removal fails, the
  prepared replacement is discarded without publishing it.
- Successful partial unmaps still split only mapping regions and preserve
  module, startup, `brk`, stack, and guard reservations. This completes the
  failure-side symmetry with the earlier atomic `mmap` commit-order slice.

Validation:

- Native process lifecycle sanitizer: 113 checks passed.
- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 184 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the shared mapping path was exercised by
  the headless harness.

Implementation update (2026-09-24, protection-range atomicity slice):

- The process `mprotect` boundary now has regression coverage proving that a
  range containing an unmapped or out-of-bounds page is rejected before any
  page protection changes are published.
- A previously read-only page remains read-only after the rejected range, and
  a subsequent valid one-page protection change still succeeds. This keeps
  process protection state aligned with the engine VMA state.

Validation:

- Native process lifecycle sanitizer: 113 checks passed.
- Existing offline C-engine browser mapping harness: 184 official fixtures
  passed with no failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; this increment exercises an already
  headless-covered process protection contract.

Implementation update (2026-09-24, file-mapping ownership cleanup slice):

- `munmap` now prepares and commits the process file-mapping ownership update
  together with the virtual-page and region update. Full removal and partial
  unmap splits retain the correct file-object references and advance the file
  offset on a surviving right-hand segment.
- If page removal fails, the prepared file-mapping replacement is discarded
  and the existing ownership records remain intact. This prevents stale
  truncated-file validation records after an unmap and preserves descriptor-
  independent file-object lifetime.
- The explicit `native_process_capsule_forget_file_mapping` API now uses the
  same retain-before-commit path, so direct cleanup and `munmap` have identical
  ownership semantics.

Validation:

- Native process lifecycle sanitizer: 113 checks passed.
- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 184 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the file-mapping path was exercised by
  the headless harness.

Implementation update (2026-09-24, file-ownership commit-order hardening):

- The `munmap` path now prepares file-mapping replacements before removing
  pages, then commits page, process-region, and file-object metadata together.
  Partial unmaps retain both surviving segments with balanced references and
  advance the right segment's file offset.
- The explicit file-mapping cleanup API uses the same retain-before-commit
  implementation. If page removal fails, only the prepared references are
  discarded and the existing ownership records remain unchanged.
- This closes the process-side handoff needed by VM-F: an unmapped file range
  cannot remain in the process's truncated-file validation set.

Validation:

- Native process lifecycle sanitizer: 113 checks passed.
- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 184 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the shared file-mapping path was
  exercised by the headless harness.

Implementation update (2026-09-24, single-owner unmap handoff):

- Removed the browser POSIX wrapper's redundant post-`munmap` file-mapping
  cleanup. The process VM operation is now the single owner of the page,
  region, and file-mapping ownership commit.
- This avoids a second metadata pass after a successful unmap and makes the
  native and browser paths use the same file-object reference transition.

Validation:

- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 185 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the browser mapping path was exercised
  by the headless harness.

Implementation update (2026-09-24, browser unmap ownership handoff):

- Removed the browser POSIX wrapper's second `forget_file_mapping` call after
  `native_process_capsule_munmap_range`. The process VM operation is now the
  single owner of the page, region, and file-mapping commit in both native and
  browser execution.
- This prevents duplicate ownership transitions after a successful unmap and
  keeps descriptor-close/unlink lifetime behavior on the same reference-counted
  file-object path.

Validation:

- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 185 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the browser file-mapping path was
  exercised by the headless harness.

Implementation update (2026-09-24, descriptor-close/unlink lifetime slice):

- Extended the native process mapping gate to close the source descriptor and
  unlink its pathname before using the retained file object through the shared
  page cache. The mapping and cache continue to resolve the same object after
  both namespace references are gone.
- This confirms that mapping lifetime is owned by retained engine file objects,
  not by an open descriptor or pathname, including the independent-process
  shared-page path.

Validation:

- Native process lifecycle sanitizer: 114 checks passed.
- Existing offline C-engine browser harness: 185 official fixtures passed with
  no failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; browser shared-file coverage already
  exercises the same retained-object path.

## Stage VM-F: Implement file-backed mappings and shared backing

Status: complete for current file-backed mapping and signal-termination
boundary; guest signal handlers remain a separate POSIX extension

Work:

- Add VFS-backed page objects keyed by open-file/object identity and file page
  offset rather than path text.
- Implement private copy-on-write and coherent shared file mappings.
- Track dirty pages and implement `msync`, unmap writeback, truncation checks,
  and explicit I/O errors.
- Define behavior for access past the mapped file's valid range and connect it
  to the future `SIGBUS` path.
- Preserve `MAP_FIXED_NOREPLACE`'s non-destructive collision semantics; keep
  destructive `MAP_FIXED` unsupported until a separate gate covers replacement
  atomically.

Gate:

- Two processes mapping the same file with `MAP_SHARED` observe each other's
  writes through shared backing pages.
- `MAP_PRIVATE` writes never modify the VFS file or another process.
- File growth, truncation, partial final pages, `msync`, descriptor closure,
  and unmap behavior have deterministic tests.
- Native and browser behavior agree without delegating state to the host OS.

### VM-F.4: Independent-process `MAP_SHARED` backing handoff (2026-09-24)

Status: complete for the shared-page cache and browser evidence probe

The existing file-backed path is sufficient for VFS object identity,
descriptor-visible growth and truncation, `msync`, unmap writeback, forked
process inheritance, and non-destructive fixed mappings. A forked child keeps
the parent's mapping page aliases, so shared writes already remain coherent in
that case.

The remaining VM-F boundary is different: two independently instantiated
process images that open the same VFS file currently populate separate page
objects from the same file bytes. Their mappings therefore share file
identity and writeback rules, but do not yet share dirty page contents before
`msync` or unmap. This is an architectural gap, not a WAST byte-stream or
browser-loader problem.

The kernel/store now owns a file-page cache keyed by the reference-counted
file-object identity and file-page offset. A `MAP_SHARED` mapping acquires the
cache page directly, so independently instantiated processes bind to the same
reference-counted engine page and see writes without waiting for `msync`.
`MAP_PRIVATE` mappings continue to receive independent pages and cannot dirty
the cache. Dirty tracking, truncation validation, `msync`, and unmap writeback
continue to operate through the shared page record. Cache entries retain both
the file object and page until the store is released, so their lifetime is not
coupled to one process image.

Do not close this stage by only adding a second file read or by comparing path
strings. That would preserve the current independent-process incoherence and
would also give the wrong behavior after unlink or descriptor duplication.

Current evidence:

- C-engine process lifecycle: 104 checks passed.
- Offline browser `mmap.wast`: 22/22 assertions passed.
- The existing coverage includes fork inheritance and shared writeback, and
  the cache is now compiled into both native and browser runtimes. The current
  `mmap.wast` fixture still does not exercise two independently instantiated
  processes mapping the same file before either process calls `msync`.
- The native process lifecycle sanitizer now passes 108 checks, including two
  separately allocated process memories bound to one cached file page and an
  immediate cross-memory write/read assertion.
- Browser evidence now includes the C-engine shared-file-page probe described
  in VM-F.5.

### VM-F.5: Browser evidence for independent shared file pages (2026-09-24)

Status: complete

The current browser harness has two deliberately different modes. WAST-stream
tests execute the C engine, while `browser-native` tests instantiate guest
modules against JavaScript-managed memory and descriptor objects. The latter
cannot validate the engine-owned file-page cache, and the current WAST process
fixture exercises fork inheritance rather than two independently instantiated
process images.

The offline C-engine harness now requests a narrowly scoped browser export
before the WAST stream runs. After VFS/store initialization, the probe creates
two independent engine memories, acquires one staged VFS file page through the
store cache, binds it to both memories, writes through one, and reads through
the other before `msync`. It then writes an independent private page and
confirms that the shared page is unchanged. The probe remains inside the
offline `file://` dashboard and does not use JavaScript-side POSIX state.

Validation:

- Native process lifecycle sanitizer: 108 checks passed.
- Offline browser `diy-posix-test/mmap.wast`: 24/24 passed, including the
  shared-file-page probe.
- `git diff --check` passed.

### VM-F.1: Truncation-aware mapping boundary (2026-09-24)

Status: implemented as an interim fault boundary

The VFS now provides `posix_kernel_ftruncate`. It resizes the reference-counted
file object, zero-fills growth, and makes shrink/grow visible through every
descriptor, forked kernel, and mapping that retains that object. The browser
POSIX resolver exposes the same operation as `ftruncate`.

`msync` checks the current file-object size before writing a shared mapping. If
truncation has made any part of that mapping extend beyond the valid file range,
the operation fails deterministically with `EIO`. This prevents stale mapped
bytes from being written back after truncation and establishes the error path
needed for the eventual signal implementation.

The executor now has a process-owned access validator on the ordinary
`exec_memory_read` and `exec_memory_write` paths. It checks file-mapping valid
ranges before a guest load/store, so an access into a truncated tail produces a
deterministic engine trap with the message `access past truncated file mapping`.
The mapping remains explicitly unmap-able after a failed `msync`; cleanup must
not depend on successful writeback. This is the engine fault boundary, not yet
the final POSIX signal result: the next slice must translate this fault into
the process's engine-owned `SIGBUS` disposition and preserve the signal frame
and return/termination semantics.

Coverage:

- `tests/diy-posix-test/mmap.wast` has a 21-assertion browser/native fixture
  covering a shared mapping truncated below its mapped length, the `EIO`
  result from `msync`, and a direct guest load trap from the invalid tail.
- The native VFS sanitizer gate now passes 311 checks, including shrink and
  zero-filled growth.
- The offline C-engine browser fixture passes all 21 assertions.

### VM-F.2: Route truncated mapping faults through process signals (2026-09-24)

Status: implemented for process termination

The engine access validator now annotates a truncated-file access trap with
`SIGBUS`. The process driver preserves ordinary WAST assertion behavior, but
when the same fault occurs while running a process handler or executable image,
the signal-aware completion path records signal termination as `128 + SIGBUS`
and stores the corresponding wait status. Handler cleanup and the process
transition to `EXITED` happen through the engine-owned process state; no host
signal is raised.

The signal disposition/handler-frame semantics are still intentionally
unfinished. The current path implements the default terminating disposition,
which is the required behavior for a Coreutils process with no custom
`SIGBUS` handler. A later slice must deliver an installed guest handler,
preserve the interrupted execution frame, apply the signal mask, and support
return from the handler before this becomes full POSIX signal compliance.

Validation:

- C-engine process lifecycle: 104 checks passed.
- i32 execution smoke test passed.
- Offline browser `mmap.wast`: 21/21 assertions passed.

### VM-F.2a: Structured truncated-file fault records (2026-09-24)

Status: implemented as the signal-translation input boundary

Truncated file-backed accesses now carry a distinct
`EXEC_MEMORY_FAULT_FILE_TRUNCATED` record in addition to their `SIGBUS`
annotation. The record preserves the guest address, requested byte length, and
read/write access that crossed the now-invalid file range. This distinguishes a
file-backed `SIGBUS` source from an ordinary out-of-range, unmapped, or
protection fault without changing default process termination behavior.

Guest signal-handler frames and `sigreturn` remain future work; this slice only
makes the engine-owned fault-to-signal handoff lossless.

Validation:

- Native process lifecycle sanitizer: 114 checks passed.
- Native i32 ASan/UBSan smoke passed.
- Browser WAST engine rebuilt successfully.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; this metadata-only signal boundary does
  not change browser-visible mapping behavior.

### VM-F.3: Non-destructive fixed mappings (2026-09-24)

Status: implemented

The memory ABI now accepts `MAP_FIXED_NOREPLACE`. A nonzero requested address
is treated as a fixed placement, and an occupied page range returns `EEXIST`
without replacing or unmapping the existing mapping. The engine mapping metadata
has a corresponding fixed-no-replace flag, so the collision check happens in
the process VM layer rather than in browser or host address-space behavior.
Destructive `MAP_FIXED` replacement remains unsupported as planned.

Validation:

- `tests/diy-posix-test/mmap.wast` now has 22 assertions, including the
  fixed-no-replace collision errno.
- Offline browser mapping fixture: 22/22 assertions passed.
- i32 smoke, native POSIX kernel (322 checks), and store checkpoint (25 checks)
  all passed.

## Stage VM-G: Add POSIX shared-memory objects

Status: complete for the current store-wide shared-memory and cooperative
transaction boundary; preemptive scheduling extensions remain separate

Work:

- Implement the required `shm_open`, `shm_unlink`, `ftruncate`, and mapping
  interactions in the engine kernel namespace.
- Treat names as kernel objects with permissions and lifetimes, not VFS host
  paths unless the architecture explicitly mounts a guest-visible namespace.
- Reuse the same backing-page and descriptor machinery as file mappings.
- Define process exit, unlink-while-open, fork, and exec inheritance behavior.

Gate:

- Independent processes can map the same named object and observe coherent
  writes.
- Unlink removes the name while existing descriptors and mappings remain
  valid until their final references are released.
- Unsupported synchronization semantics fail explicitly.

### VM-G.1: Named shared-memory object lifetime (2026-09-24)

Status: implemented for one kernel namespace and fork inheritance

The kernel now exposes `posix_kernel_shm_open` and
`posix_kernel_shm_unlink`, with browser resolver entries for `shm_open` and
`shm_unlink`. Names require the POSIX leading slash and a single component,
and are stored in a kernel-private reserved namespace rather than colliding
with ordinary guest pathname lookups. The object uses the existing
reference-counted file-object backing, so `ftruncate`, `mmap`, `msync`, and
descriptor I/O reuse the established machinery.

Unlink removes the name while open descriptors and mappings retain the object.
Forked kernels retain the object and see its contents through their inherited
descriptors. This is deliberately the first VM-G slice: independently created
processes currently have independent kernel namespaces, so the next slice must
move named-object ownership to a store/engine shared registry before claiming
full POSIX `shm_open` behavior.

Validation:

- Native POSIX kernel sanitizer: 317 checks passed, including create, size,
  write, unlink, descriptor lifetime, and fork inheritance.
- The existing browser memory-mapping fixture remains green after adding the
  resolver entries.

### VM-G.2: Store-wide shared-memory namespace (2026-09-24)

Status: implemented for store-attached kernels

Named objects now live in a reference-counted `posix_shm_namespace` rather
than only in one kernel's pathname table. `native_store` owns the namespace;
its process kernel and replacement terminal kernel attach to it, and forked or
independently created kernels can attach to the same registry explicitly.
`shm_open` materializes a per-kernel hidden pathname entry that points at the
registry's file object, preserving the existing descriptor and mapping code.
Unlink removes the registry name but does not invalidate hidden entries held by
open descriptors or mappings. Recreating the name creates a new object rather
than resurrecting the unlinked one.

This closes the namespace-visibility gap for processes in one engine store.
The remaining POSIX work is permissions and `O_EXCL`/creation-race semantics,
store checkpoint serialization of the namespace, and synchronization beyond
the current coherent file-object bytes.

Validation:

- Native POSIX kernel sanitizer: 319 checks passed, including two independent
  kernels opening the same named object, unlink lifetime, and fork retention.
- Process lifecycle sanitizer: 104 checks passed.
- Browser mapping regression remains green after the namespace integration.

### VM-G.3: Exclusive named-object creation (2026-09-24)

Status: implemented

`POSIX_O_EXCL` is now accepted by the kernel open flags and enforced for
`shm_open` when combined with `POSIX_O_CREAT`. An existing name returns
`EEXIST`, including when the caller is an independently attached kernel. After
`shm_unlink`, the same name may be created again and receives a new backing
file object; existing descriptors continue to reference the old object.

Validation:

- Native POSIX kernel sanitizer: 320 checks passed, including exclusive
  creation failure.
- The browser runtime recompiles with the shared ABI change and retains the
  existing offline mapping coverage.

At the VM-G.3 boundary, the remaining work was permission enforcement,
checkpoint serialization of the store namespace, and the complete creation-race
model once the engine has an explicit scheduler for concurrent namespace
operations; permission enforcement is covered by VM-G.4 below.

### VM-G.4: Shared-memory permission checks (2026-09-24)

Status: implemented for kernel credentials

Named objects now retain creator mode, UID, and GID metadata. `shm_open`
checks the requested read/write access against owner, group, or other mode bits
for non-root kernels and returns `EACCES` when access is insufficient. UID 0
retains the kernel's root override. Credentials are copied by kernel cloning,
and the explicit kernel credential setter gives process setup a stable hook for
future `setuid`/`setgid` integration.

Validation:

- Native POSIX kernel sanitizer: 322 checks passed, including denial for an
  unrelated user and successful root access.
- Browser runtime rebuild and offline `mmap.wast` regression: 21/21 passed.

At the VM-G.4 boundary, remaining work was connecting credentials to the
complete process identity, serializing namespace objects and credentials
through checkpoints, and finishing scheduler-level atomicity rules for
concurrent create/unlink operations; checkpoint registry persistence is covered
by VM-G.5 below.

### VM-G.5: Checkpoint shared-memory namespace (2026-09-24)

Status: implemented for named-object registry and credentials

Store checkpoints now clone the store-wide shared-memory namespace, including
object names, mode/UID/GID metadata, inode identity, and file bytes. Restore
attaches the restored namespace to every process kernel and restores each
kernel's UID/GID credentials. The checkpoint owns its namespace copy, so later
mutations of the live registry do not modify the captured state.

This slice intentionally snapshots the named-object registry rather than the
entire POSIX descriptor table. Descriptors that were already open retain their
existing file objects, while new `shm_open` calls after restore resolve through
the restored registry. Full descriptor/mapping serialization and alias
topology preservation remain part of the later process-checkpoint work.

Validation:

- Store checkpoint sanitizer: 25 checks passed.
- Native POSIX kernel sanitizer: 322 checks passed.
- Browser build and offline `mmap.wast`: 21/21 assertions passed.

### VM-G.6: Browser named shared-memory coverage (2026-09-24)

Status: implemented for the current browser shared-memory ABI

The browser POSIX resolver now has an end-to-end named-object fixture covering
`shm_open(O_CREAT)`, `ftruncate`, shared `mmap`, guest writes, `msync`, and
`shm_unlink`. The fixture rewrites its pathname at the point of use because
the preceding mapping cases intentionally reuse guest scratch memory; this
keeps the test independent of execution order while preserving the intended
VFS-backed byte path.

The browser process driver also binds the engine memory access validator when
it selects or replaces a process capsule. This keeps truncated file mappings
consistent between native execution and the browser path, including the
SIGBUS metadata used by the process completion handler.

Validation:

- Offline browser `mmap.wast`: 23/23 assertions passed, including named shared
  memory.
- Native POSIX kernel and checkpoint gates remain passing at 322 and 25 checks,
  respectively.

This closes the browser test slice of VM-G. Full shared-memory completion still
depends on the later process-checkpoint work for descriptor/mapping topology
and on scheduler-level atomicity for concurrent create/unlink operations.

### VM-G.7: Checkpoint process mapping topology (2026-09-24)

Status: implemented for process-owned mapping and region metadata

Store checkpoints now snapshot and restore each live process capsule's
file-mapping ownership records, process-region reservations, and independent
virtual-page limit. File-object references are retained by the checkpoint, so
restoring after a mapping split or metadata mutation cannot leave a dangling
mapping record or lose the file identity used by truncation and `SIGBUS`
validation. Restore replaces the capsule metadata during the same restore
operation, before the engine memory graph is restored.

This slice deliberately does not serialize the complete POSIX descriptor table,
continuations, or scheduler state. Open descriptors remain a separate
process-checkpoint boundary; the new snapshot only makes the already
engine-owned memory/VMA topology agree with the process capsule after restore.

Validation:

- Store checkpoint sanitizer: 30 checks passed, including process mapping and
  region metadata restore.
- Process lifecycle sanitizer: 112 checks passed.
- Browser WAST engine rebuild passed.
- Offline C-engine browser harness: 190 official fixtures passed, with no
  failures.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; this is native checkpoint metadata and
  the browser artifact compilation gate is sufficient.

### VM-G.8: Atomic named-object publication (2026-09-24)

Status: implemented for the cooperative scheduler boundary

Named shared-memory creation now rolls back the namespace entry when the
creator fails its requested permission check. A failed `shm_open(O_CREAT)` no
longer publishes a name that a later process can open. The mutation remains a
single non-yielding kernel operation in the current cooperative scheduler, so
there is no observable create/unlink interleave inside one call. Existing
descriptors and mappings continue to retain their file object after unlink.

True preemptive concurrent namespace transactions are intentionally not
claimed here; they require the future scheduler's operation-lock or
compare-and-commit mechanism. This slice closes the current engine's partial
publication bug and defines the handoff point for that scheduler work.

Validation:

- Native POSIX kernel sanitizer: 324 tests passed, including failed-create
  rollback and the existing unlink/lifetime cases.
- Browser WAST engine rebuild passed after compiling the same kernel path.
- `git diff --check` and `bash -n start.sh` passed.
- No manual browser test is required; the browser artifact rebuild covers this
  kernel-only change.

## Stage VM-H: Resume Coreutils and lock regression gates

Status: handoff validation complete for `true`; later utility waves belong to Stage 8F

Work:

- Return to Stage 8F and build Coreutils applications with imported process
  memory rather than an application-owned allocator object.
- Instantiate `true` with its process-local libc, close its import audit, and
  verify direct and PATH-based execution from Bash.
- Continue the Coreutils utility waves only after the module-shared memory and
  process-isolation gates pass.
- Add page allocation, copy-on-write, mapping, and translation-cache counters
  behind `WASTE_PROFILE`.
- Consolidate durable rules into `docs/architecture.md`,
  `docs/techniques.md`, and `docs/wasm32-abi.md` after implementation proves
  them.

Gate:

- `/usr/bin/true` and `/bin/true` return status 0 in the offline browser and
  Bash reaches a later prompt.
- Repeated execution does not share private libc state between processes.
- Import audits, native sanitizers, official Wasm tests, focused VM tests,
  browser runtime tests, Python/shell/Node syntax checks, and
  `git diff --check` pass.

### VM-H.1: Reopen the Coreutils `true` gate (2026-09-24)

Status: implemented for the first utility

The VM-F shared-page work and the browser runtime merge boundary now allow the
Coreutils `true` artifact to resume. The reproducible `build-coreutils` path
configures the staged Coreutils source, links `true` as Wasm, and reports an
empty undefined-symbol/import-audit result. The Bash relink path now enables
the same non-trapping float-to-int Wasm feature already used by the Coreutils
libc linker, so the integrated guest libc validates during the merge.

The offline C-engine Bash probe stages the linked artifact as both
`/usr/bin/true` and `/bin/true`, invokes `true` through Bash PATH resolution,
and observes `__C_ENGINE_COREUTILS_TRUE_STATUS_0__`. The complete probe passed
5/5 checks, including the child exec/wake transition and final shell exit.

Validation:

- `make -C src/html-rt BUILD_DIR=../../build/html-rt build-coreutils` passed.
- `./start.sh --html-bash` passed, including the ordinary Bash and continuation
  browser smoke gates.
- `node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html
  --coreutils-true` passed 5/5.
- `git diff --check` passed.

The remaining Coreutils utility waves are not VM-plan work. They continue in
Stage 8F after this plan's closure pass, using the validated process-memory and
loader handoff.

### VM-H.2: Compile and audit the `false` utility (2026-09-24)

Status: implemented as the second utility process-image gate

The Coreutils relinker is now utility-named instead of being hard-coded to
`true`. The normal `build-coreutils` target builds and validates both utilities
without weakening the import allowlist. `false` produces
`build/coreutils/utility-probe/false-linked.wasm`, with no undefined symbols,
unknown imports, or Asyncify symbols in its report.

The browser Bash page now stages both linked images at `/usr/bin` and `/bin`,
and the focused runtime harness executes `/bin/false` through Bash PATH and
checks that the child returns status 1. This is intentionally a separate
gate: successful compilation and import closure do not prove that a utility's
`_start` and process exit status are correct in the browser. The paired `true`
gate remains in place to ensure adding another process image does not regress
the original zero-status path.

Validation:

- `make -C src/html-rt BUILD_DIR=../../build/html-rt build-coreutils` passed
  for both `true` and `false`.
- `./start.sh --html-bash` passed and rebuilt the offline page with both VFS
  images.
- `node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html
  --coreutils-false` passed 5/5 with status 1.
- `node tests/c-engine-bash-browser-runtime.cjs build/html-rt/bash.html
  --coreutils-true` passed 5/5 with status 0.
- `git diff --check`, `bash -n start.sh`, and Python/Node syntax checks passed.

No manual browser test is required for this implementation slice; the
headless browser-equivalent worker harness exercises the generated offline
page and both process images. Opening `build/html-rt/bash.html` manually is
optional visual confirmation only.

### VM-CLOSURE.1: Publish the virtual-memory ownership contract (2026-09-24)

Status: implemented as the documentation closure boundary

The durable architecture, C-engine techniques, and wasm32 ABI documents now
state the process-capsule ownership model used by the implementation:
virtual regions and page mappings belong to the engine, threads share a
process address-space graph, independent processes remain isolated, and
explicit shared mappings retain backing-page identity. They also record the
transactional mapping/`execve` commit rule, the distinction between reserved,
`PROT_NONE`, unmapped, and truncated-file states, and the fact that structured
memory faults are inputs to signal translation rather than host signals.

This closes the documentation portion of the VM closure pass. It does not
claim that guest signal-handler frames, `sigreturn`, or preemptive scheduler
transactions are complete; those remain explicit future boundaries and are
not required for the current Coreutils process-image handoff.

Validation:

- `./start.sh --html-bash` and the paired `true`/`false` browser process-image
  gates remain passing.
- `git diff --check`, `bash -n start.sh`, and Python/Node syntax checks pass.

### VM-CLOSURE.2: Reconcile the VM-E explicit-region boundary (2026-09-24)

Status: complete for the current process-memory contract

VM-E now has one documented acceptance boundary for the engine-owned region
allocator. A committed process image records disjoint module and startup
regions, reserves `brk`, downward stack, and guard ranges, and allocates
automatic mappings only in collision-free mapping regions. Exact mappings,
partial `munmap`, protection changes, page promotion, virtual-capacity
extension, fork cloning, and checkpoints preserve or roll back region metadata
atomically with the corresponding page operation.

This closes the explicit-region work required by the current Coreutils image
handoff. It does not claim that the address-space implementation is a host
kernel replacement: future work still includes complete POSIX signal-handler
delivery, preemptive scheduler transactions, and any independent backing
store needed beyond the current process virtual-capacity model.

Validation:

- Native i32 ASan/UBSan smoke passed.
- Native process lifecycle sanitizer: 114 checks passed.
- Native store checkpoint sanitizer: 30 checks passed.
- Native POSIX kernel sanitizer: 324 checks passed.
- Browser C-engine artifact rebuilt successfully; existing offline mapping and
  Coreutils browser gates remain the regression evidence for this boundary.
- `git diff --check`, `bash -n start.sh`, and Python/Node syntax checks pass.

The full official browser dashboard remains a long-running regression suite;
the closure decision here is based on the focused native region/checkpoint
gates and the already-recorded offline mapping coverage, not on shortening
that suite or treating an interrupted run as a pass.

### VM-CLOSURE.3: Reconcile VM-F and VM-G aggregate status (2026-09-24)

Status: complete for the current engine and cooperative-kernel contract

VM-F now has one aggregate boundary covering regular-file mappings, shared
file-page identity across independently instantiated process memories,
private copy-on-write behavior, descriptor-independent file lifetime,
truncation-aware `SIGBUS` termination metadata, fixed-no-replace mappings,
writeback, and checkpointed mapping ownership. The remaining guest signal
handler frame/`sigreturn` work is a POSIX signal extension, not a blocker for
the current Coreutils process-image handoff.

VM-G now has one aggregate boundary covering store-wide named shared-memory
objects, permissions, exclusive creation, unlink lifetime, fork and
independent-kernel visibility, checkpointed namespace metadata, browser
resolver coverage, and cooperative failed-publication rollback. Full
descriptor-table checkpointing and atomic transactions under a future
preemptive scheduler remain explicit follow-up work.

Validation:

- Native process lifecycle sanitizer: 114 checks passed.
- Native store checkpoint sanitizer: 30 checks passed.
- Native POSIX kernel sanitizer: 324 checks passed.
- The recorded offline browser mapping/shared-memory gates remain passing,
  including 24/24 mapping assertions and 23/23 named-memory assertions.
- `git diff --check`, `bash -n start.sh`, and Python/Node syntax checks pass.

No manual browser test is required for this reconciliation; it changes plan
status and acceptance wording only. The focused browser gates already cover
the implemented file-mapping and named-memory paths.

## Required test matrix

Every stage must select the relevant rows rather than postponing all browser
coverage to the end:

| Area | Native | Browser | Differential/oracle |
| --- | --- | --- | --- |
| Scalar, SIMD, bulk, grow, memory64 | ASan/UBSan | C dashboard | Official OCaml interpreter |
| Imported-memory aliasing | Focused module fixtures | C dashboard | Official linking tests |
| Fork and copy-on-write | Kernel fixture | Bash/process fixture | OCaml POSIX behavior where available |
| Anonymous and file mappings | Kernel/libc fixture | Offline browser fixture | POSIX contract fixtures |
| Checkpoint topology | Store fixture | Fork/exec fixture | Pre/post state invariants |
| Coreutils process image | CLI/import audit | Bash page | Pinned native GNU behavior |

Tests must include zero-length and overflow failures, first/last byte of a
page, every supported width crossing a page boundary, overlapping copies in
both directions, growth failure, protection changes, mapping splits, shared
and private fork behavior, and cleanup after partial construction failures.

## Completion criteria

This plan is complete when:

- no production engine or kernel path depends on contiguous guest memory;
- each process owns a private virtual address space and threads share it;
- module instances inside one process can alias one memory for normal C
  pointers;
- `fork` uses correct private copy-on-write and preserves explicit shared
  mappings;
- anonymous and regular-file `mmap`, `munmap`, `mprotect`, and `msync` pass
  native and offline-browser tests;
- checkpoints preserve page and alias topology;
- shared-memory objects use the same kernel-owned backing model;
- official WebAssembly memory behavior remains conformant; and
- Stage 8F can resume without embedding a second allocator or memory in each
  Coreutils executable.

## Closure record (2026-09-24)

This plan is complete for the engine-owned virtual-memory contract and is
ready to be archived or removed after check-in. The final evidence is:

- Native i32 ASan/UBSan smoke passed.
- Native process lifecycle sanitizer: 114 checks passed.
- Native store checkpoint sanitizer: 30 checks passed.
- Native POSIX kernel sanitizer: 324 checks passed.
- The full offline C-engine browser run passed all `core/*`, `custom/*`, and
  `diy-posix-test/*` fixtures, including `diy-posix-test/mmap.wast`.
- The focused browser mapping/shared-memory evidence passed, including the
  recorded 24/24 mapping and 23/23 named-memory assertions.
- Coreutils `true` and `false` process-image browser gates passed 5/5 each.
- `git diff --check`, `bash -n start.sh`, Python syntax checks, and Node syntax
  checks passed.

The browser aggregate still reports six pre-existing `unknown module id`
failures in `libc-test/entropy-messages.wast`,
`libc-test/environment-boundaries.wast`, `libc-test/path-runtime.wast`,
`libc-test/select-runtime.wast`, `libc-test/terminal.wast`, and
`libc-test/time-resource.wast`. Those fixtures require libc modules outside
the VM closure scope; they are recorded rather than counted as VM failures.

Deferred POSIX extensions remain tracked outside this closed plan: guest
signal-handler frames and `sigreturn`, complete descriptor-table checkpoint
serialization, and preemptive scheduler transactions. Later Coreutils utility
waves remain owned by Stage 8F.
