;; A sufficiently long assertion crosses browser pump boundaries. Expectations
;; must survive those returns, including a deliberately wrong return value.
(module
  (func $work
    (local $remaining i32)
    i32.const 1000000 local.set $remaining
    (loop $again
      local.get $remaining i32.const 1 i32.sub local.tee $remaining
      br_if $again))
  (func (export "value") (result i32) call $work i32.const 42)
  (func (export "trap") call $work unreachable))
(assert_return (invoke "value") (i32.const 42))
(assert_trap (invoke "trap") "unreachable")
(assert_return (invoke "value") (i32.const 41))
