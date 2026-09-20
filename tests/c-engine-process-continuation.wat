(module
  ;; Stage 1 freezes the process-control ABI.  Later stages drive both phases
  ;; from one suspended fork continuation instead of separate probe invocations.
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (import "env" "waitpid" (func $waitpid (param i32 i32 i32) (result i32)))
  (memory (export "memory") 1)

  (func $child (result i32)
    (i32.store (i32.const 0) (i32.const 51966)) ;; 0xCAFE
    (call $exit (i32.const 127))
    (i32.const -1))

  (func (export "run") (result i32)
    (local $pid i32)
    (local.set $pid (call $fork))
    (if (result i32) (i32.eqz (local.get $pid))
      (then (call $child))
      (else
        (i32.store (i32.const 0) (i32.const 48879)) ;; 0xBEEF
        (drop (call $waitpid (local.get $pid) (i32.const 8) (i32.const 0)))
        (i32.load (i32.const 8)))))
)
