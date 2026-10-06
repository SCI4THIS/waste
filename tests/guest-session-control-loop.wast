(module
  (func (export "spin") (loop $again (br $again))))
;; Runtime interruption must not satisfy an expected guest trap.
(assert_trap (invoke "spin") "unreachable")
