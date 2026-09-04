(module
  (func (export "bad") (result i32) (global.get $missing-global))
)
