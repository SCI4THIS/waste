(module
  (import "env" "fork" (func $fork (result i32)))
  (func (export "spin")
    (if (i32.eqz (call $fork))
      (then (loop $again (br $again))))))
(assert_return (invoke "spin"))
