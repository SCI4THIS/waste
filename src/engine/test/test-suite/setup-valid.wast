;; Valid ordinary modules count as setup, independently of assertion totals.
(module $provider (func (export "answer") (result i32) i32.const 42))
(register "provider" $provider)
(module definition $template (func))
(module instance $instance $template)
(module (import "provider" "answer" (func (result i32))))
