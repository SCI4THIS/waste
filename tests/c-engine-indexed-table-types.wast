(module $provider
  (type $signature (func))
  (table (export "nullable") 1 (ref null $signature)))
(register "typed-tables" $provider)

(module
  (type $signature (func))
  (import "typed-tables" "nullable" (table 1 (ref null $signature)))
  (func (export "answer") (result i32) (i32.const 42)))
(assert_return (invoke "answer") (i32.const 42))
