;; Regression coverage for canonical WAT-to-Wasm section emission.
(module
  (type $unary (func (param i32) (result i32)))
  (table (export "table") 2 funcref)
  (memory (export "memory") 1 2)
  (global $answer (export "answer") i32 (i32.const 42))
  (func $identity (type $unary) (export "identity")
    (local.get 0))
  (elem (i32.const 0) func $identity)
  (data (i32.const 8) "ok")
)
(assert_return (invoke "identity" (i32.const 7)) (i32.const 7))
