(module
  (import "provider" "run" (func $provider (result i32)))
  (func (export "run_consumer") (result i32)
    (i32.add (call $provider) (i32.const 3)))
)
