;; The reusable native adapter resumes ordinary WAST assertions after READ.
(module (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (memory 1)
  (func (export "input") (result i32)
    i32.const 0 i32.const 64 i32.const 1 call $read
    i32.const 1 i32.eq
    i32.const 64 i32.load8_u i32.const 120 i32.eq i32.and))
(assert_return (invoke "input") (i32.const 1))
