;; Backgrounded-group fan-out: the parent joins pgid=42 and forks a child
;; that moves itself to pgid=99 before entering SELECT.  A single WSC1
;; operation=3 record targets pgid=99.  Only the backgrounded child
;; receives SIGUSR1; the parent is bounded-child-first-blocked in waitpid
;; the entire time and must not have its handler fire on resume because
;; its own pgid (42) is not a member of the signalled group.  Covers the
;; "backgrounded group while the active parent is bounded-child-first
;; blocked" item left as future work in the multi-member fan-out slice.
(module
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "pselect" (func $select (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "sigaction" (func $action (param i32 i32 i32) (result i32)))
  (import "env" "tcgetattr" (func $get (param i32 i32) (result i32)))
  (import "env" "tcsetattr" (func $set (param i32 i32 i32) (result i32)))
  (import "env" "setpgid" (func $setpgid (param i32 i32) (result i32)))
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "waitpid" (func $wait (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (global $caught (mut i32) (i32.const 0))
  (data (i32.const 64) "B\0a")
  ;; env.sigaction stores the Wasm function index.  Imports (9) +
  ;; __errno_location (idx 9) + $handler (idx 10) → store 10.
  (data (i32.const 128) "\0a\00\00\00")
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
    ;; Parent joins pgid=42; the child will override its own pgid below.
    (drop (call $setpgid (i32.const 0) (i32.const 42)))
    (i32.const 0))
  (func (export "backgrounded") (result i32) (local $pid i32) (local $status i32)
    (local.set $pid (call $fork))
    (if (i32.eqz (local.get $pid)) (then
      ;; Child moves itself out of the parent's pgid before pausing, so
      ;; the group fan-out below is the only path that can wake it.
      (drop (call $setpgid (i32.const 0) (i32.const 99)))
      (global.set $caught (i32.const 0))
      (drop (call $write (i32.const 1) (i32.const 64) (i32.const 2)))
      (drop (call $select (i32.const 0) (i32.const 0) (i32.const 0)
            (i32.const 0) (i32.const 0) (i32.const 0)))
      (call $exit (global.get $caught))))
    (drop (call $wait (local.get $pid) (i32.const 360) (i32.const 0)))
    (local.set $status (i32.shr_u (i32.load (i32.const 360)) (i32.const 8)))
    ;; Zero-timeout pselect to drain any pending signal queued on the parent;
    ;; the signal targeted pgid=99, so this must leave $caught at 0.  The
    ;; 384 timespec is zero bytes.
    (drop (call $select (i32.const 0) (i32.const 0) (i32.const 0)
          (i32.const 0) (i32.const 384) (i32.const 0)))
    ;; Parent is in pgid=42, not a member of pgid=99: no handler must fire.
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
