(module
  (func (export "answer") (result i32) i32.const 42)
  (func (export "trap") unreachable))
(assert_return (invoke "answer") (i32.const 42))
(assert_trap (invoke "trap") "unreachable")
(assert_invalid (module (func (result i32))) "type mismatch")
