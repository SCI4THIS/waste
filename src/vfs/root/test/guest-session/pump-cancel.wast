;; Cooperative dispatch-pump cancellation: the guest emits a wake marker
;; then enters an unbounded compute loop with no I/O yield.  The host
;; harness arms a pump quantum and posts a `cancel` message after seeing
;; the marker; the worker's pump handshake delivers that message between
;; opcodes so the next dispatch safepoint reports EXEC_ERROR_INTERRUPTED
;; with EXEC_STOP_CANCELLED.  Closes the "immediate external cancellation
;; during a running worker" item left as future work in the bounded
;; execution-policy slice.
(module
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 64) "P\0a")
  (func (export "__errno_location") (result i32) (i32.const 0))
  (func (export "spin") (result i32) (local $i i32)
    (drop (call $write (i32.const 1) (i32.const 64) (i32.const 2)))
    (loop $top
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $top))
    (i32.const 0))
  (func (export "done") (call $exit (i32.const 0))))
(assert_return (invoke "spin") (i32.const 0))
(invoke "done")
