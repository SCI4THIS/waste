(module
  ;; Minimal Stage 2 lifecycle fixture.  The host deliberately controls the
  ;; fork/exec yields so the same parent continuation can be resumed after a
  ;; successful or failed child exec.
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "execve" (func $execve (param i32 i32 i32) (result i32)))
  (import "env" "waitpid" (func $waitpid (param i32 i32 i32) (result i32)))
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)

  (data (i32.const 64) "/bin/waste-probe\00")
  (data (i32.const 96) "PARENT-RESUMED\00")
  (data (i32.const 112) "PARENT-DONE\00")
  (data (i32.const 128) "EXEC-FAILED\00")

  (func (export "run") (result i32)
    (local $pid i32)
    (local.set $pid (call $fork))
    (if (result i32) (i32.eqz (local.get $pid))
      (then
        (drop (call $execve (i32.const 64) (i32.const 0) (i32.const 0)))
        (drop (call $write (i32.const 1) (i32.const 128) (i32.const 11)))
        (call $exit (i32.const 127))
        (i32.const -1))
      (else
        (drop (call $write (i32.const 1) (i32.const 96) (i32.const 14)))
        (drop (call $waitpid (local.get $pid) (i32.const 32) (i32.const 0)))
        (drop (call $write (i32.const 1) (i32.const 112) (i32.const 11)))
        (i32.const 0)))))
