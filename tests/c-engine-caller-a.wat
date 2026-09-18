(module
  (import "host" "read_caller_mem" (func $read (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "\de\ad\be\ef")

  ;; Store a value and read it back through the host function
  (func (export "store_and_read") (param $val i32) (result i32)
    (i32.store (i32.const 16) (local.get $val))
    (call $read (i32.const 16) (i32.const 4)))

  ;; Read the known data at offset 0
  (func (export "read_initial") (result i32)
    (call $read (i32.const 0) (i32.const 4)))
)
