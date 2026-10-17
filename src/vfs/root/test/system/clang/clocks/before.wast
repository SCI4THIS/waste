;; Deterministic clock ABI checks. Assertions run in the installed Bash.
(module $clock
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "clock_gettime" (func $clock (param i32 i32) (result i32)))
  (import "libc" "gettimeofday" (func $timeofday (param i32 i32) (result i32)))
  (import "libc" "time" (func $time (param i32) (result i64)))
  (func (export "realtime") (result i32)
    i32.const 0 i32.const 180000 call $clock i32.eqz
    i32.const 180000 i64.load i64.const 4294967296 i64.eq i32.and
    i32.const 180008 i32.load i32.const 123456789 i32.eq i32.and
    i32.const 180012 i32.load i32.eqz i32.and)
  (func (export "monotonic") (result i32)
    i32.const 1 i32.const 180000 call $clock i32.eqz
    i32.const 180000 i64.load i64.const 42 i64.eq i32.and
    i32.const 180008 i32.load i32.const 0 i32.eq i32.and)
  (func (export "wall-api-agreement") (result i32)
    i32.const 180016 i32.const 0 call $timeofday i32.eqz
    i32.const 180016 i64.load i64.const 4294967296 i64.eq i32.and
    i32.const 180024 i32.load i32.const 123456 i32.eq i32.and
    i32.const 0 call $time i64.const 4294967296 i64.eq i32.and))
(assert_return (invoke $clock "realtime") (i32.const 1))
(assert_return (invoke $clock "monotonic") (i32.const 1))
(assert_return (invoke $clock "wall-api-agreement") (i32.const 1))
