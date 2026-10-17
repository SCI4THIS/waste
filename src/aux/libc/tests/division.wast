;; Aggregate C returns use a hidden destination pointer in the Wasm ABI.
(module $division
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "div" (func $div (param i32 i32 i32)))
  (import "libc" "ldiv" (func $ldiv (param i32 i32 i32)))
  (import "libc" "lldiv" (func $lldiv (param i32 i64 i64)))
  (import "libc" "llabs" (func $llabs (param i64) (result i64)))
  (export "llabs" (func $llabs))
  (func (export "negative-dividend") (result i32)
    i32.const 180000 i32.const -17 i32.const 5 call $div
    i32.const 180000 i32.load i32.const -3 i32.eq
    i32.const 180004 i32.load i32.const -2 i32.eq i32.and)
  (func (export "negative-divisor") (result i32)
    i32.const 180000 i32.const 17 i32.const -5 call $ldiv
    i32.const 180000 i32.load i32.const -3 i32.eq
    i32.const 180004 i32.load i32.const 2 i32.eq i32.and)
  (func (export "wide-division") (result i32)
    i32.const 180000 i64.const -4294967297 i64.const 2 call $lldiv
    i32.const 180000 i64.load i64.const -2147483648 i64.eq
    i32.const 180008 i64.load i64.const -1 i64.eq i32.and))
(assert_return (invoke $division "negative-dividend") (i32.const 1))
(assert_return (invoke $division "negative-divisor") (i32.const 1))
(assert_return (invoke $division "wide-division") (i32.const 1))
(assert_return (invoke $division "llabs" (i64.const -4294967297)) (i64.const 4294967297))
