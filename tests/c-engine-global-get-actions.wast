(module $provider
  (global (export "constant") i32 (i32.const 42))
  (global (export "mutable") (mut i32) (i32.const 7)))
(register "globals" $provider)

(assert_return (get $provider "constant") (i32.const 42))
(assert_return (get $provider "mutable") (i32.const 7))

(module $consumer
  (global (import "globals" "constant") i32)
  (export "imported" (global 0)))
(assert_return (get "imported") (i32.const 42))
(assert_return (get $provider "constant") (i32.const 42))
