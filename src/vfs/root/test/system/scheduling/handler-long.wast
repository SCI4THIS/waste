;; Concurrent handlers deliberately reuse module IDs and registrations.
(module $same
  (import "env" "select" (func $select (param i32 i32 i32 i32 i32) (result i32)))
  (memory 1)
  (data (i32.const 128) "\00\00\00\00\00\00\00\00\e0\93\04\00")
  (func (export "sleep") (result i32)
    (call $select (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 128)))
  (func (export "value") (result i32) (i32.const 11)))
(register "same" $same)
(assert_return (invoke $same "sleep") (i32.const 0))
;; A sibling must not replace this process's current module or registration.
(assert_return (invoke "value") (i32.const 11))
(module
  (import "same" "value" (func $value (result i32)))
  (func (export "linked") (result i32) (call $value)))
(assert_return (invoke "linked") (i32.const 11))
