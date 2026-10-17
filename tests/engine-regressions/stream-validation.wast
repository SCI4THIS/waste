;; Every body must validate, including uncalled functions and later bodies.
(module $valid
  (func $unused (result i32) i32.const 9)
  (func (export "f") (result i32) i32.const 42))
(assert_return (invoke $valid "f") (i32.const 42))
(assert_invalid
  (module (func $unused drop) (func (export "f") (result i32) i32.const 42))
  "type mismatch")
(assert_invalid
  (module (func (export "f") (result i32) i32.const 42) (func $unused drop))
  "type mismatch")
(assert_invalid
  (module (func $unused (result i32)) (func (export "f") (result i32) i32.const 42))
  "type mismatch")
(assert_trap
  (module (func $start (unreachable)) (start $start))
  "unreachable")
(module $recover (func (export "f") (result i32) i32.const 17))
(assert_return (invoke $recover "f") (i32.const 17))
