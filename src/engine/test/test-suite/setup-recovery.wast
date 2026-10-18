;; A later good assertion cannot erase an earlier setup failure.
(module $good (func (export "answer") (result i32) i32.const 42))
(assert_return (invoke "answer") (i32.const 42))
(module (func (result i32)))
(module quote "(module (func call $absent))")
(assert_return (invoke $good "answer") (i32.const 42))
(module (func (export "answer") (result i32) i32.const 7))
(assert_return (invoke "answer") (i32.const 7))
