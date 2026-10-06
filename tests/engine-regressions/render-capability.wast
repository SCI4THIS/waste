;; Native and isolated browser batch stores must not request a DOM renderer.
(module
  (import "waste_kernel" "render_test_v1"
    (func $render (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "query") (result i32)
    (call $render (i32.const 0) (i32.const 0)))
  (func (export "request") (result i32)
    (call $render (i32.const 64) (i32.const 1024))))
(assert_return (invoke "query") (i32.const -1))
(assert_return (invoke "request") (i32.const -1))
