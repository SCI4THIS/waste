(module
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "execve" (func $exec (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 16) "/bin/control.wast\00")
  (data (i32.const 64) "\10\00\00\00\00\00\00\00")
  (func (export "spawn")
    (if (i32.eqz (call $fork)) (then
      (drop (call $exec (i32.const 16) (i32.const 64) (i32.const 0)))
      unreachable))))
(assert_return (invoke "spawn"))
