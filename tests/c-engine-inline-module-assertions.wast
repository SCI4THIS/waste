(assert_trap
  (module
    (table 0 funcref)
    (elem (i32.const 1) $f)
    (func $f))
  "out of bounds table access")

(assert_unlinkable
  (module
    (import "missing" "function" (func)))
  "unknown import")
