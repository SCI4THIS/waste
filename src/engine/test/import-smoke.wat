(module
  (import "host" "add" (func $add (param i32 i32) (result i32)))
  (export "direct" (func $add))
  (func (export "wrapped") (param i32 i32) (result i32)
    local.get 0 local.get 1 call $add))
