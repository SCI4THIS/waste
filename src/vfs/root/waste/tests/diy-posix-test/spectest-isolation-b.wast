;; This separate script must receive fresh spectest memory even when scheduled
;; in the same interpreter after spectest-isolation-a.wast.
(module
  (import "spectest" "memory" (memory 1))
  (func (export "load") (result i32)
    i32.const 0
    i32.load8_u))

(assert_return (invoke "load") (i32.const 0))
