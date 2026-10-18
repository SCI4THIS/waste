;; Assertion imports never enable a production provider namespace.
(assert_unlinkable
  (module (import "libc" "strlen" (func (param i32) (result i32))))
  "unknown import")
