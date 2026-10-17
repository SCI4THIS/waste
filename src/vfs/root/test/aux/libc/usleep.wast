;; Shared libc microsecond sleep uses the same engine deadline as nanosleep.
(module $usleep
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "usleep" (func $sleep (param i32) (result i32)))
  (import "libc" "clock_gettime" (func $clock (param i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (func (export "sleep") (param $microseconds i32) (result i32)
    call $errno i32.const 73 i32.store
    local.get $microseconds call $sleep)
  (func (export "errno") (result i32) call $errno i32.load)
  (func (export "elapsed") (param $microseconds i32) (result i32)
    (local $seconds i64) (local $nanos i32) (local $ok i32)
    i32.const 1 i32.const 180000 call $clock i32.eqz local.set $ok
    i32.const 180000 i64.load local.set $seconds
    i32.const 180008 i32.load local.set $nanos
    call $errno i32.const 73 i32.store
    local.get $microseconds call $sleep i32.eqz local.get $ok i32.and local.set $ok
    call $errno i32.load i32.const 73 i32.eq local.get $ok i32.and local.set $ok
    i32.const 1 i32.const 180000 call $clock i32.eqz local.get $ok i32.and
    i32.const 180000 i64.load local.get $seconds i64.sub i64.const 1000000000 i64.mul
    i32.const 180008 i32.load i64.extend_i32_u i64.add
    local.get $nanos i64.extend_i32_u i64.sub
    local.get $microseconds i64.extend_i32_u i64.const 1000 i64.mul i64.ge_s i32.and))
(assert_return (invoke $usleep "sleep" (i32.const 0)) (i32.const 0))
(assert_return (invoke $usleep "errno") (i32.const 73))
(assert_return (invoke $usleep "sleep" (i32.const 1000000)) (i32.const -1))
(assert_return (invoke $usleep "errno") (i32.const 22))
(assert_return (invoke $usleep "sleep" (i32.const -1)) (i32.const -1))
(assert_return (invoke $usleep "errno") (i32.const 22))
(assert_return (invoke $usleep "elapsed" (i32.const 1)) (i32.const 1))
(assert_return (invoke $usleep "elapsed" (i32.const 1000)) (i32.const 1))
(assert_return (invoke $usleep "elapsed" (i32.const 20000)) (i32.const 1))
