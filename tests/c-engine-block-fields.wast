;; Folded control instructions parse their block type fields before entering
;; the instruction body, matching the phased structure of the OCaml parser.
(module
  (func (export "block-param-result") (result i32)
    (i32.const 41)
    (block (param i32) (result i32)
      (i32.const 1)
      (i32.add)))

  (func (export "block-multi-result") (result i32 i64)
    (block (result i32) (result i64)
      (i32.const 7)
      (i64.const 8)))

  (func (export "loop-result") (result i32)
    (loop (result i32)
      (i32.const 9)))

  (func (export "if-result") (result i32)
    (if (result i32) (i32.const 1)
      (then (i32.const 10))
      (else (i32.const 11))))

  (func (export "folded-bottom")
    (block (result i32)
      (unreachable))
    (drop))
)

(assert_return (invoke "block-param-result") (i32.const 42))
(assert_return
  (invoke "block-multi-result")
  (i32.const 7) (i64.const 8))
(assert_return (invoke "loop-result") (i32.const 9))
(assert_return (invoke "if-result") (i32.const 10))
(assert_trap (invoke "folded-bottom") "unreachable")
