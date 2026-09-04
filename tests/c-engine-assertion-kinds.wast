(module
  (func (export "answer") (result i32) (i32.const 42))
  (func (export "unreachable") unreachable)
  (func $recurse (call $recurse))
  (export "recurse" (func $recurse)))

(assert_return (invoke "answer") (i32.const 42))
(assert_trap (invoke "unreachable") "unreachable")
(assert_exhaustion (invoke "recurse") "call stack exhausted")
