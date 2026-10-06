;; select-runtime-client.wast.inc — Wrapper behavior shared with the OCaml
;; oracle through the generated waste_kernel reference provider.
(register "waste-libc" $waste_libc)
(module $select_runtime_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "__errno_location" (func $errno (result i32)))
  (import "waste-libc" "select"
    (func $select (param i32 i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "pselect"
    (func $pselect (param i32 i32 i32 i32 i32 i32) (result i32)))
  (data (i32.const 65536) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")

  (func (export "select-zero") (result i32)
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 65536
    call $select)

  (func (export "select-einval") (result i32)
    i32.const -1 i32.const 0 i32.const 0 i32.const 0 i32.const 0
    call $select
    i32.const -1 i32.eq
    call $errno i32.load i32.const 22 i32.eq
    i32.and)

  (func (export "pselect-zero") (result i32)
    i32.const 0 i32.const 0 i32.const 0 i32.const 0
    i32.const 65536 i32.const 0 call $pselect)

  (func (export "pselect-einval") (result i32)
    i32.const -1 i32.const 0 i32.const 0 i32.const 0
    i32.const 65536 i32.const 0 call $pselect
    i32.const -1 i32.eq
    call $errno i32.load i32.const 22 i32.eq
    i32.and))

(assert_return (invoke $select_runtime_tests "select-zero") (i32.const 0))
(assert_return (invoke $select_runtime_tests "select-einval") (i32.const 1))
(assert_return (invoke $select_runtime_tests "pselect-zero") (i32.const 0))
(assert_return (invoke $select_runtime_tests "pselect-einval") (i32.const 1))
