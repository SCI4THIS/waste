;; Function type uses may refer to a declaration later in the text module.
(module
  (func (type $unary) (export "identity")
    (local.get 0))
  (type $unary (func (param i32) (result i32)))
)
(assert_return (invoke "identity" (i32.const 9)) (i32.const 9))
