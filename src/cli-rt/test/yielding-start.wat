;; An entry is resumable; a constructor must complete synchronously.
(module (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (memory 1)
  (func $init (drop (call $read (i32.const 0) (i32.const 1024) (i32.const 1))))
  (start $init)
  (func (export "_start")))
