;; Host-boundary fixture: the runner must time out or cancel this finite wait
;; long before its guest deadline. It is not an ordinary installed passing test.
(module
  (import "env" "select" (func $select (param i32 i32 i32 i32 i32) (result i32)))
  (memory 1)
  (func (export "wait") (result i32)
    (i64.store (i32.const 64) (i64.const 30))
    (i64.store (i32.const 72) (i64.const 0))
    (call $select (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 64))))
(assert_return (invoke "wait") (i32.const 0))
