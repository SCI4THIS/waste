;; Real shared-libc modf: exact truncation, signed fractional/integer results,
;; no narrowing, host rounding dependency or errno changes.
(module $modf
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "modf" (func $modf (param f64 i32) (result f64)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (func $prepare
    i32.const 180000 i64.const 0x123456789abcdef0 i64.store
    i32.const 180008 i64.const -1 i64.store
    i32.const 180016 i64.const 0x23456789abcdef01 i64.store
    call $errno i32.const 17 i32.store)
  (func $unchanged (result i32)
    i32.const 180000 i64.load i64.const 0x123456789abcdef0 i64.eq
    i32.const 180016 i64.load i64.const 0x23456789abcdef01 i64.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func (export "split") (param $value f64) (param $integer i64) (param $fraction i64) (result i32)
    call $prepare
    local.get $value i32.const 180008 call $modf i64.reinterpret_f64
    local.get $fraction i64.eq
    i32.const 180008 i64.load local.get $integer i64.eq i32.and
    call $unchanged i32.and)
  (func (export "nan") (param $value f64) (result i32)
    (local $fraction f64)
    call $prepare
    local.get $value i32.const 180008 call $modf local.set $fraction
    local.get $fraction local.get $fraction f64.ne
    i32.const 180008 f64.load i32.const 180008 f64.load f64.ne i32.and
    call $unchanged i32.and)
)
(assert_return (invoke $modf "split" (f64.const 3.75) (i64.const 0x4008000000000000) (i64.const 0x3fe8000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -3.75) (i64.const 0xc008000000000000) (i64.const 0xbfe8000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 0.25) (i64.const 0x0000000000000000) (i64.const 0x3fd0000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -0.25) (i64.const 0x8000000000000000) (i64.const 0xbfd0000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 0) (i64.const 0x0000000000000000) (i64.const 0x0000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -0) (i64.const 0x8000000000000000) (i64.const 0x8000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 1) (i64.const 0x3ff0000000000000) (i64.const 0x0000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -1) (i64.const 0xbff0000000000000) (i64.const 0x8000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const inf) (i64.const 0x7ff0000000000000) (i64.const 0x0000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -inf) (i64.const 0xfff0000000000000) (i64.const 0x8000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 0x1p-1074) (i64.const 0x0000000000000000) (i64.const 0x0000000000000001)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -0x1p-1074) (i64.const 0x8000000000000000) (i64.const 0x8000000000000001)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 0x1.fffffffffffffp51) (i64.const 0x432ffffffffffffe) (i64.const 0x3fe0000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -0x1.fffffffffffffp51) (i64.const 0xc32ffffffffffffe) (i64.const 0xbfe0000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 0x1p52) (i64.const 0x4330000000000000) (i64.const 0x0000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -0x1p52) (i64.const 0xc330000000000000) (i64.const 0x8000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const 0x1.fffffffffffffp1023) (i64.const 0x7fefffffffffffff) (i64.const 0x0000000000000000)) (i32.const 1))
(assert_return (invoke $modf "split" (f64.const -0x1.fffffffffffffp1023) (i64.const 0xffefffffffffffff) (i64.const 0x8000000000000000)) (i32.const 1))
(assert_return (invoke $modf "nan" (f64.const nan:0x8123456789abc)) (i32.const 1))
(assert_return (invoke $modf "nan" (f64.const -nan:0x8123456789abc)) (i32.const 1))
