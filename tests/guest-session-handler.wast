;; Definitions, registration, module assertions, invocation and trap matching.
(module definition $template (func (export "answer") (result i32) (i32.const 42)))
(module instance $instance $template)
(register "handler-answer" $instance)
(assert_return (invoke $instance "answer") (i32.const 42))
(module $consumer
  (import "handler-answer" "answer" (func $answer (result i32)))
  (func (export "answer") (result i32) (call $answer))
  (func (export "trap") unreachable))
(assert_return (invoke $consumer "answer") (i32.const 42))
(assert_trap (invoke $consumer "trap") "unreachable")
(assert_invalid (module (func (result i32))) "type mismatch")
(assert_trap (module (func $start unreachable) (start $start)) "unreachable")
