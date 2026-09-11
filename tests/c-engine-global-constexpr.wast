;; GC-specific invalid global initializer cases not covered by global.wast
;; or the gc/ test suite.

(assert_invalid
  (module (global i32 (i32.div_s (i32.const 4) (i32.const 2))))
  "constant expression required"
)

(assert_invalid
  (module (global (ref i31) (ref.i31 (i64.const 0))))
  "type mismatch"
)

(assert_invalid
  (module
    (type $pair (struct (field i32 i64)))
    (global (ref $pair) (struct.new $pair (i32.const 1) (i32.const 2))))
  "type mismatch"
)

(assert_invalid
  (module
    (type $nondefault (struct (field (ref i31))))
    (global (ref $nondefault) (struct.new_default $nondefault)))
  "type mismatch"
)

(assert_invalid
  (module
    (type $i64s (array i64))
    (global (ref $i64s) (array.new_fixed $i64s 2 (i64.const 1))))
  "type mismatch"
)

(assert_invalid
  (module (global (ref $missing) (ref.null $missing)))
  "unknown type"
)
