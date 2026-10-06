;; A valid module whose start pauses must NOT satisfy assert_invalid.
(assert_invalid
  (module
    (import "waste_kernel" "select_v1" (func $select (param i32 i32 i32 i32 i32) (result i32)))
    (memory 1)
    (func $start
      (i64.store (i32.const 128) (i64.const 3600))
      (drop (call $select (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 128))))
    (start $start))
  "type mismatch")
