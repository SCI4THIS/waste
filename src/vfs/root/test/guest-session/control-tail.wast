(module
  (func $spin (export "spin") (return_call $spin)))
(assert_return (invoke "spin"))
