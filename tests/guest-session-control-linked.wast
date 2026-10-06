(module $provider
  (func (export "spin") (loop $again (br $again))))
(register "provider" $provider)
(module
  (import "provider" "spin" (func $spin))
  (func (export "spin") (call $spin)))
(assert_return (invoke "spin"))
