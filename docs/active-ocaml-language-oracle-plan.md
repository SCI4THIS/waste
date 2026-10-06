# OCaml reference implementation’s scope and kernel retirement

Status: policy adopted 2026-10-04; implementation cleanup deferred.

## Scope

The official OCaml interpreter in `submodules/wasm-spec` is a reference for
WebAssembly binary (`.wasm`), text (`.wat`) and specification-script (`.wast`)
semantics: parsing, encoding/decoding, validation, linking, instantiation,
execution, traps and script assertions. Standard specification-test imports
and minimal test scaffolding belong to that role.

The experiment using OCaml as an application engine was not practical.
Production application execution, POSIX behavior and kernel development belong
to the C engine and its native/browser adapters. Do not develop additional
OCaml kernel, VFS, process, signal, scheduler, terminal, libc-host or broker
capabilities. Do not add OCaml POSIX providers merely to run C kernel fixtures.

A fixture written in WAST is not automatically a WebAssembly-language test:
its imported POSIX behavior remains a C runtime contract. Missing OCaml POSIX
imports and differences in descriptor allocation, errno, stat layout or kernel
profiles are outside the OCaml reference implementation's scope. They do not block C kernel
acceptance and are not an OCaml implementation backlog. Verify those contracts
through native/browser C parity, private sanitizer gates, compiled guest ABI
checks and applicable POSIX contract fixtures instead.

## Deferred implementation work

The target is to remove the repository-added OCaml kernel implementation and
its application-runtime integration while retaining the OCaml reference implementation. This
document records that work; adopting the policy does not remove code, commands,
artifacts or tests immediately.

1. Inventory repository-owned OCaml patch sections, kernel modules, host
   providers, evaluator hooks, direct/CPS build paths, worker/control adapters,
   Bash/libc/POSIX harnesses and command dependencies. Distinguish upstream
   language code and necessary spec-test scaffolding from application runtime
   extensions before pruning.
2. Map each useful legacy POSIX/libc assertion to a C WAST fixture, shared
   session, compiled guest ABI check or retained private C/browser check.
   Record unsupported behavior explicitly; remove duplicate legacy drivers
   only after their coverage is accounted for. Historical OCaml results remain
   evidence, not parity requirements for new C kernel work.
3. Remove the OCaml kernel and application-only integration from the
   repository-owned patch/build inputs. Retain only justified OCaml reference implementation
   fixes and scaffolding. Do not commit changes into upstream submodule history;
   use `submodules/wasm-spec-i31-int32.patch` and its build transaction.
4. Remove dependent OCaml application-runtime commands, packaging, artifacts
   and direct/threaded POSIX test requirements. Keep the supported language
   OCaml reference implementation build/run path and official test provenance; select its build variants
   based on language verification needs rather than application scheduling.
5. Re-run supported official Wasm/WAT/WAST comparisons and the relevant C
   native/browser coverage gates after pruning. Document retained language
   fixes and any language conformance gaps separately from C runtime gaps.

## Acceptance

- OCaml builds and runs the supported WebAssembly-OCaml reference implementation corpus
  without a repository-added kernel or application runtime dependency.
- C native/browser kernel, libc, application and session coverage remains
  accounted for; no OCaml kernel parity gate remains.
- Build/help, documentation and test selection describe OCaml only as the
  OCaml reference implementation. Legacy application artifacts and commands are removed with
  explicit replacements where needed.

OCaml reference implementation comparisons remain part of the
[browser/VFS plan](active-browser-vfs-layout-plan.md). Kernel retirement is
deferred follow-up work, not an additional Stage 6B kernel migration gate.


Stage 6B.48 records the pinned installed language corpus in
[the official comparison ledger](wasm-language-coverage.md): 265 official
identities, 261 compared inputs, four legacy exclusions and two explicit C
name/setup-profile differences. That ledger and its standard spec scaffolding
are retained language verification work. Its comparisons do not authorize any
additional OCaml kernel or application-runtime development.
