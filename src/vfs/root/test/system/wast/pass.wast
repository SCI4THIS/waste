;; /bin/wast --verbose pass.wast prints three dots and a success summary.
(module
  (func (export "one") (result i32) i32.const 1))
(assert_return (invoke "one") (i32.const 1))
(assert_return (invoke "one") (i32.const 1))
(assert_return (invoke "one") (i32.const 1))
