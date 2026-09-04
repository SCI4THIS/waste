(module $provider
  (func (export "identity") (param i32) (result i32)
    (local.get 0)))
(register "functions" $provider)

(module
  (func (import "functions" "identity") (param i32) (result i32)))

(assert_unlinkable
  (module
    (func (import "functions" "identity") (param i64) (result i32)))
  "incompatible import type")

(assert_unlinkable
  (module
    (func (import "functions" "identity") (param i32)))
  "incompatible import type")
