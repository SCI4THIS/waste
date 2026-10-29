# Embedding command execution

The embedding interface runs binary guest commands through the shared C process
driver and retains results until explicit session destruction. The C session
contract and Wasm host ABI below are version 1.0; distribution artifact versions
are separate. Implementation and acceptance evidence are recorded in
[active-embedding.md](active-embedding.md). This work does not publish an
external release or add the follow-on `wat.wasm`/`wast.wasm` profiles.

## Build and exercise

```sh
./start.sh --embedding             # runtime, selected package, offline example
./start.sh --embedding-check       # native API + Wasm/worker/package gates
./start.sh --embedding-browser     # above plus actual file:// Chromium gate
make -C src/cli-rt -j4 embedding-check
make -C src/html-rt -j4 embedding
node src/html-rt/test/embedding.mjs
# Exact native/browser DOT file parity, after the native gate above:
node src/html-rt/test/embedding.mjs --native-svg=build/engine/logs/embedding-native.svg
```

The native gate uses warnings-as-errors and ASan/UBSan. The Wasm target builds
`build/html-rt/wasm.wasm` and copies `waste.mjs` and `waste.worker.mjs` beside it. The command artifact
does not link the WAT parser, WAST runner/assertion buffers, browser shell API,
terminal renderer, installed corpus, or guest executables. Guest bytecode is
interpreted; the outer Wasm artifact runs in the host WebAssembly implementation.

Minimum host tools depend on the selected target:

| Target | Host tools |
| --- | --- |
| Native C library/example | C11 compiler, make, ar, host math library, AWK, Flex and Bison for the existing optional text frontend |
| Standalone `embedding` | Clang with wasm32 support, wasm-ld, make, AWK and Python 3; no Flex/Bison dependency |
| `embedding-package` | Above plus Node.js with modern WebAssembly/BigInt support; installed selected guest files and notices |
| `embedding-check` | Native sanitizer support plus Node.js workers; guest assets already installed |
| `embedding-browser` | Above plus Chromium, selected through `WASTE_CHROMIUM` when needed |

The Makefiles can use the existing linker staged under `build/engine/toolchain`
when `wasm-ld` is absent from PATH. Packaging and browser use require no upstream
source checkout; explicit guest builds and corresponding-source generation have
the package-specific prerequisites documented under `src/aux/NAME/README.md`.

The retained C consumer at `src/cli-rt/test/embedding.c` includes only the public
engine header and ordinary host C headers. Its lifecycle is:

1. Initialize `waste_session_options`, provide a monotonic clock, and create.
2. Populate files/directories and configure cwd and stdin.
3. Start the executable with literal arguments and environment entries.
4. Step/resume, supplying input or waiting for a deadline as requested.
5. Inspect status, copy streams and output files, and delete explicitly.

Native platform callbacks supply clocks and optional entropy. C session code
never opens host files, sleeps, launches a GUI, or makes network requests.

The minimal native consumer in `src/cli-rt/examples/embedding.c` also includes
only the public header. Build and run it from the repository root:

```sh
make -C src/cli-rt embedding-example
printf 'one two\nthree four\n' | build/cli-rt/embedding-example \
  src/vfs/lib/libc.so.wasm src/vfs/usr/bin/awk /usr/bin/awk '{print $1}'
printf 'digraph { a -> b }' | build/cli-rt/embedding-example \
  --collect /work/graph.svg src/vfs/lib/libc.so.wasm \
  src/vfs/usr/bin/dot /usr/bin/dot -Tsvg -o /work/graph.svg > build/graph.svg
```

The consumer supplies host file bytes explicitly, closes stdin after EOF, pumps
the session, copies separate streams and the selected file, then deletes. Its
10-second deadline is an example policy, not an implicit engine requirement.

`make -C src/cli-rt embedding-native-package` produces a separate native package
under `build/cli-rt/embedding/`: `libwaste.a`, the public header, this consumer,
the guide and MIT license. It contains the shared engine, including optional
text support, and no native TTY/VFS/syscall frontend. Link a separate consumer
against the archive and the host math library. From the repository root:

```sh
cc -std=c11 -I build/cli-rt/embedding \
  build/cli-rt/embedding/examples/embedding.c \
  build/cli-rt/embedding/libwaste.a -lm -o build/cli-rt/external-embedding-example
```

The archive targets the host architecture/compiler ABI used to build it; the
versioned native C contract does not make a static archive architecture-neutral.

## JavaScript example

```js
import {createWaste} from "./waste.mjs";

// Obtain these bytes through the enclosing application's asset mechanism.
const runtime = await createWaste({
  engine: engineBytes,
  files: {
    "/lib/libc.so.wasm": libcBytes,
    "/usr/bin/awk": awkBytes,
    "/usr/bin/dot": dotBytes
  }
});

const text = await runtime.run({
  executable: "/usr/bin/awk",
  args: ["{ print $1 }"],
  stdin: new TextEncoder().encode("one two\nthree four\n")
});
console.log(text.exitCode, new TextDecoder().decode(text.stdout));

const graph = await runtime.run({
  executable: "/usr/bin/dot",
  args: ["-Tsvg", "-o", "/work/graph.svg"],
  stdin: "digraph { a -> b }",
  outputPaths: ["/work/graph.svg"]
});
const svgBytes = graph.outputFiles["/work/graph.svg"];
runtime.dispose();
```

`engine` accepts byte buffers or a compiled `WebAssembly.Module`. Files accept
byte buffers or `{bytes, mode}` records; templates are copied at runtime creation.
Each `run` gets a new isolated C session. Per-run files overlay the template;
the adapter creates missing parent directories. Default modes are 0755 under
`/usr/bin` and `/bin`, and 0644 otherwise. Missing cwd is an error; its default
is `/work`. C callers create parents explicitly.

Arguments exclude argv[0], which is the executable path. They are passed
literally, without shell expansion. Paths are canonical absolute UTF-8 strings:
no NULs, backslashes, empty components, `.` or `..`, trailing slashes other than
root, or control bytes. Argument/environment strings may be empty where allowed
by their respective APIs but cannot contain NUL. The guest ABI bounds vectors
to 127 total arguments, 255 environment entries and 128 KiB of string storage.

An omitted environment uses `HOME=/work`, `PATH=/usr/bin:/bin`, `LC_ALL=C`, and
`PWD` matching cwd. An explicit object or vector replaces this environment;
an empty object/vector gives an empty environment. Entries must be `NAME=VALUE`.
No host environment is inherited.

Stdin accepts text, bytes, an async iterable, or a readable stream. Finite input
automatically ends with EOF; streaming input ends when its iterator completes.
Output is raw bytes with independent stdout/stderr buffers, including NULs and
unchanged newlines. No descriptors are TTYs in this profile. `signal` accepts
an `AbortSignal`; cooperative pump yields allow cancellation during CPU work.

Results contain `outcome`, `exitCode`, `signal`, `message`, stdout/stderr bytes,
`outputFiles`, per-path `outputErrors`, `elapsedNs`, and observed outer-engine
linear-memory bytes. `resources` reports `memoryPagesPeak`,
`filesystemBytesPeak`, `processesPeak`, and `instructions` (a BigInt).
Outcomes distinguish `exited`, `signaled`, `trapped`,
`failed`, `cancelled`, `timed_out`, and `limit`. `exitCode` is meaningful only
for `exited`; an ordinary nonzero exit is a result, not a rejected promise.
Invalid host requests reject with an error; guest setup failures return `failed`.

Requested output paths are copied after completion, including partial files
after failure. A missing output has a per-path error instead of an invented
empty file. Aggregate collected file bytes are bounded by `maxOutputBytes`.
Returned arrays are independent copies and survive subsequent runs/disposal.
The default JS adapter serializes runs per runtime; it rejects concurrent runs
and disposal during an active run. It is not a reentrant or thread-safe C API.

The embedding tests record raw/compressed artifact size, host imports, initial
outer linear memory and the observed peak after each example under
`build/engine/logs/embedding-browser-results.json`. Initial measured test budgets
are 1 MiB raw, 256 KiB gzip, and 128 MiB outer memory per example. These budgets
detect packaging regressions; they do not substitute for guest resource quotas.
The native API gate writes its DOT SVG for an exact byte comparison with the
browser interpreter; `--native-svg` makes that comparison mandatory.

## Ownership and current limits

The public C declarations live in `src/engine/include/waste.h`. Create copies
options; callback functions/context remain borrowed until deletion. File/input
population copies bytes, and startup copies vectors synchronously. Inspection
copies a result record; output/file reads copy caller-selected ranges. No public
method returns a borrowed guest-memory pointer or exposes a store/kernel.
Delete takes a pointer to the opaque session pointer and sets it to NULL, making
repeated deletion through that pointer safe.

File population and cwd changes require `CONFIGURING`. Inspection/listing/file
reads require `CONFIGURING` or a terminal state; stream copies are also allowed
during execution. Start transitions to `RUNNABLE`; step returns a wait or terminal
result. A terminal session cannot be started or stepped again. Cancellation is
idempotent. Completion freezes execution of remaining processes while retaining
their state for destruction and root filesystem inspection.

Limits are supplied at runtime creation and may be overridden per run:

| JS option | Default | Bounds and accounting |
| --- | --- | --- |
| `maxInputBytes` | 16 MiB | 1..64 MiB; all stdin supplied during the run |
| `maxOutputBytes` | 16 MiB | 1..64 MiB; combined captured streams; also the separate aggregate file-collection bound |
| `timeoutMs` | 0 | 0 disables; otherwise up to one hour, starting at command startup |
| `quantumMs` | 16 | 1..60000; cooperative host-event pump |
| `maxMemoryPages` | 2048 | 1..32768; resident 64 KiB guest backing pages across the process tree |
| `maxProcesses` | 16 | 1..16; allocated process slots, including unreaped children |
| `maxFilesystemBytes` | 128 MiB | 1..128 MiB; retained writable file/symlink backing bytes, including supplied assets |
| `maxInstructions` | 0n | 0 disables the quota; otherwise up to u64 max, shared dispatched instruction count |

Stream capture rejects a write that
would exceed the output cap and completes with `limit`; it does not silently
truncate the write. Host input exceeding its configured bound is rejected before
it is copied. A configuration-time file quota rejection preserves the prior
file and permits another valid population call. Runtime quota exhaustion is
terminal and retains partial output/files. Supplied monotonic time is clamped
against backward movement.

Wait records expose input, timer or pump waits and a relative nanosecond deadline.
`UINT64_MAX` means no deadline. Input waits may also have an execution timeout;
the host must service either input or that deadline. The public step boundary
resumes existing engine-owned continuations through ordinary returns.

Shared pages and file objects are charged once while retained; copy-on-write
pages consume new charges. Closing one alias, unlinking an open file, or cloning
a kernel does not release a backing object's charge early. Allocation, file
resize, fork and dispatch checks run before exceeding caps, and share the same
session budget across providers, exec and fork. Sparse address-space size,
decoded instructions, engine tables/GC bookkeeping and JS copies are outside
the resident-page quota. Quotas bound guest resources rather than total host
heap use. Artifact memory budgets and the outer Wasm maximum remain separate.
Deadlines cannot preempt a blocking trusted host callback.

Text-interpreter child commands are unavailable in the binary profile, even
when Bash is supplied. The command profile removes text handling only at the shared loader's optional
WAT/WAST branches. Binary decode/validation, dynamic library startup, signals,
fork/exec, waits and continuation scheduling remain shared. Its opcode generator
builds independently of Flex/Bison. Runtime handle/transfer registries and the
browser allocator remain per outer Wasm instance. Native CLI and browser WAST
consumers use the same opaque session's store/driver ownership through the
private `session/adapter.h` bridge. This preserves their terminal, typed WAST
results and explicit host-service callbacks; the batch public API exposes none
of those implementation pointers. Normal browser shell startup directly launches
Bash through that session, while legacy WAST exports retain compatibility.

## Wasm host ABI 1.0

All exported function names start with `waste_command_`. Scalars are i32 unless
specified otherwise. Records are little-endian and encoded field by field; the
adapter never serializes a native C struct. All caller buffer spans must belong
to a live allocation made with `alloc`. A subrange of an allocation is allowed;
overflowing, freed and unregistered spans are rejected before dereferencing.
JavaScript must reacquire views after allocation or any call that may grow memory.

`version()` returns `major << 16 | minor`; `features()` currently returns 15:
streams=1, files=2, cancellation=4, resource limits=8.
`options_size()`, `result_size()` and `file_info_size()` return 64, 328 and 32.
`create(major, minor, requiredFeatures,
optionsPtr, optionsLen)` rejects unsupported versions/features before starting
anything. It returns a nonzero integer session handle or zero on error. Handle
slots use generation counters; stale/destroyed handles are rejected, and slots
are retired rather than wrapping a generation. The current instance supports
16 live sessions and 128 live transfer allocations.

| Options record offset | Field |
| --- | --- |
| 0 | u32 record size, currently 64 |
| 4 | u32 maximum input bytes |
| 8 | u32 maximum captured output bytes |
| 12 | u32 timeout milliseconds; zero disables |
| 16 | u32 pump quantum milliseconds |
| 20 | u32 maximum resident guest pages; zero selects the default |
| 24 | u32 maximum process slots; zero selects the default |
| 28 | u32 maximum retained filesystem bytes; zero selects the default |
| 32 | u64 maximum dispatched instructions; zero disables the quota |
| 40..63 | Reserved zero fields; nonzero values are unsupported |

| Result record offset | Field |
| --- | --- |
| 0 | u32 record size, currently 328 |
| 4, 8 | u32 state and wait kind, as enumerated in the public header |
| 12 | i32 exit code; -1 when not applicable |
| 16 | u32 terminating signal; zero when absent |
| 20, 24 | u32 stdout and stderr byte counts |
| 28 | Reserved zero |
| 32, 40 | u64 elapsed and relative wait nanoseconds |
| 48..303 | NUL-terminated diagnostic, at most 255 UTF-8 bytes plus terminator |
| 304, 308, 312 | u32 peak resident pages, filesystem bytes, process slots |
| 316 | Reserved zero |
| 320 | u64 dispatched instruction count |

File metadata is a 32-byte record: u32 kind at 0, u32 permission mode at 4,
u64 size at 8, i64 mtime seconds at 16 and i64 nanoseconds at 24. Kind values
match the public guest path ABI: regular=1, directory=2, symlink=3, null=4.

| Export suffix | Arguments and result |
| --- | --- |
| `alloc`, `free` | Allocate a nonzero byte count (up to 128 MiB) and release its exact pointer. |
| `error` | Copy the last diagnostic to a registered buffer of at least 256 bytes; return its positive status code. |
| `destroy` | Delete a checked session handle. |
| `put_file` | Handle, path pointer/length, data pointer/length, permission mode. |
| `mkdir`, `cwd` | Handle, path pointer/length, and mode for mkdir. |
| `input` | Handle, byte pointer/length, EOF flag (0 or 1). |
| `start` | Handle, vector-record pointer/length. |
| `step`, `cancel` | Handle; advance a bounded step or request cancellation. |
| `inspect` | Handle, destination pointer, exact result-record byte count. |
| `output` | Handle, stream fd (1 or 2), byte offset, destination pointer/capacity; returns copied count. |
| `stat` | Handle, path pointer/length, metadata destination pointer. |
| `read_file` | Handle, path pointer/length, offset low/high u32 words, destination pointer/capacity; returns copied count. |
| `list` | Handle, directory pointer/length, child index, name destination/capacity, metadata destination. |

Operations return zero on success except counts/handles/pointers documented
above. Failures return the negative `waste_status` enum value. `error` provides
additional diagnostics; copy them before a later call overwrites them. A directory
index beyond its immediate children returns `WASTE_ERROR_NOT_FOUND`. Names are
NUL-terminated basenames; insufficient capacity is an error. Directory order is
the kernel's current insertion order, not a stable sort contract.
Status values 0..8 preserve the existing public module API; resource allocation
failure is 9 and a host configuration limit rejection is 10. Runtime limit
outcomes are reported through the terminal result state, rather than being
mistaken for guest exit codes or traps.

A startup vector record consists of u32 argument count, u32 environment count,
u32 executable path length, path bytes, then one u32 length plus UTF-8 bytes per
argument/environment entry. Strings have no trailing NUL on the wire. Environment
count `0xffffffff` selects defaults and carries no entries; zero is explicitly
empty. The decoder rejects excess counts, embedded NULs, invalid UTF-8, truncated
strings, oversized records, and trailing bytes. Arguments exclude argv[0].

The standalone artifact imports only `waste_command_host.monotonic_ns() -> i64`,
`realtime_ns() -> i64`, and `random(ptr, len) -> i32`. Random receives a host-owned
destination in outer-engine memory and returns zero or a negative guest errno.
Absent entropy is unsupported, never silently replaced by a weak generator.
The current JS host uses monotonic `performance.now`, realtime `Date.now`, and
available Web Crypto entropy. All guest POSIX state stays in C.

## Worker adapter

`createWasteWorker` accepts the same engine/files/limits template as `createWaste`
and returns asynchronous `run` and `dispose` methods. Its default is a module
worker loaded from `waste.worker.mjs` beside the host module. `workerFactory`
can provide a worker owned by the enclosing application, and `workerSource`
accepts a complete bundled worker string for a Blob worker. The offline bundler
emits `waste.worker.bundle.js` as a classic script without imports or exports.
`workerSource` uses this classic mode so a single `file://` page does not need
module fetching from an opaque Blob origin.

Worker messages use `{protocol: 1, type, id, ...}`. Initialization has ID zero;
run IDs are positive monotonically increasing safe integers. A worker permits
one active run. Messages include init/ready, run/result, cancel, input/input-ack,
and error. Streaming input adds monotonic sequence numbers and acknowledgements;
the worker enforces its input byte cap before accepting queued chunks. Cancellation
and input for another run ID are ignored, as are late replies at the host.
An input-source error cancels the session and is reported only after that run's
cleanup boundary, so a later job cannot race a still-active cancelled session.
Output arrays are cloned across the message boundary and remain caller-owned.

Separate worker runtimes use separate outer Wasm instances. Parallelism is
between these instances, not concurrent calls on a session or a shared store.
Current host-boundary worker verification uses actual Node workers with the
same authored worker entry. `embedding-browser.cjs` exercises the bundled entry
in actual Chromium over `file://`, including cancellation, a clean second run,
file-byte parity, and absence of external runtime requests.

## Selected packages and offline embedding

```sh
make -C src/html-rt embedding-package
# A package containing only DOT, libc and their notices:
make -C src/html-rt embedding-package EMBEDDING_TOOLS=dot \
  EMBEDDING_DIR=../../build/html-rt/embedding-dot
# Explicitly include Bash for subprocess operations:
make -C src/html-rt embedding-package EMBEDDING_TOOLS=awk,m4 EMBEDDING_SHELL=1 \
  EMBEDDING_DIR=../../build/html-rt/embedding-shell
```

Open `build/html-rt/embedding/embedding.html` directly. It embeds the command
engine, selected guest files, host code and classic Blob worker. Buttons run the
three examples; cancellation and SVG download use the same documented JS API.
It needs no server, external module imports, SharedArrayBuffer or cross-origin
isolation. `example.html` is the authored consumer template and shows how an
enclosing document can supply bytes and `workerSource`.

`command/packages.json` declares the selected resource lists. The package includes
`wasm.wasm`, host/worker modules, the worker bundle, the example, this guide,
the WASTE MIT license, and a `resources/` tree preserving guest paths and notices.
`manifest.json` records ABI versions/features/record sizes, guest layout/profile,
actual import providers, resource paths/modes, and artifact SHA-256 digests.
`releaseVersion` is independent of the host ABI and metadata format; it defaults
to `development`. Set `EMBEDDING_RELEASE=VERSION` when producing a named artifact.
Digests describe the packaged current files; they are not a freshness approval
requirement for edits to `src/vfs`. `embedding-package.mjs` consumes this manifest
through the distributed public host and verifies the selected resource closure.

The package records explicit corresponding-source commands/paths for each tool.
Before distributing binary releases, run those existing aux source-package
targets and publish their archives with the notices. They remain separate from
HTML/embedding packaging; packaging never compiles guests or reinstalls a VFS
snapshot. Source targets require their pinned source objects and documented host
build tools. Verify the intended binary/source revision before release.

## Guest dependencies and compatibility

The engine-host ABI is distinct from imports in guest executables. The installed
commands use the two-argument shared CRT, `_start`, imported memory/table, ordinary
`libc` functions, legacy `env` kernel adapters, and `waste_kernel.startup_v1`.
Current command import counts and selected direct kernel operations are:

| Command | libc functions | Direct env functions | Examples |
| --- | --- | --- | --- |
| AWK | 113 | 14 | read, fstat, fork/dup/wait, signal delivery, sigsetjmp/longjmp |
| M4 | 97 | 15 | write, fstat, fork/dup/wait, file operations and signal masks |
| DOT | 91 | 2 | read and fstat |

These are inventory observations, not frozen counts or a source-hash check.
Actual provider signature checks remain authoritative. See the package READMEs
and [guest-sdk.md](guest-sdk.md) for supported guest layouts and compatibility
limits. Embedding preserves current guest imports and compiled bytes.

The minimal examples supply libc and the selected executable. AWK `system()` and
command pipes and M4 `syscmd`/`esyscmd` additionally require `/usr/bin/bash` and
the commands their scripts call. Include these resources explicitly when needed;
their absence must not grant implicit host access. DOT uses the existing core
SVG renderer/font-metric profile. Guest fork namespace/regular-file-offset limits
remain as documented by the packages.

Installed command/library bytes remain sourced from `src/vfs`; packaging must
not rebuild or reinstall them. Keep repository patches outside read-only
submodules and preserve notices and existing corresponding-source artifacts
when distributing the selected packages.
