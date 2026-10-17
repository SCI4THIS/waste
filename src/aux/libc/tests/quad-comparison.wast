;; Exact binary128 compiler comparison helpers from the installed libc.
(module $quad
  (import "libc" "__eqtf2" (func $eq (param i64 i64 i64 i64) (result i32)))
  (import "libc" "__netf2" (func $ne (param i64 i64 i64 i64) (result i32)))
  (import "libc" "__letf2" (func $le (param i64 i64 i64 i64) (result i32)))
  (import "libc" "__lttf2" (func $lt (param i64 i64 i64 i64) (result i32)))
  (import "libc" "__getf2" (func $ge (param i64 i64 i64 i64) (result i32)))
  (import "libc" "__gttf2" (func $gt (param i64 i64 i64 i64) (result i32)))
  (import "libc" "__unordtf2" (func $unord (param i64 i64 i64 i64) (result i32)))
  (func (export "check") (param $al i64) (param $ah i64) (param $bl i64) (param $bh i64)
                       (param $expected i32) (result i32)
    (local $le i32) (local $ge i32)
    local.get $expected local.set $le
    local.get $expected local.set $ge
    local.get $expected i32.const 2 i32.eq
    if i32.const 1 local.set $le i32.const -1 local.set $ge end
    local.get $al local.get $ah local.get $bl local.get $bh call $eq
    local.get $le i32.eq
    local.get $al local.get $ah local.get $bl local.get $bh call $ne
    local.get $le i32.eq i32.and
    local.get $al local.get $ah local.get $bl local.get $bh call $le
    local.get $le i32.eq i32.and
    local.get $al local.get $ah local.get $bl local.get $bh call $lt
    local.get $le i32.eq i32.and
    local.get $al local.get $ah local.get $bl local.get $bh call $ge
    local.get $ge i32.eq i32.and
    local.get $al local.get $ah local.get $bl local.get $bh call $gt
    local.get $ge i32.eq i32.and
    local.get $al local.get $ah local.get $bl local.get $bh call $unord
    local.get $expected i32.const 2 i32.eq i32.eq i32.and
  )
)
;; signed zeros
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i64.const 0x8000000000000000) (i32.const 0)) (i32.const 1))
;; reversed zeros
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x8000000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i32.const 0)) (i32.const 1))
;; zero versus subnormal
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000001) (i64.const 0x0000000000000000) (i32.const -1)) (i32.const 1))
;; subnormal versus zero
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i32.const 1)) (i32.const 1))
;; negative subnormal versus zero
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0x8000000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i32.const -1)) (i32.const 1))
;; zero versus negative subnormal
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000001) (i64.const 0x8000000000000000) (i32.const 1)) (i32.const 1))
;; adjacent subnormals
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0x0000000000000000) (i64.const 0x0000000000000002) (i64.const 0x0000000000000000) (i32.const -1)) (i32.const 1))
;; negative adjacent subnormals
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0x8000000000000000) (i64.const 0x0000000000000002) (i64.const 0x8000000000000000) (i32.const 1)) (i32.const 1))
;; adjacent normal values
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x3fff000000000000) (i64.const 0x0000000000000001) (i64.const 0x3fff000000000000) (i32.const -1)) (i32.const 1))
;; reversed adjacent normals
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0x3fff000000000000) (i64.const 0x0000000000000000) (i64.const 0x3fff000000000000) (i32.const 1)) (i32.const 1))
;; negative adjacent normals
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0xbfff000000000000) (i64.const 0x0000000000000001) (i64.const 0xbfff000000000000) (i32.const 1)) (i32.const 1))
;; reversed negative normals
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0xbfff000000000000) (i64.const 0x0000000000000000) (i64.const 0xbfff000000000000) (i32.const -1)) (i32.const 1))
;; maximum finite versus infinity
(assert_return (invoke $quad "check" (i64.const 0xffffffffffffffff) (i64.const 0x7ffeffffffffffff) (i64.const 0x0000000000000000) (i64.const 0x7fff000000000000) (i32.const -1)) (i32.const 1))
;; infinity versus maximum finite
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x7fff000000000000) (i64.const 0xffffffffffffffff) (i64.const 0x7ffeffffffffffff) (i32.const 1)) (i32.const 1))
;; opposite infinities
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x7fff000000000000) (i64.const 0x0000000000000000) (i64.const 0xffff000000000000) (i32.const 1)) (i32.const 1))
;; negative infinity versus negative finite
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0xffff000000000000) (i64.const 0xffffffffffffffff) (i64.const 0xfffeffffffffffff) (i32.const -1)) (i32.const 1))
;; negative finite versus negative infinity
(assert_return (invoke $quad "check" (i64.const 0xffffffffffffffff) (i64.const 0xfffeffffffffffff) (i64.const 0x0000000000000000) (i64.const 0xffff000000000000) (i32.const 1)) (i32.const 1))
;; equal infinities
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x7fff000000000000) (i64.const 0x0000000000000000) (i64.const 0x7fff000000000000) (i32.const 0)) (i32.const 1))
;; normal boundary versus largest subnormal
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x0001000000000000) (i64.const 0xffffffffffffffff) (i64.const 0x0000ffffffffffff) (i32.const 1)) (i32.const 1))
;; positive NaN
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x7fff800000000000) (i64.const 0x0000000000000000) (i64.const 0x3fff000000000000) (i32.const 2)) (i32.const 1))
;; negative NaN
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0xffff800000000000) (i64.const 0x0000000000000000) (i64.const 0x3fff000000000000) (i32.const 2)) (i32.const 1))
;; right-hand NaN
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x3fff000000000000) (i64.const 0x0000000000000000) (i64.const 0x7fff800000000000) (i32.const 2)) (i32.const 1))
;; low-word signaling NaN
(assert_return (invoke $quad "check" (i64.const 0x0000000000000001) (i64.const 0x7fff000000000000) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000) (i32.const 2)) (i32.const 1))
;; two NaNs
(assert_return (invoke $quad "check" (i64.const 0x0000000000000000) (i64.const 0x7fff800000000000) (i64.const 0x0000000000000000) (i64.const 0xffff800000000000) (i32.const 2)) (i32.const 1))
