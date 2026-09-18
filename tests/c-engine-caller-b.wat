(module
  (import "host" "read_caller_mem" (func $read (param i32 i32) (result i32)))
  (import "modA" "store_and_read" (func $a_store_and_read (param i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "\ca\fe\ba\be")

  ;; Direct: host sees module B's memory
  (func (export "read_own") (result i32)
    (call $read (i32.const 0) (i32.const 4)))

  ;; Nested: call into module A, which calls the host function.
  ;; The host should see A's memory, not B's.
  (func (export "nested_through_a") (param $val i32) (result i32)
    (call $a_store_and_read (local.get $val)))
)
