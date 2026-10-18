;; Reporting negative control: prints .F. and exits 1; the middle mismatch is intentional.
(module
  (func (export "one") (result i32) i32.const 1))
(assert_return (invoke "one") (i32.const 1))
(assert_return (invoke "one") (i32.const 2))
(assert_return (invoke "one") (i32.const 1))
