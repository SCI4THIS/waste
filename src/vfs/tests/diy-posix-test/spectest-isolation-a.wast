;; Modules in one script intentionally share the sandbox-local spectest memory.
(module $writer
  (import "spectest" "memory" (memory 1))
  (func (export "load") (result i32)
    i32.const 0
    i32.load8_u)
  (func (export "store") (param i32)
    i32.const 0
    local.get 0
    i32.store8))

(module $observer
  (import "spectest" "memory" (memory 1))
  (func (export "load") (result i32)
    i32.const 0
    i32.load8_u))

(assert_return (invoke $writer "load") (i32.const 0))
(assert_return (invoke $observer "load") (i32.const 0))
(assert_return (invoke $writer "store" (i32.const 170)))
(assert_return (invoke $observer "load") (i32.const 170))
