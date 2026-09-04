(module $provider
  (func (export "answer") (result i32) i32.const 42))
(register "provider" $provider)
(module $consumer
  (import "provider" "answer" (func $answer (result i32)))
  (func (export "run") (result i32) call $answer))
(assert_return (invoke $consumer "run") (i32.const 42))
