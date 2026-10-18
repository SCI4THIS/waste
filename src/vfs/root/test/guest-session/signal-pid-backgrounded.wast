;; Backgrounded-child PID routing: the parent forks a child which pauses
;; in pselect.  A single WSC1 operation=2 record targets the child's PID
;; while the parent is bounded-child-first-blocked in waitpid.  Only the
;; child receives SIGUSR1 because by-PID routing does not fan out to
;; other processes; the parent's own handler must not fire on resume.
;; Covers the "signals delivered to a backgrounded child while the active
;; parent is bounded-child-first-blocked" item left as future work in the
;; signal-pid slice.
(module
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "pselect" (func $select (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "sigaction" (func $action (param i32 i32 i32) (result i32)))
  (import "env" "tcgetattr" (func $get (param i32 i32) (result i32)))
  (import "env" "tcsetattr" (func $set (param i32 i32 i32) (result i32)))
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "waitpid" (func $wait (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (global $caught (mut i32) (i32.const 0))
  (data (i32.const 64) "D\0a")
  ;; env.sigaction stores the Wasm function index.  Imports (8) +
  ;; __errno_location (idx 8) + $handler (idx 9) → store 9.
  (data (i32.const 128) "\09\00\00\00")
  (func (export "__errno_location") (result i32) (i32.const 0))
  (func $handler (param $signal i32) (global.set $caught (local.get $signal)))
  (func (export "setup") (result i32)
    (drop (call $get (i32.const 0) (i32.const 256)))
    (i32.store (i32.const 256) (i32.const 0))
    (i32.store (i32.const 260) (i32.const 0))
    (i32.store (i32.const 268) (i32.const 0))
    (i32.store8 (i32.const 279) (i32.const 1))
    (drop (call $set (i32.const 0) (i32.const 0) (i32.const 256)))
    (drop (call $action (i32.const 10) (i32.const 128) (i32.const 0)))
    (i32.const 0))
  (func (export "backgrounded") (result i32) (local $pid i32) (local $status i32)
    (local.set $pid (call $fork))
    (if (i32.eqz (local.get $pid)) (then
      (global.set $caught (i32.const 0))
      (drop (call $write (i32.const 1) (i32.const 64) (i32.const 2)))
      (drop (call $select (i32.const 0) (i32.const 0) (i32.const 0)
            (i32.const 0) (i32.const 0) (i32.const 0)))
      (call $exit (global.get $caught))))
    (drop (call $wait (local.get $pid) (i32.const 360) (i32.const 0)))
    (local.set $status (i32.shr_u (i32.load (i32.const 360)) (i32.const 8)))
    ;; Zero-timeout pselect to drain any signal that leaked to the parent.
    ;; The 384 timespec is zero bytes.
    (drop (call $select (i32.const 0) (i32.const 0) (i32.const 0)
          (i32.const 0) (i32.const 384) (i32.const 0)))
    ;; By-PID routing targeted the child only: the parent must not have fired.
    (if (i32.ne (global.get $caught) (i32.const 0))
      (then (return (i32.const -1))))
    ;; Child received SIGUSR1 and exited with 10 as its status.
    (if (i32.ne (local.get $status) (i32.const 10))
      (then (return (i32.const -2))))
    (i32.const 1))
  (func (export "done") (call $exit (i32.const 0))))
(assert_return (invoke "setup") (i32.const 0))
(assert_return (invoke "backgrounded") (i32.const 1))
(invoke "done")
