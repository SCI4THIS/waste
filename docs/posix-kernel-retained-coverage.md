# Retained native kernel coverage

Stage 6B.38 completes the current CHECK inventory of `tests/posix-kernel.c`:
**21 helpers, 121 retained CHECK sites and 261 successful runtime checks**.
Ordinals below are local to each current helper, after earlier migrations.
Historical migration ordinals in Git history refer to their dated source
snapshots instead. See [test boundary selection](techniques.md#test-boundary-selection)
for the distinction between guest outcomes and private C invariants.

One final standalone guest check, pipe creation in `test_pipe_full`, moved to
`pipe-descriptors.wast` assertion 36. Real creation remains as setup. Its four
remaining sites execute 19 times: sixteen positive 256-byte writes, exact
4096-byte capacity, and two raw readiness queries. The five lifecycle sites
execute 130 times: two allocation guards and descriptor loops of 64/3/61.
Every other site executes once. Thus 121 source sites produce 261 checks;
the larger runtime count is not an additional migration count.

Each retained ordinal is classified below. Guest complements exercise public
behavior but do not replace the named C boundary. Supporting open/read/write
guards stay when they validate the host-seeded or cloned fixture used by a
compound ownership/API probe. No partial compound check is counted as fully
migrated.

## Allocation, mapping objects and namespace publication

| Current helper / ordinals | Retained boundary | Guest complement |
| --- | --- | --- |
| `test_lifecycle` 1/3 | Allocation of noninteractive/interactive kernel objects | Guest sandboxes cannot invoke these allocation APIs |
| `test_lifecycle` 2/4/5 | Full raw descriptor-table initialization across 64 slots and three terminal aliases | Descriptor fixtures and shared sessions observe selected guest descriptors |
| `test_invalid_fd` 1–3 | Direct readiness-query bounds, including FD_MAX+100 | Guest descriptor errors in `descriptor-flags.wast` |
| `test_invalid_fd` 4/6 | Direct raw query of closed descriptors in both kernel profiles | Closed guest descriptor operations |
| `test_invalid_fd` 5 | NULL-kernel rejection | A guest descriptor or linear-memory address is not a C kernel pointer |
| `test_file_read_at` 1–3 | Kernel allocation, host-seeded source installation and acceptance of that mapping source by open | `path-vfs.wast` exercises ordinary guest open/I/O, not host source installation |
| `test_file_read_at` 4–6 | Offset-independent read/write APIs and descriptor cursor independence after those calls | Guest seek/read/write do not invoke the private mapping APIs |
| `test_file_read_at` 7 | Exact host-seeded inode, writable identity and non-NULL retained object handle | Guest stat does not expose retained C object handles |
| `test_file_read_at` 8 | Write through a retained object after descriptor close, then read the shared backing | Open guest aliases complement persistence but cannot call write_object |
| `test_file_read_at` 9 | Explicit kernel clone shares retained mapping backing | Guest fork sessions also involve the process driver |
| `test_file_read_at` 10–11 | Strict mapping-range rejection past EOF and direct shared-source reads after clone writes | Ordinary POSIX reads have a different short-read/EOF contract |
| `test_file_read_at` 12–13 | Truncate/grow via the private size helper, including strict old-tail rejection | Guest truncation/stat in `shared-memory.wast` complements size changes |
| `test_fork_path_publication` 1–2 | Explicit clone allocation and child output setup/write guards | Guest fork/exec sessions cover higher-level process behavior |
| `test_fork_path_publication` 3 | Parent namespace remains unpublished before an explicit merge | Guest reap semantics cannot isolate this private pre-merge API boundary |
| `test_fork_path_publication` 4 | Direct merge publishes the child pathname | Guest reap also exercises scheduling and process teardown |
| `test_fork_path_publication` 5 | Merged namespace opens and reads the retained child bytes | Guest file I/O complements contents, not explicit clone/merge ownership |

Kernel destruction, retained-object release, child destruction and NULL destroy
remain real setup/teardown calls under sanitizers. They are not separate CHECK
sites or guest assertions.

## Seeded metadata and clone-private state

| Current helper / ordinals | Retained boundary | Guest complement |
| --- | --- | --- |
| `test_directory_metadata` 1 | Exact host-seeded directory/file metadata installation | `directory-umask.wast` creates guest directory fixtures |
| `test_directory_metadata` 2–4 | Dot, parent and child preserve seeded inodes 41/40/42 | Guest iteration tests names/EOF and practical metadata |
| `test_directory_metadata` 5–6 | Root dot and parent preserve fixed root inode 1 | Guest enumeration cannot assume arbitrary host-seeded inode values |
| `test_creation_mask` 1–2 | Injected realtime callback determines exact second/nanosecond mtimes at creation and write | Guest clock/stat checks do not expose this callback installation |
| `test_creation_mask` 3 | An explicitly cloned kernel inherits an independent umask | Guest umask checks in `directory-umask.wast` complement public behavior |
| `test_isolation` 1–2 | Input in one separately allocated kernel leaves the other raw terminal queue empty | Isolated WAST batches use the runtime's sandbox construction |
| `test_isolation` 3–4 | Same descriptor number is open in one kernel and closed in another | Guest descriptor tests observe one current kernel |
| `test_isolation` 5–6 | Closing stdin in one kernel leaves the other kernel's alias alive | Shared terminal sessions cover aliases within one kernel |

## Terminal and pipe API boundaries

| Current helper / ordinals | Retained boundary | Guest complement |
| --- | --- | --- |
| `test_terminal_readiness` 1–5 | Raw initial IN/OUT/HUP masks and exact equality across terminal aliases | The 90-check terminal-readiness session uses SELECT/pselect |
| `test_terminal_readiness` 6 | Direct host enqueue acceptance | Real post-yield input events |
| `test_terminal_readiness` 7–9 | Raw input/writable masks before and after drain | Guest read contents and SELECT readiness |
| `test_terminal_readiness` 10 | Direct host EOF API result | Canonical VEOF session input uses a different entry point |
| `test_terminal_readiness` 11–12 | Raw EOF IN/HUP masks | Repeated zero reads and guest readiness |
| `test_terminal_readiness` 13 | Host enqueue rejects a closed non-terminal descriptor | Guest read/close errors do not invoke host enqueue |
| `test_terminal_modes` 1/3 | Host enqueue accepts partial line and newline | Canonical partial-input session guards |
| `test_terminal_modes` 2/4 | Exact raw canonical readiness masks | Guest SELECT completion |
| `test_terminal_modes` 5–6 | Private SIGINT pending state and raw partial-line discard | Handler/session behavior does not expose all private queues |
| `test_terminal_modes` 7 | Direct kernel VMIN EAGAIN result | Interactive guest reads yield instead |
| `test_terminal_modes` 8–11 | Host winsize acceptance and private SIGWINCH absence/presence | Existing resize/signal session contracts exercise runtime controls |
| `test_terminal_output_lengths` 1–2 | Private output helper reports transformed length 4 versus source length 3 | Guest write reports consumed source bytes; timing/output transcript observes expansion |
| `test_terminal_vtime` 1/4 | Direct kernel EAGAIN while registering timed reads | Terminal timing session exercises guest READ yields |
| `test_terminal_vtime` 2–3 | Active wait record and exact TIMEOUT polling against an injected C clock | Guest frozen-clock events cannot invoke the private clock callback |
| `test_pipe_readiness` 1–6 | Raw IN/OUT/HUP/ERR masks around actual write/drain | `pipe-descriptors.wast` uses SELECT and real byte I/O |
| `test_pipe_readiness` 7 | Direct kernel EAGAIN on an empty pipe | Ordinary batch read exposes raw EAGAIN; interactive import behavior differs |
| `test_pipe_close_transitions` 1–4 | Raw IN/HUP after writer close and ERR/OUT after reader close | Guest buffered drain, EOF and EPIPE |
| `test_pipe_full` 1 | Every positive partial fill (sixteen executions) | One guest capacity fill does not prove each private partial-write result |
| `test_pipe_full` 2 | Exact internal POSIX_PIPE_CAPACITY aggregate | Guest fill/full-write/read recovery complements the 4096-byte capacity |
| `test_pipe_full` 3–4 | Raw writability disappears at capacity and returns after drain | Guest SELECT complements the raw masks |
| `test_dup` 1–5 | Raw readiness equality, host enqueue visibility, drain, closed alias and surviving original | Shared terminal-descriptor session covers guest aliases/read/close |
| `test_pipe_dup_readiness` 1–2 | Raw HUP tracks the final writer reference | Guest duplicated writer keeps the reader live, then exposes EOF |
| `test_close` 1–3 | Direct raw readiness after closing one shared terminal alias | Shared terminal-descriptor session covers closed reads and surviving aliases |

## Pointer, exec, shared-memory and signal boundaries

| Current helper / ordinals | Retained boundary | Guest complement |
| --- | --- | --- |
| `test_edge_cases` 1–4 | C NULL read/write buffers, pipe pair and enqueue data | A guest memory offset is not a C NULL pointer |
| `test_close_on_exec` 1–2 | Direct close_on_exec destroys marked descriptors and preserves an unmarked replacement | Guest fcntl/dup flags do not invoke direct exec teardown |
| `test_close_on_exec` 3 | Unmarked descriptor survives an explicit kernel clone | Guest process lifecycle also exercises the driver and evaluator |
| `test_shared_memory_names` 1–2 | Allocate independent kernels and explicitly attach the shared namespace pointer | Guest shm_open cannot install a kernel namespace pointer |
| `test_shared_memory_names` 3–4 | Direct credential changes and failed-creation non-publication | Guest root-profile shared-memory checks do not change credentials |
| `test_shared_memory_names` 5 | Offset-independent write to the named backing object | Guest writes do not call the mapping API |
| `test_shared_memory_names` 6–7 | Independent kernel credentials deny another user and restore root | No guest credential injection is added |
| `test_shared_memory_names` 8 | Independent attached kernel opens and reads the same backing | Multiple guest descriptors complement public sharing |
| `test_shared_memory_names` 9 | Explicit clone allocation guard | Guest fork involves additional process machinery |
| `test_shared_memory_names` 10–11 | Direct original/cloned backing reads survive namespace unlink | Guest unlink/open-descriptor tests complement lifetime |
| `test_foreground_process_group_routing` 1 | Direct host background VINTR enqueue | Shared process-group session delivers real terminal bytes |
| `test_foreground_process_group_routing` 2/4 | Private signal is absent while background and consumed after foreground routing | Guest sigpending exposes blocked signals, not the whole private queue |
| `test_foreground_process_group_routing` 3 | Raw kernel pselect EINTR | Guest session observes interruption and handler delivery |

## Verification and remaining work

`make -C src/cli-rt posix-kernel` requires no Node or HTML. It runs the C gate
under warnings-as-errors/ASan/UBSan, compares authored/installed regression
bytes, executes the WAST fixtures and verifies shared native event contracts.
Make retains the documented sandbox-ptrace LeakSanitizer exception; standalone
leak verification runs outside that environment.

The dated evidence directory
`build/engine/refactor-stage6b-retained-kernel-audit/` contains the complete
per-site inventory, expression hashes, expected execution multiplicities, one
full migration mapping and the preserved-expression/runtime-count proof.
It also records unchanged installed fixture bytes and native/browser parity.

This completes classification of the current helpers in this C file. Audits of
other C harnesses and legacy libc/CJS assertions, supported OCaml reference implementation
comparison and consolidation/retirement work remain in the active plan.
These kernel contracts use C native/browser parity; OCaml remains a language
OCaml reference implementation, with no additional kernel/provider development or comparison required.
