;; Run beside the writer in either order, sequentially or concurrently.
(module
  (table $wide (import "spectest" "table64") i64 10 20 funcref)
  (func (export "size") (result i64) (table.size $wide))
  (func (export "null") (param i64) (result i32)
    (ref.is_null (table.get $wide (local.get 0))))
)
(assert_return (invoke "size") (i64.const 10))
(assert_return (invoke "null" (i64.const 0)) (i32.const 1))
(assert_return (invoke "null" (i64.const 9)) (i32.const 1))
