;; Function fields are parsed in OCaml-compatible phases: type use, parameters,
;; results, locals, then instructions. Inline exports remain accepted at a
;; phase boundary for compatibility with existing C-engine fixtures.
(module
  (type $unary (func (param i32) (result i32)))

  (func (export "identity") (type $unary)
    (param i32) (result i32) (local i64)
    (local.get 0))

  (func (type $unary) (export "late-export")
    (param i32) (result i32)
    (local.get 0))

  (func (export "split-fields")
    (param i32) (param i32) (result) (result i32)
    (local i64) (local f32)
    (local.get 0))

  (func (export "bottom") (result i32)
    (unreachable))
)

(assert_return (invoke "identity" (i32.const 7)) (i32.const 7))
(assert_return (invoke "late-export" (i32.const 8)) (i32.const 8))
(assert_return
  (invoke "split-fields" (i32.const 9) (i32.const 10))
  (i32.const 9))
(assert_trap (invoke "bottom") "unreachable")
