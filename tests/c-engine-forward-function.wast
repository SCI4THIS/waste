;; Forward named calls must resolve after all module declarations are known.
(module
  (func $caller (export "caller") (param i32) (result i32)
    (call $later (local.get 0)))
  (func $later (param i32) (result i32)
    (i32.add (local.get 0) (i32.const 1)))
)
(assert_return (invoke "caller" (i32.const 41)) (i32.const 42))
