(module
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 64) "WAT_OK\0a")
  (func (export "_start")
    (drop (call $write (i32.const 1) (i32.const 64) (i32.const 7)))))
