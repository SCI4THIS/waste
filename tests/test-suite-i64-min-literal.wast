;; Negate the magnitude as unsigned bits: INT64_MIN is a valid WAT literal.
(module
  (func (export "decimal") (result i64) (i64.const -9223372036854775808))
  (func (export "hex") (result i64) (i64.const -0x8000000000000000)))
(assert_return (invoke "decimal") (i64.const 0x8000000000000000))
(assert_return (invoke "hex") (i64.const 0x8000000000000000))
