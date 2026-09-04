(module
  (global $entry funcref (ref.func $later))
  (elem declare funcref (ref.func $later))
  (func $later)
  (func (export "has-entry") (result i32)
    (ref.is_null (global.get $entry)))
)
(assert_return (invoke "has-entry") (i32.const 0))
