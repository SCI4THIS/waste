# Installed official language comparison ledger

Stage 6B.52 snapshot (2026-10-05). Every installed identity is classified in
`build/engine/refactor-stage6b-export-table/language-comparison/ledger.json`.
The official selection is **265 identities: 261 compared, four legacy syntax
exclusions**. All 261 inputs are accepted independently by C and OCaml and
**261 agree on ordered language checks and ordinary setup counts**. The
comparison inventory is complete with no known official-language ledger gaps.

Provenance: official test revision `4b29bdbced924599346ea2ffd9e975af5d28c735`,
installed selection SHA-256 `95078ad7cde3d6923d9d7515bf84ccffff22a187797ea56eb6e3e84408861e1d`.
Installed input hashes, manifest/policy/binary/report hashes, subprocess commands
and raw C metadata/OCaml reference implementation traces are retained in the machine ledger and evidence.
Inputs are audited against installed inventory and authored/upstream sources.

## Comparison contract

The C parser supplies assertion kinds and action names through `--browser-spec`.
Native command-stream results supply actual assertion outcomes, setup diagnostics
and EOF completion. OCaml runs each unchanged installed input in a fresh process
with `-t`; only `custom/*` suites enable `-ca`. Core binary custom sections remain
opaque, as intended by those tests. Enabling annotation handlers globally made
`core/custom.wast` recursively decode arbitrary section payloads; that profile
error is captured in preliminary evidence and is not a C language exclusion.

Compare ordered kinds/action names and counts independently of exit status.
OCaml traces retain malformed/invalid custom checks, malformed binary/text,
validation, unlinkable, return, runtime trap, instantiation trap, exception,
exhaustion and bare-action categories. C folds custom categories into their base
kinds and records bare actions as return checks; the adapter normalizes those
display conventions while retaining independent category totals. Decode name
escape spellings for comparison, including UTF-8 byte escapes and OCaml Unicode
escapes. Preserve Unicode distinctions and full names; never compare prefixes.
No second WAST command scanner or OCaml provider/kernel change is added.

The Stage 6B.48 baseline was reconciled with Stage 6B.47 actual offline-browser
results. Stage 6B.52 regenerated the native corpus report and reran the strict
language-only comparison after the parser change. The Stage 6B.51 full offline
dashboard report had one incomplete
`engine-regressions/continuation-waits.wast` execution. The focused current
`core/inline-module.wast` browser report from that stage and the Stage 6B.52
exported-table shorthand fixture both pass. The full browser report is not used
to claim corpus-wide parity for this snapshot. A supplied report
must come from the current corpus/runtime: report hashes and identity/coverage
checks preserve evidence, but the existing report format has no source digest
and cannot independently prove freshness.

Expected gaps are pinned by identity, source hash and exact issue categories in
`tests/language-oracle-policy.json`. New gaps, changed issue categories or repaired
baselines exit 1; configuration/stale corpus errors exit 2. The policy currently
contains no expected gaps; `--strict` enforces that state.

## Coverage and limits

The 261 compared inputs retain **62,975 assertion/action results** and
**2,260 successful ordinary instantiations** in both runtimes. Bare inline
module fields in `core/inline-module.wast` are retained as a definition and are
not instantiated, matching the OCaml WAST profile.
OCaml reference implementation traces exercise **810 binary definitions** and **1,263 quoted text
definitions**, in addition to directly parsed WAST text. This checks the installed
scripts and their embedded inputs. It does not cross-run every C-encoded module
against OCaml, compare identical error strings/recovery policies, or establish
acceptance of arbitrary text outside the pinned corpus. The earlier portable
fixtures retain their separate C-encoded OCaml reference implementation comparisons.

Stage 6B.50 resolves the `core/names.wast` identity difference. The 257-byte
Unicode export/action name, byte-escaped names, and distinct embedded-NUL names
now compare without prefix matching or Unicode normalization. Native and actual
offline-browser runs both pass **482/482** with matching action names, results
and four setup records. Separate C-encoded WAST probes cover `\\41`/`A`, distinct
`\\00`/`\\01`, and two 257-byte names sharing a 256-byte prefix; their native
assertions pass and the OCaml reference implementation accepts and traces their binary modules.
Names allow up to 511 ordinary bytes in the bounded internal storage; embedded
NUL and marker byte `0x01` consume an extra internal byte and overflow is
rejected explicitly.

| OCaml reference implementation check category | Count |
| --- | ---: |
| `action` | 357 |
| `exception` | 18 |
| `exhaustion` | 15 |
| `instantiation-trap` | 54 |
| `invalid` | 2,723 |
| `invalid custom` | 1 |
| `malformed` | 1,940 |
| `malformed custom` | 19 |
| `return` | 52,718 |
| `trap` | 4,930 |
| `unlinkable` | 200 |

Current bounded C parser limits remain explicit: 128 data/element segments,
eight tables, 32 memories, 1,024 functions and 512-byte C-string slots for
names. Ordinary names can carry 511 bytes; embedded NUL and byte `0x01` use an
injective two-byte internal escape, so those names can hit the storage bound
earlier. These are implementation capacities, not WebAssembly
semantic-invalid classifications. Segment-overflow probes remain separate from
official-language acceptance; see [the setup audit](wast-setup-coverage.md).

## Resolved profile differences

- `core/inline-module.wast`: both accept zero checks. Bare inline module fields
  are definitions in both runtimes and produce zero instantiations.

Exported table32/table64 element-list shorthand was added in Stage 6B.52 and is
covered by `tests/test-suite-exported-table-shorthand.wast`; it is a
repository-owned syntax probe, not an additional official-ledger identity. No
installed identity is removed or reclassified to conceal differences.
The 31 repository-owned identities (14 libc, five DIY, twelve executor fixtures)
are outside this official ledger; their C runtime/import/session coverage remains
in [the regression coverage guide](test-coverage.md). WAST spelling does not make
POSIX imports an OCaml language contract. Existing OCaml kernel removal is
deferred under [the retirement plan](active-ocaml-language-oracle-plan.md).

## Run

```sh
build/cli-rt/waste-test --vfs-root=src/vfs --jobs=1 \
  --expected-failures=tests/native-corpus-expected-failures.txt \
  --results=build/cli-rt/language-corpus-results.json
python3 tests/OCaml reference implementation-check.py \
  --native-results=build/cli-rt/language-corpus-results.json
python3 tests/OCaml reference implementation-ledger-check.py
```

Add `--browser-results=PATH` to reconcile an actual production-browser report.
Use `--output=PATH` for evidence and `--strict` to enforce gap-free comparison.
The 180-second subprocess bound belongs to this OCaml reference implementation audit; production C
corpus deadlines, selection and expected-failure policies are unchanged.

## Official identities

Counts below are C/OCaml reference implementation checks and C/OCaml reference implementation ordinary instantiations.
“Gap” preserves a completed, accepted script with a comparison difference.
Legacy exclusions retain their installed unsupported reason in the JSON ledger.

| Identity | Checks C / OCaml reference implementation | Setups C / OCaml reference implementation | Comparison |
| --- | ---: | ---: | --- |
| `core/address.wast` | 256 / 256 | 4 / 4 | Agreed |
| `core/align.wast` | 140 / 140 | 25 / 25 | Agreed |
| `core/annotations.wast` | 64 / 64 | 10 / 10 | Agreed |
| `core/binary-leb128.wast` | 58 / 58 | 33 / 33 | Agreed |
| `core/binary.wast` | 107 / 107 | 20 / 20 | Agreed |
| `core/block.wast` | 222 / 222 | 1 / 1 | Agreed |
| `core/br.wast` | 96 / 96 | 1 / 1 | Agreed |
| `core/br_if.wast` | 118 / 118 | 1 / 1 | Agreed |
| `core/br_on_non_null.wast` | 9 / 9 | 3 / 3 | Agreed |
| `core/br_on_null.wast` | 7 / 7 | 3 / 3 | Agreed |
| `core/br_table.wast` | 185 / 185 | 1 / 1 | Agreed |
| `core/bulk-memory/bulk.wast` | 104 / 104 | 13 / 13 | Agreed |
| `core/bulk-memory/memory_copy.wast` | 4417 / 4417 | 33 / 33 | Agreed |
| `core/bulk-memory/memory_fill.wast` | 89 / 89 | 11 / 11 | Agreed |
| `core/bulk-memory/memory_init.wast` | 221 / 221 | 29 / 29 | Agreed |
| `core/bulk-memory/table-sub.wast` | 2 / 2 | 1 / 1 | Agreed |
| `core/bulk-memory/table_copy.wast` | 1675 / 1675 | 52 / 52 | Agreed |
| `core/bulk-memory/table_fill.wast` | 44 / 44 | 1 / 1 | Agreed |
| `core/bulk-memory/table_init.wast` | 750 / 750 | 41 / 41 | Agreed |
| `core/call.wast` | 90 / 90 | 1 / 1 | Agreed |
| `core/call_indirect.wast` | 169 / 169 | 3 / 3 | Agreed |
| `core/call_ref.wast` | 31 / 31 | 4 / 4 | Agreed |
| `core/comments.wast` | 3 / 3 | 5 / 5 | Agreed |
| `core/const.wast` | 376 / 376 | 402 / 402 | Agreed |
| `core/conversions.wast` | 618 / 618 | 1 / 1 | Agreed |
| `core/custom.wast` | 8 / 8 | 3 / 3 | Agreed |
| `core/data.wast` | 34 / 34 | 31 / 31 | Agreed |
| `core/elem.wast` | 72 / 72 | 76 / 76 | Agreed |
| `core/endianness.wast` | 68 / 68 | 1 / 1 | Agreed |
| `core/exceptions/tag.wast` | 4 / 4 | 4 / 4 | Agreed |
| `core/exceptions/throw.wast` | 12 / 12 | 1 / 1 | Agreed |
| `core/exceptions/throw_ref.wast` | 14 / 14 | 1 / 1 | Agreed |
| `core/exceptions/try_table.wast` | 60 / 60 | 6 / 6 | Agreed |
| `core/exports.wast` | 41 / 41 | 56 / 56 | Agreed |
| `core/f32.wast` | 2513 / 2513 | 1 / 1 | Agreed |
| `core/f32_bitwise.wast` | 363 / 363 | 1 / 1 | Agreed |
| `core/f32_cmp.wast` | 2406 / 2406 | 1 / 1 | Agreed |
| `core/f64.wast` | 2513 / 2513 | 1 / 1 | Agreed |
| `core/f64_bitwise.wast` | 363 / 363 | 1 / 1 | Agreed |
| `core/f64_cmp.wast` | 2406 / 2406 | 1 / 1 | Agreed |
| `core/fac.wast` | 7 / 7 | 1 / 1 | Agreed |
| `core/float_exprs.wast` | 829 / 829 | 98 / 98 | Agreed |
| `core/float_literals.wast` | 177 / 177 | 2 / 2 | Agreed |
| `core/float_memory.wast` | 84 / 84 | 6 / 6 | Agreed |
| `core/float_misc.wast` | 470 / 470 | 1 / 1 | Agreed |
| `core/forward.wast` | 4 / 4 | 1 / 1 | Agreed |
| `core/func.wast` | 171 / 171 | 4 / 4 | Agreed |
| `core/func_ptrs.wast` | 33 / 33 | 3 / 3 | Agreed |
| `core/gc/array.wast` | 47 / 47 | 7 / 7 | Agreed |
| `core/gc/array_copy.wast` | 34 / 34 | 1 / 1 | Agreed |
| `core/gc/array_fill.wast` | 29 / 29 | 1 / 1 | Agreed |
| `core/gc/array_init_data.wast` | 44 / 44 | 2 / 2 | Agreed |
| `core/gc/array_init_elem.wast` | 33 / 33 | 3 / 3 | Agreed |
| `core/gc/array_new_data.wast` | 23 / 23 | 5 / 5 | Agreed |
| `core/gc/array_new_elem.wast` | 19 / 19 | 5 / 5 | Agreed |
| `core/gc/binary-gc.wast` | 1 / 1 | 0 / 0 | Agreed |
| `core/gc/br_on_cast.wast` | 34 / 34 | 3 / 3 | Agreed |
| `core/gc/br_on_cast_fail.wast` | 34 / 34 | 3 / 3 | Agreed |
| `core/gc/extern.wast` | 17 / 17 | 1 / 1 | Agreed |
| `core/gc/i31.wast` | 65 / 65 | 7 / 7 | Agreed |
| `core/gc/ref_cast.wast` | 43 / 43 | 2 / 2 | Agreed |
| `core/gc/ref_eq.wast` | 88 / 88 | 1 / 1 | Agreed |
| `core/gc/ref_test.wast` | 69 / 69 | 2 / 2 | Agreed |
| `core/gc/struct.wast` | 24 / 24 | 6 / 6 | Agreed |
| `core/gc/type-subtyping.wast` | 73 / 73 | 46 / 46 | Agreed |
| `core/global.wast` | 114 / 114 | 9 / 9 | Agreed |
| `core/i32.wast` | 459 / 459 | 1 / 1 | Agreed |
| `core/i64.wast` | 415 / 415 | 1 / 1 | Agreed |
| `core/id.wast` | 6 / 6 | 1 / 1 | Agreed |
| `core/if.wast` | 240 / 240 | 1 / 1 | Agreed |
| `core/imports.wast` | 144 / 144 | 68 / 68 | Agreed |
| `core/inline-module.wast` | 0 / 0 | 0 / 0 | Agreed |
| `core/instance.wast` | 12 / 12 | 6 / 6 | Agreed |
| `core/int_exprs.wast` | 89 / 89 | 19 / 19 | Agreed |
| `core/int_literals.wast` | 50 / 50 | 1 / 1 | Agreed |
| `core/labels.wast` | 28 / 28 | 1 / 1 | Agreed |
| `core/left-to-right.wast` | 95 / 95 | 1 / 1 | Agreed |
| `core/linking.wast` | 133 / 133 | 21 / 21 | Agreed |
| `core/load.wast` | 96 / 96 | 1 / 1 | Agreed |
| `core/local_get.wast` | 35 / 35 | 1 / 1 | Agreed |
| `core/local_init.wast` | 8 / 8 | 2 / 2 | Agreed |
| `core/local_set.wast` | 52 / 52 | 1 / 1 | Agreed |
| `core/local_tee.wast` | 97 / 97 | 1 / 1 | Agreed |
| `core/loop.wast` | 120 / 120 | 1 / 1 | Agreed |
| `core/memory.wast` | 78 / 78 | 11 / 11 | Agreed |
| `core/memory64/address64.wast` | 238 / 238 | 4 / 4 | Agreed |
| `core/memory64/align64.wast` | 131 / 131 | 26 / 26 | Agreed |
| `core/memory64/binary_leb128_64.wast` | 1 / 1 | 1 / 1 | Agreed |
| `core/memory64/bulk64.wast` | 65 / 65 | 5 / 5 | Agreed |
| `core/memory64/call_indirect64.wast` | 1 / 1 | 1 / 1 | Agreed |
| `core/memory64/endianness64.wast` | 68 / 68 | 1 / 1 | Agreed |
| `core/memory64/float_memory64.wast` | 84 / 84 | 6 / 6 | Agreed |
| `core/memory64/load64.wast` | 96 / 96 | 1 / 1 | Agreed |
| `core/memory64/memory64-imports.wast` | 30 / 30 | 40 / 40 | Agreed |
| `core/memory64/memory64.wast` | 59 / 59 | 9 / 9 | Agreed |
| `core/memory64/memory_copy64.wast` | 4417 / 4417 | 33 / 33 | Agreed |
| `core/memory64/memory_fill64.wast` | 89 / 89 | 11 / 11 | Agreed |
| `core/memory64/memory_grow64.wast` | 45 / 45 | 4 / 4 | Agreed |
| `core/memory64/memory_init64.wast` | 221 / 221 | 29 / 29 | Agreed |
| `core/memory64/memory_redundancy64.wast` | 7 / 7 | 1 / 1 | Agreed |
| `core/memory64/memory_trap64.wast` | 170 / 170 | 2 / 2 | Agreed |
| `core/memory64/table64.wast` | 2 / 2 | 11 / 11 | Agreed |
| `core/memory64/table_copy64.wast` | 1675 / 1675 | 52 / 52 | Agreed |
| `core/memory64/table_copy_mixed.wast` | 3 / 3 | 1 / 1 | Agreed |
| `core/memory64/table_fill64.wast` | 79 / 79 | 1 / 1 | Agreed |
| `core/memory64/table_get64.wast` | 10 / 10 | 1 / 1 | Agreed |
| `core/memory64/table_grow64.wast` | 21 / 21 | 1 / 1 | Agreed |
| `core/memory64/table_init64.wast` | 843 / 843 | 44 / 44 | Agreed |
| `core/memory64/table_set64.wast` | 18 / 18 | 1 / 1 | Agreed |
| `core/memory64/table_size64.wast` | 36 / 36 | 1 / 1 | Agreed |
| `core/memory_grow.wast` | 96 / 96 | 8 / 8 | Agreed |
| `core/memory_redundancy.wast` | 7 / 7 | 1 / 1 | Agreed |
| `core/memory_size.wast` | 38 / 38 | 4 / 4 | Agreed |
| `core/memory_trap.wast` | 180 / 180 | 2 / 2 | Agreed |
| `core/multi-memory/address0.wast` | 91 / 91 | 1 / 1 | Agreed |
| `core/multi-memory/address1.wast` | 126 / 126 | 1 / 1 | Agreed |
| `core/multi-memory/align0.wast` | 4 / 4 | 1 / 1 | Agreed |
| `core/multi-memory/binary0.wast` | 2 / 2 | 5 / 5 | Agreed |
| `core/multi-memory/data0.wast` | 0 / 0 | 7 / 7 | Agreed |
| `core/multi-memory/data1.wast` | 14 / 14 | 0 / 0 | Agreed |
| `core/multi-memory/data_drop0.wast` | 10 / 10 | 1 / 1 | Agreed |
| `core/multi-memory/exports0.wast` | 0 / 0 | 8 / 8 | Agreed |
| `core/multi-memory/float_exprs0.wast` | 13 / 13 | 1 / 1 | Agreed |
| `core/multi-memory/float_exprs1.wast` | 2 / 2 | 1 / 1 | Agreed |
| `core/multi-memory/float_memory0.wast` | 28 / 28 | 2 / 2 | Agreed |
| `core/multi-memory/imports0.wast` | 6 / 6 | 1 / 1 | Agreed |
| `core/multi-memory/imports1.wast` | 4 / 4 | 1 / 1 | Agreed |
| `core/multi-memory/imports2.wast` | 14 / 14 | 5 / 5 | Agreed |
| `core/multi-memory/imports3.wast` | 8 / 8 | 1 / 1 | Agreed |
| `core/multi-memory/imports4.wast` | 8 / 8 | 5 / 5 | Agreed |
| `core/multi-memory/linking0.wast` | 4 / 4 | 1 / 1 | Agreed |
| `core/multi-memory/linking1.wast` | 9 / 9 | 4 / 4 | Agreed |
| `core/multi-memory/linking2.wast` | 8 / 8 | 2 / 2 | Agreed |
| `core/multi-memory/linking3.wast` | 10 / 10 | 2 / 2 | Agreed |
| `core/multi-memory/load0.wast` | 2 / 2 | 1 / 1 | Agreed |
| `core/multi-memory/load1.wast` | 15 / 15 | 2 / 2 | Agreed |
| `core/multi-memory/load2.wast` | 37 / 37 | 1 / 1 | Agreed |
| `core/multi-memory/memory-multi.wast` | 4 / 4 | 2 / 2 | Agreed |
| `core/multi-memory/memory_copy0.wast` | 28 / 28 | 1 / 1 | Agreed |
| `core/multi-memory/memory_copy1.wast` | 13 / 13 | 1 / 1 | Agreed |
| `core/multi-memory/memory_fill0.wast` | 15 / 15 | 1 / 1 | Agreed |
| `core/multi-memory/memory_grow.wast` | 47 / 47 | 3 / 3 | Agreed |
| `core/multi-memory/memory_init0.wast` | 12 / 12 | 1 / 1 | Agreed |
| `core/multi-memory/memory_size0.wast` | 7 / 7 | 1 / 1 | Agreed |
| `core/multi-memory/memory_size1.wast` | 14 / 14 | 1 / 1 | Agreed |
| `core/multi-memory/memory_size2.wast` | 20 / 20 | 1 / 1 | Agreed |
| `core/multi-memory/memory_size3.wast` | 2 / 2 | 0 / 0 | Agreed |
| `core/multi-memory/memory_size_import.wast` | 4 / 4 | 2 / 2 | Agreed |
| `core/multi-memory/memory_trap0.wast` | 13 / 13 | 1 / 1 | Agreed |
| `core/multi-memory/memory_trap1.wast` | 167 / 167 | 1 / 1 | Agreed |
| `core/multi-memory/start0.wast` | 8 / 8 | 1 / 1 | Agreed |
| `core/multi-memory/store0.wast` | 4 / 4 | 1 / 1 | Agreed |
| `core/multi-memory/store1.wast` | 8 / 8 | 3 / 3 | Agreed |
| `core/multi-memory/store2.wast` | 22 / 22 | 2 / 2 | Agreed |
| `core/multi-memory/traps0.wast` | 14 / 14 | 1 / 1 | Agreed |
| `core/names.wast` | 482 / 482 | 4 / 4 | Agreed |
| `core/nop.wast` | 87 / 87 | 1 / 1 | Agreed |
| `core/obsolete-keywords.wast` | 11 / 11 | 0 / 0 | Agreed |
| `core/ref.wast` | 12 / 12 | 1 / 1 | Agreed |
| `core/ref_as_non_null.wast` | 5 / 5 | 2 / 2 | Agreed |
| `core/ref_func.wast` | 13 / 13 | 3 / 3 | Agreed |
| `core/ref_is_null.wast` | 20 / 20 | 2 / 2 | Agreed |
| `core/ref_null.wast` | 32 / 32 | 2 / 2 | Agreed |
| `core/relaxed-simd/i16x8_relaxed_q15mulr_s.wast` | 2 / 2 | 1 / 1 | Agreed |
| `core/relaxed-simd/i32x4_relaxed_trunc.wast` | 0 / 0 | 1 / 1 | Agreed |
| `core/relaxed-simd/i8x16_relaxed_swizzle.wast` | 5 / 5 | 1 / 1 | Agreed |
| `core/relaxed-simd/relaxed_dot_product.wast` | 10 / 10 | 1 / 1 | Agreed |
| `core/relaxed-simd/relaxed_laneselect.wast` | 11 / 11 | 1 / 1 | Agreed |
| `core/relaxed-simd/relaxed_madd_nmadd.wast` | 17 / 17 | 2 / 2 | Agreed |
| `core/relaxed-simd/relaxed_min_max.wast` | 24 / 24 | 1 / 1 | Agreed |
| `core/return.wast` | 83 / 83 | 1 / 1 | Agreed |
| `core/return_call.wast` | 46 / 46 | 3 / 3 | Agreed |
| `core/return_call_indirect.wast` | 78 / 78 | 3 / 3 | Agreed |
| `core/return_call_ref.wast` | 46 / 46 | 5 / 5 | Agreed |
| `core/select.wast` | 154 / 154 | 3 / 3 | Agreed |
| `core/simd/simd_address.wast` | 46 / 46 | 3 / 3 | Agreed |
| `core/simd/simd_align.wast` | 54 / 54 | 46 / 46 | Agreed |
| `core/simd/simd_bit_shift.wast` | 250 / 250 | 2 / 2 | Agreed |
| `core/simd/simd_bitwise.wast` | 167 / 167 | 2 / 2 | Agreed |
| `core/simd/simd_boolean.wast` | 275 / 275 | 2 / 2 | Agreed |
| `core/simd/simd_const.wast` | 446 / 446 | 312 / 312 | Agreed |
| `core/simd/simd_conversions.wast` | 280 / 280 | 2 / 2 | Agreed |
| `core/simd/simd_f32x4.wast` | 788 / 788 | 2 / 2 | Agreed |
| `core/simd/simd_f32x4_arith.wast` | 1819 / 1819 | 3 / 3 | Agreed |
| `core/simd/simd_f32x4_cmp.wast` | 2605 / 2605 | 2 / 2 | Agreed |
| `core/simd/simd_f32x4_pmin_pmax.wast` | 3886 / 3886 | 1 / 1 | Agreed |
| `core/simd/simd_f32x4_rounding.wast` | 200 / 200 | 1 / 1 | Agreed |
| `core/simd/simd_f64x2.wast` | 801 / 801 | 2 / 2 | Agreed |
| `core/simd/simd_f64x2_arith.wast` | 1822 / 1822 | 3 / 3 | Agreed |
| `core/simd/simd_f64x2_cmp.wast` | 2683 / 2683 | 2 / 2 | Agreed |
| `core/simd/simd_f64x2_pmin_pmax.wast` | 3886 / 3886 | 1 / 1 | Agreed |
| `core/simd/simd_f64x2_rounding.wast` | 200 / 200 | 1 / 1 | Agreed |
| `core/simd/simd_i16x8_arith.wast` | 192 / 192 | 2 / 2 | Agreed |
| `core/simd/simd_i16x8_arith2.wast` | 170 / 170 | 2 / 2 | Agreed |
| `core/simd/simd_i16x8_cmp.wast` | 463 / 463 | 2 / 2 | Agreed |
| `core/simd/simd_i16x8_extadd_pairwise_i8x16.wast` | 20 / 20 | 1 / 1 | Agreed |
| `core/simd/simd_i16x8_extmul_i8x16.wast` | 116 / 116 | 1 / 1 | Agreed |
| `core/simd/simd_i16x8_q15mulr_sat_s.wast` | 29 / 29 | 1 / 1 | Agreed |
| `core/simd/simd_i16x8_sat_arith.wast` | 220 / 220 | 2 / 2 | Agreed |
| `core/simd/simd_i32x4_arith.wast` | 192 / 192 | 2 / 2 | Agreed |
| `core/simd/simd_i32x4_arith2.wast` | 147 / 147 | 2 / 2 | Agreed |
| `core/simd/simd_i32x4_cmp.wast` | 473 / 473 | 2 / 2 | Agreed |
| `core/simd/simd_i32x4_dot_i16x8.wast` | 31 / 31 | 1 / 1 | Agreed |
| `core/simd/simd_i32x4_extadd_pairwise_i16x8.wast` | 20 / 20 | 1 / 1 | Agreed |
| `core/simd/simd_i32x4_extmul_i16x8.wast` | 116 / 116 | 1 / 1 | Agreed |
| `core/simd/simd_i32x4_trunc_sat_f32x4.wast` | 106 / 106 | 1 / 1 | Agreed |
| `core/simd/simd_i32x4_trunc_sat_f64x2.wast` | 106 / 106 | 1 / 1 | Agreed |
| `core/simd/simd_i64x2_arith.wast` | 198 / 198 | 2 / 2 | Agreed |
| `core/simd/simd_i64x2_arith2.wast` | 23 / 23 | 2 / 2 | Agreed |
| `core/simd/simd_i64x2_cmp.wast` | 112 / 112 | 1 / 1 | Agreed |
| `core/simd/simd_i64x2_extmul_i32x4.wast` | 116 / 116 | 1 / 1 | Agreed |
| `core/simd/simd_i8x16_arith.wast` | 129 / 129 | 2 / 2 | Agreed |
| `core/simd/simd_i8x16_arith2.wast` | 209 / 209 | 2 / 2 | Agreed |
| `core/simd/simd_i8x16_cmp.wast` | 443 / 443 | 2 / 2 | Agreed |
| `core/simd/simd_i8x16_sat_arith.wast` | 212 / 212 | 2 / 2 | Agreed |
| `core/simd/simd_int_to_int_extend.wast` | 252 / 252 | 1 / 1 | Agreed |
| `core/simd/simd_lane.wast` | 463 / 463 | 12 / 12 | Agreed |
| `core/simd/simd_linking.wast` | 0 / 0 | 2 / 2 | Agreed |
| `core/simd/simd_load.wast` | 25 / 25 | 14 / 14 | Agreed |
| `core/simd/simd_load16_lane.wast` | 35 / 35 | 1 / 1 | Agreed |
| `core/simd/simd_load32_lane.wast` | 23 / 23 | 1 / 1 | Agreed |
| `core/simd/simd_load64_lane.wast` | 15 / 15 | 1 / 1 | Agreed |
| `core/simd/simd_load8_lane.wast` | 51 / 51 | 1 / 1 | Agreed |
| `core/simd/simd_load_extend.wast` | 102 / 102 | 2 / 2 | Agreed |
| `core/simd/simd_load_splat.wast` | 124 / 124 | 2 / 2 | Agreed |
| `core/simd/simd_load_zero.wast` | 37 / 37 | 2 / 2 | Agreed |
| `core/simd/simd_memory-multi.wast` | 0 / 0 | 1 / 1 | Agreed |
| `core/simd/simd_select.wast` | 6 / 6 | 1 / 1 | Agreed |
| `core/simd/simd_splat.wast` | 181 / 181 | 4 / 4 | Agreed |
| `core/simd/simd_store.wast` | 26 / 26 | 2 / 2 | Agreed |
| `core/simd/simd_store16_lane.wast` | 35 / 35 | 1 / 1 | Agreed |
| `core/simd/simd_store32_lane.wast` | 23 / 23 | 1 / 1 | Agreed |
| `core/simd/simd_store64_lane.wast` | 15 / 15 | 1 / 1 | Agreed |
| `core/simd/simd_store8_lane.wast` | 51 / 51 | 1 / 1 | Agreed |
| `core/skip-stack-guard-page.wast` | 10 / 10 | 1 / 1 | Agreed |
| `core/stack.wast` | 5 / 5 | 2 / 2 | Agreed |
| `core/start.wast` | 15 / 15 | 5 / 5 | Agreed |
| `core/store.wast` | 67 / 67 | 1 / 1 | Agreed |
| `core/switch.wast` | 27 / 27 | 1 / 1 | Agreed |
| `core/table.wast` | 27 / 27 | 17 / 17 | Agreed |
| `core/table_get.wast` | 15 / 15 | 1 / 1 | Agreed |
| `core/table_grow.wast` | 48 / 48 | 8 / 8 | Agreed |
| `core/table_set.wast` | 25 / 25 | 1 / 1 | Agreed |
| `core/table_size.wast` | 38 / 38 | 1 / 1 | Agreed |
| `core/token.wast` | 26 / 26 | 35 / 35 | Agreed |
| `core/traps.wast` | 32 / 32 | 4 / 4 | Agreed |
| `core/type-canon.wast` | 0 / 0 | 2 / 2 | Agreed |
| `core/type-equivalence.wast` | 5 / 5 | 21 / 21 | Agreed |
| `core/type-rec.wast` | 15 / 15 | 11 / 11 | Agreed |
| `core/type.wast` | 2 / 2 | 1 / 1 | Agreed |
| `core/unreachable.wast` | 63 / 63 | 1 / 1 | Agreed |
| `core/unreached-invalid.wast` | 121 / 121 | 0 / 0 | Agreed |
| `core/unreached-valid.wast` | 10 / 10 | 3 / 3 | Agreed |
| `core/unwind.wast` | 49 / 49 | 1 / 1 | Agreed |
| `core/utf8-custom-section-id.wast` | 176 / 176 | 0 / 0 | Agreed |
| `core/utf8-import-field.wast` | 176 / 176 | 0 / 0 | Agreed |
| `core/utf8-import-module.wast` | 176 / 176 | 0 / 0 | Agreed |
| `core/utf8-invalid-encoding.wast` | 176 / 176 | 0 / 0 | Agreed |
| `custom/custom/custom_annot.wast` | 14 / 14 | 3 / 3 | Agreed |
| `custom/metadata.code.branch_hint/branch_hint.wast` | 3 / 3 | 1 / 1 | Agreed |
| `custom/name/name_annot.wast` | 3 / 3 | 4 / 4 | Agreed |
| `legacy/exceptions/core/rethrow.wast` | — | — | Excluded: legacy syntax |
| `legacy/exceptions/core/throw.wast` | — | — | Excluded: legacy syntax |
| `legacy/exceptions/core/try_catch.wast` | — | — | Excluded: legacy syntax |
| `legacy/exceptions/core/try_delegate.wast` | — | — | Excluded: legacy syntax |
