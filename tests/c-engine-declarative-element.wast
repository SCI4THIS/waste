(module
  (func $f)
  (elem declare func $f)
  (func (export "answer") (result i32) (i32.const 42)))
(assert_return (invoke "answer") (i32.const 42))
