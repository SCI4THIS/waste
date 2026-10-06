;; A wrong expectation after a module assertion must produce a failed result.
(module (func (export "answer") (result i32) i32.const 42))
(assert_invalid (module (func (result i32))) "type mismatch")
(assert_return (invoke "answer") (i32.const 41))
(assert_return (invoke "answer") (i32.const 42))
