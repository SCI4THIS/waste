;; Process-group signal fan-out: the active process joins pgid=42 and then
;; receives SIGUSR1 via the store's by-pgid route (signalPgid=42).  The native
;; fd protocol encodes this as WSC1 operation=3; the browser export is
;; waste_wast_raise_signal_pgid; the worker uses the pgid message field.
(module
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "pselect" (func $select (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "env" "sigaction" (func $action (param i32 i32 i32) (result i32)))
  (import "env" "tcgetattr" (func $get (param i32 i32) (result i32)))
  (import "env" "tcsetattr" (func $set (param i32 i32 i32) (result i32)))
  (import "env" "setpgid" (func $setpgid (param i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (global $caught (mut i32) (i32.const 0))
  (data (i32.const 64) "A\0a")
  ;; env.sigaction stores the Wasm function index of $handler.  Imports (7) +
  ;; __errno_location (idx 7) + $handler (idx 8) → store 8.
  (data (i32.const 128) "\08\00\00\00")
  (func (export "__errno_location") (result i32) (i32.const 0))
  (func $handler (param $signal i32) (global.set $caught (local.get $signal)))
  (func (export "setup") (result i32)
    ;; Disable ONLCR etc. so the marker byte reaches the host transcript verbatim.
    (drop (call $get (i32.const 0) (i32.const 256)))
    (i32.store (i32.const 256) (i32.const 0))
    (i32.store (i32.const 260) (i32.const 0))
    (i32.store (i32.const 268) (i32.const 0))
    (i32.store8 (i32.const 279) (i32.const 1))
    (drop (call $set (i32.const 0) (i32.const 0) (i32.const 256)))
    (drop (call $action (i32.const 10) (i32.const 128) (i32.const 0)))
    ;; Join pgid=42; later control event signals that pgid via the fan-out.
    ;; posix_kernel_setpgid returns the new pgid rather than 0, so drop it.
    (drop (call $setpgid (i32.const 0) (i32.const 42)))
    (i32.const 0))
  (func (export "pause") (result i32)
    (global.set $caught (i32.const 0))
    (drop (call $write (i32.const 1) (i32.const 64) (i32.const 2)))
    (if (i32.ne (call $select (i32.const 0) (i32.const 0) (i32.const 0)
          (i32.const 0) (i32.const 0) (i32.const 0)) (i32.const -4))
      (then (return (i32.const -1))))
    (global.get $caught))
  (func (export "done") (call $exit (i32.const 0))))
(assert_return (invoke "setup") (i32.const 0))
(assert_return (invoke "pause") (i32.const 10))
(invoke "done")
