;; Inline export placement after type reference is not tested by the spec suite.
;; func.wast always places (export ...) before (type ...).
(module
  (type $unary (func (param i32) (result i32)))

  (func (type $unary) (export "late-export")
    (param i32) (result i32)
    (local.get 0))
)

(assert_return (invoke "late-export" (i32.const 8)) (i32.const 8))
