(module (func (export "answer") (result i32) i32.const 41))
(assert_return (invoke "answer") (i32.const 42))
