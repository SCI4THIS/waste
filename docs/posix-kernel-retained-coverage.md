# Retained native kernel coverage

Current CHECK inventory of `src/vfs/root/test/diy-posix-test/posix-kernel.c`: **27 helpers, 200 retained
CHECK sites and 340 successful runtime checks**. See
[test boundary selection](techniques.md#test-boundary-selection) for the
distinction between guest outcomes and private C invariants.

The four `test_pipe_full` sites execute 19 times (sixteen positive 256-byte
writes, exact 4096-byte capacity, two raw readiness queries). The five
lifecycle sites execute 130 times (two allocation guards and descriptor loops
of 64/3/61). Every other site executes once. 157 source sites produce 297
checks. The null-device ownership helper adds 16 source sites and runtime checks,
bringing those totals to 173 sites and 313 checks. Child-deadline ownership adds
11 sites/checks for missing callbacks, token/clone/cancellation ownership and
full-width deadline arithmetic, bringing current totals to 184/324.
Status/wait ownership adds sixteen sites/checks for shared OFD identity, stable
wait tokens, nonblocking cancellation, copied terminal minimums independent of
SELECT readiness, restored blocking registration, retained regular status across
namespace rebasing, teardown and saturated-ref rejection, bringing totals to 200/340.

Each retained ordinal is classified below. Guest complements exercise public
behavior but do not replace the named C boundary. Supporting open/read/write
guards stay when they validate the host-seeded or cloned fixture used by a
compound ownership/API probe.

## Allocation, mapping objects and namespace publication

| Current helper / ordinals | Retained boundary | Guest complement |
| --- | --- | --- |
| `test_lifecycle` 1/3 | Allocation of noninteractive/interactive kernel objects | Guest sandboxes cannot invoke these allocation APIs |
| `test_lifecycle` 2/4/5 | Full raw descriptor-table initialization across 64 slots and three terminal aliases | Descriptor fixtures and shared sessions observe selected guest descriptors |
| `test_missing_clocks` 1–4 | Allocation and absent platform-clock callbacks return ENOSYS without changing the output | Guest clock WAST covers installed callbacks; it cannot remove runtime callback pointers |
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
| `test_hardlink_inode_ownership` 1–9 | Opaque inode pointer/refcount identity, namespace counts across clone, directory pointer rebasing, and descriptor retention after parent destruction and unlink | `system/clang/hardlink/hardlink.wast` covers public links, stat layouts, errors and compiled fork/wait observations |
| `test_null_inode_ownership` 1–16 | Shared stateless OFD and retained inode references across dup/fork, absent byte backing, orphan lifetime after both namespaces unlink and the parent is destroyed, and final alias release | `system/devices/null.wast` checks public metadata/path/readiness; actual LLVM empty redirects run in `system/clang/program-execution/program-execution.wast` |
| `test_child_deadline_ownership` 1–11 | Missing clock callback, stable wait token on retry, clone drops active wait while preserving transaction alarm, cancellation leaves independent alarm, exact UINT64_MAX expiry | `system/clang/child-deadline` covers public errors, live/nested concurrency, controlled clocks/signals and cleanup |
| `test_creation_mask` 1–2 | Injected realtime callback determines exact second/nanosecond mtimes at creation and write | Guest clock/stat checks do not expose this callback installation |
| `test_creation_mask` 3 | An explicitly cloned kernel inherits an independent umask | Guest umask checks in `directory-umask.wast` complement public behavior |
| `test_isolation` 1–2 | Input in one separately allocated kernel leaves the other raw terminal queue empty | Isolated WAST batches use the runtime's sandbox construction |
| `test_isolation` 3–4 | Same descriptor number is open in one kernel and closed in another | Guest descriptor tests observe one current kernel |
| `test_isolation` 5–6 | Closing stdin in one kernel leaves the other kernel's alias alive | Shared terminal sessions cover aliases within one kernel |

## Record-lock ownership and continuations

| Current helper / ordinals | Retained boundary | Guest complement |
| --- | --- | --- |
| `test_record_lock_ownership` 1–5 | Host setup, exact inode ref retention, transactional owner identity/teardown and explicitly assigned fork identity | `system/clang/locks/locks.wast` verifies public fork/exec and inode aliases |
| `test_record_lock_ownership` 6–11 | Copied normalized interests, raw polling, wait generation and reference balance across a scratch clone and resume | WAST checks blocking wakeup and restart after a signal handler seeks |
| `test_record_lock_ownership` 12–16 | Exit reference balance, config-derived admission/split failure and atomic coalescing | WAST checks public partial unlock/downgrade and close release |
| `test_record_lock_ownership` 17–18 | Injected unrepresentable fd offset and saturated inode refcount reject before publication | Guest struct fields cannot represent these private states |
| `test_record_lock_ownership` 19–23 | Closed-fd wait revalidation/cancellation and exact parent record/inode lifetime | WAST verifies alias close, exit/death and process contention |

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

Terminal-mode setup clears the observed SIGINT before its independent VMIN
read and installs a caught SIGWINCH disposition before checking its pending
queue. Default SIGWINCH notifications are ignored; leaving the earlier SIGINT
pending would make the next read return EINTR instead of testing VMIN.
| `test_terminal_output_lengths` 1–2 | Private output helper reports transformed length 4 versus source length 3 | Guest write reports consumed source bytes; timing/output transcript observes expansion |
| `test_terminal_vtime` 1/4 | Direct kernel EAGAIN while registering timed reads | Terminal timing session exercises guest READ yields |
| `test_terminal_vtime` 2–3 | Active wait record and exact TIMEOUT polling against an injected C clock | Guest frozen-clock events cannot invoke the private clock callback |
| `test_pipe_readiness` 1–6 | Raw IN/OUT/HUP/ERR masks around actual write/drain | `pipe-descriptors.wast` uses SELECT and real byte I/O |
| `test_pipe_readiness` 7 | Direct blocking kernel EAGAIN on an empty pipe | Guest blocking reads yield on the registered wait in either runtime profile; explicit O_NONBLOCK returns EAGAIN without waiting |
| `test_status_wait_ownership` 1–16 | Setup, exact wait generation, shared OFD/status pointers across clones, cancellation on nonblocking retry, fresh blocking token, copied terminal read minimum independent of raw readiness, absent tokens on nonblocking terminal/stream reads, regular status ref balance and saturated-reference rejection | `pipe-descriptors.wast` checks flags and aliases; `system/scheduling/pipe-modes.wast` checks fork sharing for pipe and regular status, restored blocking wakeup, EOF and append aliases; terminal-readiness checks post-yield VMIN completion |
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

These kernel contracts use C native/browser parity.
