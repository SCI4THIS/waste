;; Deterministic guest clock parity: identical module and expected outputs in
;; native, browser exports and the production worker.  The host-side session
;; timeout keeps real time, so a slow runner cannot ride a frozen guest clock.
(module
  (import "env" "time" (func $time (param i32) (result i64)))
  (import "env" "gettimeofday" (func $gettimeofday (param i32 i32) (result i32)))
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 64) "FROZEN\0a")
  (func (export "__errno_location") (result i32) (i32.const 0))

  (func (export "frozen_time") (result i64) (local $v i64)
    (local.set $v (call $time (i32.const 0)))
    (drop (call $write (i32.const 1) (i32.const 64) (i32.const 7)))
    (local.get $v))

  (func (export "frozen_time_slot") (result i64)
    (drop (call $time (i32.const 128)))
    (i64.load (i32.const 128)))

  (func (export "frozen_realtime_sec") (result i64)
    (drop (call $gettimeofday (i32.const 192) (i32.const 0)))
    (i64.load (i32.const 192)))

  (func (export "frozen_realtime_usec") (result i32)
    (drop (call $gettimeofday (i32.const 256) (i32.const 0)))
    (i32.load (i32.const 264)))

  (func (export "done") (call $exit (i32.const 0))))
;; Shared contract pins kernel-visible realtime to 1 234 567 890 123 456 789 ns:
;;   seconds       = 1 234 567 890  (time, gettimeofday tv_sec)
;;   microseconds  =   123 456      (gettimeofday tv_usec = nanoseconds / 1000)
(assert_return (invoke "frozen_time") (i64.const 1234567890))
(assert_return (invoke "frozen_time_slot") (i64.const 1234567890))
(assert_return (invoke "frozen_realtime_sec") (i64.const 1234567890))
(assert_return (invoke "frozen_realtime_usec") (i32.const 123456))
(invoke "done")
