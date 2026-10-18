;; Batch sandboxes cannot launch another privileged host batch.
(module
  (import "waste_kernel" "test_suite_v1" (func $suite (param i32 i32 i32 i32) (result i32)))
  (memory 1)
  (data (i32.const 0) "\01\00\00\00")
  (func (export "nested") (result i32)
    (call $suite (i32.const 0) (i32.const 4) (i32.const 1024) (i32.const 1024))))
(assert_return (invoke "nested") (i32.const -1))
