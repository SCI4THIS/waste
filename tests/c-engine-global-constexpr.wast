;; Global initializers use the ordinary instruction grammar, retain raw
;; terminator-free bytes, then validate the complete instruction list.
(module
  (type $pair (struct (field i32 i64)))
  (type $defaults (struct (field i32 anyref)))
  (type $nondefault (struct (field (ref i31))))
  (type $i64s (array i64))
  (global (export "eleven") i32 (i32.const 11))
  (global i32 (i32.add (i32.const 2) (i32.const 3)))
  (global i64 (i64.mul (i64.const 6) (i64.sub (i64.const 5) (i64.const 2))))
  (global f32 f32.const -3)
  (global f64 f64.const 4)
  (global funcref (ref.func $forward))
  (global (ref i31) (ref.i31 (i32.const 7)))
  (global anyref (any.convert_extern (ref.null extern)))
  (global externref (extern.convert_any (ref.null any)))
  (global (ref $pair) (struct.new $pair (i32.const 1) (i64.const 2)))
  (global (ref $defaults) (struct.new_default $defaults))
  (global (ref $i64s) (array.new $i64s (i64.const 4) (i32.const 3)))
  (global (ref $i64s) (array.new_default $i64s (i32.const 3)))
  (global (ref $i64s) (array.new_fixed $i64s 2 (i64.const 4) (i64.const 5)))
  (func $forward)
)

(assert_invalid
  (module (global i32 (nop)))
  "constant expression required"
)

(assert_invalid
  (module (global i32 (f32.const 0)))
  "type mismatch"
)

(assert_invalid
  (module (global i32 (i32.const 0) (i32.const 1)))
  "type mismatch"
)

(assert_invalid
  (module (global i32 (i32.div_s (i32.const 4) (i32.const 2))))
  "constant expression required"
)

(assert_invalid
  (module
    (global $mutable (mut i32) (i32.const 0))
    (global i32 (global.get $mutable)))
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
  (module (global i32 (global.get $missing)))
  "unknown global"
)

(assert_invalid
  (module (global (ref $missing) (ref.null $missing)))
  "unknown type"
)
