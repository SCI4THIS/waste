;; Timed SELECT resumes without input; fork returns zero in the child, and the
;; original parent returns the reaped child's status after restoring memory.
(module
  (import "env" "select" (func $select (param i32 i32 i32 i32 i32) (result i32)))
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (import "env" "waitpid" (func $wait (param i32 i32 i32) (result i32)))
  (memory 1)
  (func (export "timer") (result i32)
    (i64.store (i32.const 1024) (i64.const 0))
    (i32.store (i32.const 1032) (i32.const 1000))
    (call $select (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 1024)))
  (func (export "fork") (result i32) (local $pid i32)
    (local.set $pid (call $fork))
    (if (i32.eqz (local.get $pid)) (then
      (i32.store (i32.const 1040) (i32.const 99))
      (call $exit (i32.const 7))))
    (drop (call $wait (local.get $pid) (i32.const 1044) (i32.const 0)))
    (if (i32.ne (i32.load (i32.const 1040)) (i32.const 0)) (then unreachable))
    (i32.shr_u (i32.load (i32.const 1044)) (i32.const 8))))
(assert_return (invoke "timer") (i32.const 0))
(assert_return (invoke "fork") (i32.const 7))
