;; Constant-expression boundaries shared by active data and element segments.
(module
  (memory 1)
  (data (offset (;empty instruction sequence;)))
  (data (offset (i32.const 0) (i32.const 0)))
)

(module
  (table 1 funcref)
  (elem (offset (;empty instruction sequence;)))
  (elem (offset (i32.const 0) (i32.const 0)))
)

(module
  (global $g funcref (ref.null func))
  (func $f)
  (table 1 funcref)
  (elem (i32.const 0) funcref (global.get $g))
  (elem (i32.const 0) funcref (item (call $f)))
)
