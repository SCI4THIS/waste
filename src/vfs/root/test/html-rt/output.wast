;; Guest stdout must remain separate from assertion JSON in the native host.
(module (import "waste-runtime" "memory" (memory 4))
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "guest\0a")
  (func (export "write") (result i32)
    i32.const 1 i32.const 180000 i32.const 6 call $write))
(assert_return (invoke "write") (i32.const 6))
