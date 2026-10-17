;; Compiled guest null-signal queries and LLVM lock-owner observations.
(module $owner
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "strerror" (func $strerror (param i32) (result i32)))
  (import "libc" "strcmp" (func $strcmp (param i32 i32) (result i32)))
  (import "env" "killpg" (func $killpg (param i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (data (i32.const 180000) "/tmp/lock-owner.bin\00")
  (data (i32.const 180064) "/tmp/lock-owner-status.txt\00")
  (data (i32.const 180128) "No such process\00")
  (func $capture (param $path i32) (param $output i32) (param $count i32) (result i32)
    (local $fd i32) (local $bytes i32)
    local.get $path i32.const 0 i32.const 0 call $open local.set $fd
    local.get $fd local.get $output local.get $count call $read local.set $bytes
    local.get $fd call $close drop
    local.get $bytes)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 182000 i32.const 1024 call $capture)
  (func (export "exit-status") (result i32)
    i32.const 180064 i32.const 184000 i32.const 8 call $capture i32.const 2 i32.eq
    i32.const 184000 i32.load16_u i32.const 0x0a30 i32.eq i32.and)
  (func (export "value") (param $index i32) (result i32)
    i32.const 182000 local.get $index i32.const 4 i32.mul i32.add i32.load)
  (func (export "esrch-message") (result i32)
    i32.const 3 call $strerror i32.const 180128 call $strcmp)
  (func (export "group-query") (param $group i32) (result i32)
    local.get $group i32.const 0 call $killpg)
  (func (export "errno") (result i32) call $errno i32.load)
)
(assert_return (invoke $owner "capture") (i32.const 296))
(assert_return (invoke $owner "exit-status") (i32.const 1))
;; guest profile
(assert_return (invoke $owner "value" (i32.const 0)) (i32.const 1))
;; POSIX profile
(assert_return (invoke $owner "value" (i32.const 1)) (i32.const 1))
;; hostname lookup
(assert_return (invoke $owner "value" (i32.const 2)) (i32.const 0))
;; default hostname
(assert_return (invoke $owner "value" (i32.const 3)) (i32.const 1))
;; null signal to self
(assert_return (invoke $owner "value" (i32.const 4)) (i32.const 0))
;; success preserves errno
(assert_return (invoke $owner "value" (i32.const 5)) (i32.const 17))
;; live owner with stale ESRCH
(assert_return (invoke $owner "value" (i32.const 6)) (i32.const 1))
;; stale errno preserved
(assert_return (invoke $owner "value" (i32.const 7)) (i32.const 3))
;; missing PID query
(assert_return (invoke $owner "value" (i32.const 8)) (i32.const -1))
;; missing PID errno
(assert_return (invoke $owner "value" (i32.const 9)) (i32.const 3))
;; missing local owner
(assert_return (invoke $owner "value" (i32.const 10)) (i32.const 0))
;; owner query ESRCH
(assert_return (invoke $owner "value" (i32.const 11)) (i32.const 3))
;; remote owner retained
(assert_return (invoke $owner "value" (i32.const 12)) (i32.const 1))
;; remote owner does not query PID
(assert_return (invoke $owner "value" (i32.const 13)) (i32.const 17))
;; zero owner retained
(assert_return (invoke $owner "value" (i32.const 14)) (i32.const 1))
;; zero owner errno preserved
(assert_return (invoke $owner "value" (i32.const 15)) (i32.const 17))
;; negative local owner invalid
(assert_return (invoke $owner "value" (i32.const 16)) (i32.const 0))
;; negative owner does not signal group
(assert_return (invoke $owner "value" (i32.const 17)) (i32.const 17))
;; remote negative owner retained
(assert_return (invoke $owner "value" (i32.const 18)) (i32.const 1))
;; negative signal rejected
(assert_return (invoke $owner "value" (i32.const 19)) (i32.const -1))
;; negative signal EINVAL
(assert_return (invoke $owner "value" (i32.const 20)) (i32.const 22))
;; out of range signal rejected
(assert_return (invoke $owner "value" (i32.const 21)) (i32.const -1))
;; out of range signal EINVAL
(assert_return (invoke $owner "value" (i32.const 22)) (i32.const 22))
;; missing PID real signal rejected
(assert_return (invoke $owner "value" (i32.const 23)) (i32.const -1))
;; missing real signal ESRCH
(assert_return (invoke $owner "value" (i32.const 24)) (i32.const 3))
;; current group null query
(assert_return (invoke $owner "value" (i32.const 25)) (i32.const 0))
;; group success errno preserved
(assert_return (invoke $owner "value" (i32.const 26)) (i32.const 17))
;; missing group query
(assert_return (invoke $owner "value" (i32.const 27)) (i32.const -1))
;; missing group ESRCH
(assert_return (invoke $owner "value" (i32.const 28)) (i32.const 3))
;; unrepresentable group rejected
(assert_return (invoke $owner "value" (i32.const 29)) (i32.const -1))
;; group EINVAL
(assert_return (invoke $owner "value" (i32.const 30)) (i32.const 22))
;; empty mask
(assert_return (invoke $owner "value" (i32.const 31)) (i32.const 0))
;; add blocked signal
(assert_return (invoke $owner "value" (i32.const 32)) (i32.const 0))
;; block signal
(assert_return (invoke $owner "value" (i32.const 33)) (i32.const 0))
;; queue blocked signal
(assert_return (invoke $owner "value" (i32.const 34)) (i32.const 0))
;; pending before query
(assert_return (invoke $owner "value" (i32.const 35)) (i32.const 0))
;; signal pending before
(assert_return (invoke $owner "value" (i32.const 36)) (i32.const 1))
;; self query with pending signal
(assert_return (invoke $owner "value" (i32.const 37)) (i32.const 1))
;; pending after query
(assert_return (invoke $owner "value" (i32.const 38)) (i32.const 0))
;; signal pending after
(assert_return (invoke $owner "value" (i32.const 39)) (i32.const 1))
;; no handler delivery by query
(assert_return (invoke $owner "value" (i32.const 40)) (i32.const 0))
;; restore mask
(assert_return (invoke $owner "value" (i32.const 41)) (i32.const 0))
;; actual self signal
(assert_return (invoke $owner "value" (i32.const 42)) (i32.const 0))
;; actual handler delivery
(assert_return (invoke $owner "value" (i32.const 43)) (i32.const 1))
;; release pipe
(assert_return (invoke $owner "value" (i32.const 44)) (i32.const 0))
;; report pipe
(assert_return (invoke $owner "value" (i32.const 45)) (i32.const 0))
;; fork creates positive PID
(assert_return (invoke $owner "value" (i32.const 46)) (i32.const 1))
;; blocked child query
(assert_return (invoke $owner "value" (i32.const 47)) (i32.const 0))
;; child query preserves errno
(assert_return (invoke $owner "value" (i32.const 48)) (i32.const 17))
;; live child owner retained
(assert_return (invoke $owner "value" (i32.const 49)) (i32.const 1))
;; release blocked child
(assert_return (invoke $owner "value" (i32.const 50)) (i32.const 1))
;; close release writer
(assert_return (invoke $owner "value" (i32.const 51)) (i32.const 0))
;; child report bytes
(assert_return (invoke $owner "value" (i32.const 52)) (i32.const 16))
;; child queries parent
(assert_return (invoke $owner "value" (i32.const 53)) (i32.const 0))
;; child success preserves errno
(assert_return (invoke $owner "value" (i32.const 54)) (i32.const 17))
;; child read was not interrupted
(assert_return (invoke $owner "value" (i32.const 55)) (i32.const 1))
;; child received byte
(assert_return (invoke $owner "value" (i32.const 56)) (i32.const 82))
;; EOF from child exit
(assert_return (invoke $owner "value" (i32.const 57)) (i32.const 0))
;; unreaped zombie query
(assert_return (invoke $owner "value" (i32.const 58)) (i32.const 0))
;; zombie success errno
(assert_return (invoke $owner "value" (i32.const 59)) (i32.const 17))
;; zombie owner retained
(assert_return (invoke $owner "value" (i32.const 60)) (i32.const 1))
;; reap child
(assert_return (invoke $owner "value" (i32.const 61)) (i32.const 1))
;; raw child exit status
(assert_return (invoke $owner "value" (i32.const 62)) (i32.const 5888))
;; reaped child absent
(assert_return (invoke $owner "value" (i32.const 63)) (i32.const -1))
;; reaped child ESRCH
(assert_return (invoke $owner "value" (i32.const 64)) (i32.const 3))
;; reaped owner discarded
(assert_return (invoke $owner "value" (i32.const 65)) (i32.const 0))
;; reaped owner errno
(assert_return (invoke $owner "value" (i32.const 66)) (i32.const 3))
;; hostname provider failure
(assert_return (invoke $owner "value" (i32.const 67)) (i32.const -1))
;; hostname ENAMETOOLONG
(assert_return (invoke $owner "value" (i32.const 68)) (i32.const 36))
;; identity error retains owner
(assert_return (invoke $owner "value" (i32.const 69)) (i32.const 1))
;; identity error errno
(assert_return (invoke $owner "value" (i32.const 70)) (i32.const 36))
;; restore identity provider
(assert_return (invoke $owner "value" (i32.const 71)) (i32.const 0))
;; restored hostname
(assert_return (invoke $owner "value" (i32.const 72)) (i32.const 1))
;; Actual libc++ errc agrees with the public/kernel errno value.
(assert_return (invoke $owner "value" (i32.const 73)) (i32.const 3))
(assert_return (invoke $owner "esrch-message") (i32.const 0))
;; The legacy group entry also follows -1/errno and preserves errno on success.
(assert_return (invoke $owner "group-query" (i32.const -1)) (i32.const -1))
(assert_return (invoke $owner "errno") (i32.const 22))
(assert_return (invoke $owner "group-query" (i32.const 0)) (i32.const 0))
(assert_return (invoke $owner "errno") (i32.const 22))
(assert_return (invoke $owner "group-query" (i32.const 2147483647)) (i32.const -1))
(assert_return (invoke $owner "errno") (i32.const 3))
