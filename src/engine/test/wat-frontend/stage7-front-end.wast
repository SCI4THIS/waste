(module
  (; outer (; nested ;) comment ;)
  (@ignored (nested "string ) ("))
  (func (export "integer-float") (result f64)
    f64.const 18446744073709551616)
  (memory (export "bytes") 1)
  (data (i32.const 0) "A\00B")
)

((@ignored) module $annotated
  ((@ignored) func (@ignored) (export "value") (result i32)
    ((@ignored) i32.const 7))
)

(assert_return (invoke $annotated "value") (i32.const 7))

(assert_malformed
  (module binary "\00asm\01\00\00\00\00")
  "malformed section id")
