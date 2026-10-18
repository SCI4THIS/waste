;; Create mutable kernel state that the next independent script cannot see.
(module
  (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "close" (func $close (param i32) (result i32)))
  (memory 1)
  (data (i32.const 0) "/tmp/suite-leak\00")
  (func (export "create") (result i32)
    (call $close (call $open (i32.const 0) (i32.const 65) (i32.const 384)))))
(assert_return (invoke "create") (i32.const 0))
