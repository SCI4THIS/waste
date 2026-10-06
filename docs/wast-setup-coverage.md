# WAST setup and completion audit

Stage 6B.42 audits ordinary module commands independently of assertion/action
results. Previously, cli-rt printed some load errors to stderr and html-rt
discarded them; both could report PASS with zero assertions or after later
passing assertions. The native OCaml reference implementation rejects the invalid,
unlinkable and trapping probes. No OCaml kernel work is involved.

Both corpus runners now retain a separate `setup` report: `total` attempted
instantiations, `passed` successful instances, `complete` diagnostic collection,
and ordered `failures` with source `line`, execution `status`, `phase` and error.
Phases distinguish encoding, loading (including validation/link/start), unknown
definitions and retention. Successful definition commands retain syntax; their
instances count when loaded. Explicit module assertions keep their existing
assertion records. `completed` means the command scanner reached EOF, separate
from assertion counts, guest exit and cancellation.

An ordinary setup failure now fails its file even when all assertions pass.
Native exit status is 1; both production-browser and dashboard workers preserve
the diagnostics and failed outcome. Expected-failure policy can classify the
completed failure as XFAIL; repaired entries become XPASS until their baseline
is removed. Missing files, interruption, crashes and incomplete diagnostic
collection remain host/report failures. C command recovery continues collecting
later commands without clearing the earlier failure. OCaml stops at its first
rejection; compare acceptance and rejection, not identical recovery policy.

## Focused fixtures and host boundaries

These authored fixtures live under `tests/test-suite-setup-*.wast`, outside the
installed corpus. Existing host-boundary gates stage them in isolated scratch
trees; no installed identity or generated snapshot is edited.

| Fixture suffix | Setup passed / total | Assertion/action results | Expected outcome |
| --- | --- | --- | --- |
| `empty` | 0 / 0 | 0 | Successful EOF, no fabricated checks. |
| `valid` | 3 / 3 | 0 | Provider, definition/instance and typed imported module load successfully. Definition and registration do not add instantiations. |
| `invalid` | 0 / 1 | 0 | Validation rejects the ordinary module at line 2. |
| `unlinkable` | 0 / 1 | 0 | Missing import rejects setup at line 2. |
| `start-trap` | 0 / 1 | 0 | Unasserted unreachable start rejects setup at line 2. |
| `definition` | 0 / 1 | 0 | Unknown module definition rejects its instance at line 2. |
| `encode` | 0 / 1 | 0 | Quoted module compilation fails at line 2. |
| `recovery` | 2 / 4 | 3 passing | Load and quote-encoding failures at lines 4/5 remain ordered; later named/current-module assertions still pass without repairing file acceptance. |

Native warnings-as-errors ASan/UBSan and the production browser worker verify
all eight fixtures, zero-assertion XFAIL/XPASS policy and existing command-stream
recovery. An unterminated tail preserves its 18 earlier results and one parse
failure while reporting `completed=false`. Standalone LeakSanitizer checks the
eight probes and all seven affected installed official files. C and OCaml agree
on every focused fixture's acceptance/rejection.

The browser now releases a failed command's parse after the dispatch loop stops
reading its groups. Previously the module callback could free the script while
its caller still read `script->group_count`. Failed-start engines stay owned by
the store when imported tables may retain their funcrefs. Setup diagnostics own
copied strings and survive store teardown; the next browser run resets them.
Diagnostic allocation/capacity failure is explicit, never silent truncation.
Private lifetime/ownership and actual worker/DOM/offline checks remain retained.

Stages 6B.42–6B.44 verified the actual offline dashboard's failed
`core/memory64/table64.wast` row despite passing assertions. Stage 6B.45 repairs
that binding and verifies the official row now passes all eleven setups.
The host test stages authored `tests/test-suite-setup-recovery.wast` into a
separate synthetic row without changing installed selection. Its three
assertions pass, but the row fails and visibly reports setup errors at line 4
(load) and line 5 (encode). The result JSON retains both failures. The evidence
includes `actual-browser-dashboard-setup.json` and `dashboard-setup.png`.

## Segment capacity and portable behavior

Stage 6B.43 resolves the six high segment-index errors in five official files.
Unsigned LEB encoding was already correct; the WAT parser silently truncated
both segment spaces at 32 declarations. Capacity is now **128** in parser,
name-resolution, binary loader and runtime state. All thirteen append forms
check the bound, including shorthand declarations. Overflow rejects the whole
parsed module and frees unretained data bytes. This resource error is not a
semantic-invalid result, even inside `assert_invalid`. Arbitrary segment counts
remain outside the current bounded implementation.

Authored `tests/test-suite-segment-indices.wast` runs outside installed selection
through the retained native/browser host-boundary gates. Its **64 assertions /
two setups** exercise data and element indices **31, 32, 63, 64, 127**, distinct
payload/function values, numeric and deferred named references, 32/64-bit
memory/table initialization, drops, post-drop traps, zero-length initialization
and four invalid unknown-index cases. Native C, production browser and the
OCaml reference implementation agree on all checks. The two
`tests/test-suite-segment-capacity-{data,elem}.wast` fixtures exceed the bound
with 129 declarations; native/browser acceptance fails both ordinary modules
and `assert_invalid` wrappers. Their implementation resource limit is not an
OCaml language rejection requirement. Native warnings-as-errors ASan/UBSan and
LeakSanitizer include the portable fixture, all five repaired official files
and capacity payload cleanup.

A separate syntax audit found valid plain `table.init` syntax rejected, and
plain `data.drop`/`elem.drop` ignored by permissive WAST parsing. The evidence
includes C/OCaml probes demonstrating these acceptance differences. The new
portable fixture uses folded forms. Stage 6B.46 repairs these three plain forms
and the missing folded table initialization cases, independently of segment count.

## Element segment reference types

Stage 6B.44 resolves the three setup errors in `core/elem.wast`. Bare function
index lists and `elemkind func` declare non-null `(ref func)`, including empty,
passive and declarative vectors. The WAT parser previously widened these to
nullable `funcref`. It now separates function-index lists from expression types
and preserves their declaration; synthesized table-shorthand segments inherit
the table's declared type. Explicit nullable `funcref` remains nullable even if
all its items are `ref.func`.

The binary loader now stores non-null `(ref func)` for modes 0–3, including
passive/declarative vectors used by instruction validation. Mode 4 retains
implicit nullable `funcref`, and modes 5–7 retain the encoded type. Active
initialization uses the declared type directly, replacing the prior active-only
effective-type workaround. No platform adapter, kernel or ownership change is
needed; the existing encoder emits valid expression vectors with these types.

Authored `tests/test-suite-element-types.wast` adds **82 portable assertions /
eleven ordinary setups** outside installed selection. It covers bare/explicit
and empty vectors, nullable shorthand with null slots, table64, passive
initialization, active/declarative dropped state, repeated drops, post-drop
traps, all eight binary modes with distinct initial/segment function values,
and nullable-to-non-null rejection despite
non-null contents. Native C, browser and the OCaml reference implementation agree;
OCaml also accepts/rejects all twenty C-encoded setup/invalid modules as expected.
Native warnings-as-errors ASan/UBSan and LeakSanitizer check expected rejection,
segment lifetime and teardown. `core/elem.wast` now records **76 successful
setups / 72 passing assertions** and leaves both XFAIL lists.

## Standard spectest table64

Stage 6B.45 adds the C language-test binding matching the existing OCaml
`spectest.ml`: a separate nullable `funcref` table with **64-bit indices,
initial size 10 and maximum 20**. The shared store initializes and frees its
entries; the existing loader validates address width, element type and limits.
Imports within a script share the object and function ownership, while fresh
sandboxes isolate mutations. Checkpoint capture includes the table before any
imports, and restore recovers null contents and growth with stable identity.

`tests/test-suite-spectest-table64.wast` supplies **36 assertions / three setups**;
`tests/test-suite-spectest-table64-isolation.wast` supplies **three assertions /
one setup**. Both remain outside installed selection. Native/browser host gates
run both orders at jobs 1/3. They cover initial null slots, wide bounds, invalid
imports, table32 separation, aliases, cross-module calls, growth and isolation.
The official file and both fixtures agree with the OCaml reference implementation;
all nine C-encoded modules and 36 actions/assertions also pass in source order.
Two added private checkpoint checks, ASan/UBSan and LeakSanitizer cover restored
host table ownership and teardown. No OCaml kernel or provider development is
involved. `core/memory64/table64.wast` now has **eleven successful setups / two
passing assertions** and leaves both XFAIL lists.

## Official setup history and current gaps

Stage 6B.42 exposed **2,313 setup attempts / 2,303 successes / ten failures**
in seven files accepted by OCaml. Stage 6B.43 raised that to **2,309 successes /
four failures**; Stage 6B.44 reached **2,312 successes / one failure**;
Stage 6B.45 now records **2,313 attempts / 2,313 successes / zero failures**.
Repaired files leave both tracked expected-failure lists only after
C/native/browser and OCaml agreement:

| Identity | Stage 6B.42 failing setup lines | Current state |
| --- | --- | --- |
| `core/bulk-memory/bulk.wast` | 181, 274 | Fixed in 6B.43: all 65 data/element declarations retained. |
| `core/bulk-memory/memory_init.wast` | 993 | Fixed in 6B.43: `memory.init` segment 64 loads. |
| `core/bulk-memory/table_init.wast` | 2248 | Fixed in 6B.43: `table.init` segment 64 loads. |
| `core/memory64/memory_init64.wast` | 993 | Fixed in 6B.43: high data index with memory64 loads. |
| `core/memory64/table_init64.wast` | 2433 | Fixed in 6B.43: high element index with table64 loads. |
| `core/elem.wast` | 87, 448, 482 | Fixed in 6B.44: preserve non-null function-index segment types. |
| `core/memory64/table64.wast` | 13 | Fixed in 6B.45: standard table64 binding, width/type/limits and sandbox ownership. |

Full native/actual offline Chromium execution now agrees on **289 PASS /
2 XFAIL / 5 SKIP**, preserving all **296 identities / 64,309 assertion/action
results**. Stage 6B.45 changes only `core/memory64/table64.wast` from XFAIL to PASS;
all other outcomes and prior ordered assertion results are unchanged. All seven
official setup XFAILs exposed in 6B.42 are resolved; two libc XFAILs and five SKIPs
remain. Twelve executor fixtures retain **1,230** results,
and six sessions retain **358** checks/transcripts. Installed manifest/inventory
and all WAST bytes remain unchanged; both offline pages rebuild explicitly.

Stage 6B.42 evidence: `build/engine/refactor-stage6b-setup-audit/` retains the
original reporting audit, all ten diagnostics and previous outcome comparisons.
Stage 6B.43 evidence: `build/engine/refactor-stage6b-segment-indices/` contains
previous parser/capacity and baseline snapshots, language comparisons, native
sanitizer/leak and browser reports, flat-syntax follow-up probes, unchanged source
hashes and ordered corpus/session parity proof.

Stage 6B.44 evidence: `build/engine/refactor-stage6b-active-elements/` contains
previous parser/loader/baseline snapshots, original rejection and module bytes,
portable and C-encoded OCaml language comparisons, native sanitizer/leak and
browser results, unchanged source hashes and ordered corpus/session parity proof.

Stage 6B.45 evidence: `build/engine/refactor-stage6b-spectest-table64/` contains
prior store/checkpoint/baseline snapshots, portable and encoded OCaml reference implementation
results, native sanitizer/leak and browser reports, unchanged corpus/source
hashes and ordered corpus/session parity proof.

Stage 6B.46 implements plain `table.init`, `data.drop` and `elem.drop`, replacing
silent drop omission with emitted opcodes and real validation. It also repairs
folded numeric/numeric and numeric/named table initialization and preserves
operand validation for folded named abbreviations. Text explicit indices are
table/element; binary immediates are element/table. Deferred references keep
both index spaces and source locations intact. Bison conflicts remain 24.

`tests/test-suite-flat-bulk.wast` supplies **190 assertions / three setups**
outside installed selection: plain/folded numeric/forward-named forms, 32/64-bit
addresses, distinct destination/segment/peer values, repeated drops, post-drop
traps, zero-length operations and index 127. Fourteen invalid modules and six
quoted malformed modules verify rejection as well as execution. Native/browser
C and the OCaml reference implementation agree; all 17 C-encoded setup/invalid modules
also pass the ordered OCaml reference implementation script. Nine ordinary acceptance/rejection probes
include the original plain instruction failures. ASan/UBSan and LeakSanitizer
cover successful setup, rejected imports/instructions and owned payload cleanup.
Full corpus and setup records, six session transcripts and installed bytes remain
unchanged. Evidence: `build/engine/refactor-stage6b-flat-bulk/`.

Stage 6B.47 resolves both plain `table.copy`/`table.fill` follow-up gaps and
folded numeric copy/fill omissions. Copy takes zero or two table indices in
destination/source order; fill takes zero or one. Numeric and forward-named
forms share index resolution and validation. `tests/test-suite-table-copy-fill.wast`
adds **611 assertions / seven setups** outside installed selection: both table
widths, mixed-width copies, overlap, atomic bounds failure, zero-length/null
operations, reference identity/nullability and foreign function owners.
Twenty-six invalid modules and three quoted malformed modules preserve
rejection distinctions. Native/browser C and authored/33 C-encoded module
OCaml reference implementation checks pass, as do seven ordinary acceptance/rejection probes and
ASan/UBSan/LeakSanitizer cleanup gates. All 296 corpus outcomes, 64,309 results,
2,313 successful setup records, six session transcripts and installed bytes
remain unchanged. Evidence: `build/engine/refactor-stage6b-table-copy-fill/`.

Next enumerate supported installed official language inputs and their OCaml
comparison/acceptance and explicit exclusions in a finite identity-level ledger.
Inline exported-table shorthand is outside this slice's syntax contract;
`deferred-language-gaps.json` records its C rejection/OCaml acceptance.
Broader supported-OCaml reference implementation acceptance remains open.
Stage 6B, consolidation and dashboard retirement remain open. OCaml remains a
language-only OCaml reference implementation; no additional OCaml kernel development is planned.


## Finite official language comparison

Stage 6B.48 adds [the identity-level ledger](wasm-language-coverage.md):
**265 official identities / 261 compared and independently accepted / four
legacy exclusions**. The inventory is complete; **259** inputs agree on
ordered check identity/kind and ordinary setup counts, with two explicit gaps.
`core/inline-module.wast` is accepted with zero checks but validates as a
zero-instance definition in OCaml and instantiates once in C. Official setup
counts are **2,261 C / 2,260 OCaml**; full C corpus setup remains **2,313/2,313**.
`core/names.wast` check 47 reveals a 257-byte name truncated to 255 bytes;
matching declaration/action truncation masks the loss behind 482 passing checks.
Long-name collision and equivalent escape-spelling probes confirm follow-up work.

The checker audits source/inventory provenance, compares C metadata and actual
assertion/setup/EOF reports with OCaml traces, and preserves rejection categories.
Only custom-annotation suites enable custom handlers. Source-hash/issue-kind
pinned gap policy and omission/order/setup/name negative controls prevent silent
coverage reduction. Fresh native results reconcile with unchanged Stage 6B.47
actual-browser records; installed/runtime bytes and baselines remain unchanged.
Next repair export/action name length and escape semantics, then resolve the
inline setup profile and remaining syntax gaps. No OCaml kernel work is added.
Evidence: `build/engine/refactor-stage6b-language-ledger/`.
