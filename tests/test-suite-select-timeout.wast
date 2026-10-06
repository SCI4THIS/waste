;; Timed waits must resume without an external terminal event, and keep their
;; expected result across the explicit browser return/resume boundary.
(module
  (import "env" "select" (func $select (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "pselect" (func $pselect (param i32 i32 i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "timed-select") (result i32)
    i32.const 64 i64.const 0 i64.store
    i32.const 72 i64.const 30000 i64.store
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 64 call $select)
  (func (export "timed-pselect") (result i32)
    i32.const 64 i64.const 0 i64.store
    i32.const 72 i64.const 30000000 i64.store
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 64 i32.const 0 call $pselect))
(assert_return (invoke "timed-select") (i32.const 0))
(assert_return (invoke "timed-pselect") (i32.const 0))
