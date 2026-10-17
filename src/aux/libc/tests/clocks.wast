;; The production shared libc and injected kernel clocks, through Bash.
(module $clocks
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "clock_gettime" (func $clock (param i32 i32) (result i32)))
  (import "libc" "nanosleep" (func $sleep (param i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (func $normalized (param $id i32) (result i32)
    i32.const 180000 i32.const 0x12345678 i32.store
    i32.const 180020 i32.const 0x12345678 i32.store
    i32.const 180016 i32.const -1 i32.store
    local.get $id i32.const 180004 call $clock i32.eqz
    i32.const 180004 i64.load i64.const 0 i64.ge_s i32.and
    i32.const 180012 i32.load i32.const 1000000000 i32.lt_u i32.and
    i32.const 180016 i32.load i32.eqz i32.and
    i32.const 180000 i32.load i32.const 0x12345678 i32.eq i32.and
    i32.const 180020 i32.load i32.const 0x12345678 i32.eq i32.and)
  (func (export "realtime-layout") (result i32) i32.const 0 call $normalized)
  (func (export "monotonic-layout") (result i32) i32.const 1 call $normalized)
  (func (export "monotonic-progresses-after-sleep") (result i32)
    (local $seconds i64) (local $nanos i32) (local $ok i32)
    i32.const 1 i32.const 180032 call $clock i32.eqz local.set $ok
    i32.const 180032 i64.load local.set $seconds
    i32.const 180040 i32.load local.set $nanos
    i32.const 180048 i64.const 0 i64.store
    i32.const 180056 i32.const 2000000 i32.store
    i32.const 180060 i32.const 0 i32.store
    i32.const 180048 i32.const 0 call $sleep i32.eqz local.get $ok i32.and local.set $ok
    i32.const 1 i32.const 180032 call $clock i32.eqz local.get $ok i32.and
    i32.const 180032 i64.load local.get $seconds i64.sub i64.const 1000000000 i64.mul
    i32.const 180040 i32.load i64.extend_i32_u i64.add
    local.get $nanos i64.extend_i32_u i64.sub i64.const 2000000 i64.ge_s i32.and)
  (func (export "success-preserves-errno") (result i32)
    call $errno i32.const 17 i32.store
    i32.const 0 i32.const 180032 call $clock i32.eqz
    i32.const 1 i32.const 180032 call $clock i32.eqz i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func $invalid (param $id i32) (result i32)
    i32.const 180064 i64.const 0x123456789abcdef0 i64.store
    i32.const 180072 i64.const 0x123456789abcdef0 i64.store
    local.get $id i32.const 180064 call $clock i32.const -1 i32.eq
    call $errno i32.load i32.const 22 i32.eq i32.and
    i32.const 180064 i64.load i64.const 0x123456789abcdef0 i64.eq i32.and
    i32.const 180072 i64.load i64.const 0x123456789abcdef0 i64.eq i32.and)
  (func (export "invalid-clock-ids") (result i32)
    i32.const -1 call $invalid
    i32.const 2 call $invalid i32.and
    i32.const 3 call $invalid i32.and
    i32.const 0x7fffffff call $invalid i32.and)
  (func (export "null-destination") (result i32)
    i32.const 0 i32.const 0 call $clock i32.const -1 i32.eq
    call $errno i32.load i32.const 14 i32.eq i32.and
    i32.const 1 i32.const 0 call $clock i32.const -1 i32.eq i32.and
    call $errno i32.load i32.const 14 i32.eq i32.and)
  (func (export "overflow-destination") (result i32)
    i32.const 1 i32.const -8 call $clock i32.const -1 i32.eq
    call $errno i32.load i32.const 14 i32.eq i32.and)
  (func (export "partial-range-does-not-write") (result i32)
    (local $tail i32)
    memory.size i32.const 16 i32.shl i32.const 8 i32.sub local.set $tail
    local.get $tail i64.const 0x123456789abcdef0 i64.store
    i32.const 1 local.get $tail call $clock i32.const -1 i32.eq
    call $errno i32.load i32.const 14 i32.eq i32.and
    local.get $tail i64.load i64.const 0x123456789abcdef0 i64.eq i32.and))
(assert_return (invoke $clocks "realtime-layout") (i32.const 1))
(assert_return (invoke $clocks "monotonic-layout") (i32.const 1))
(assert_return (invoke $clocks "monotonic-progresses-after-sleep") (i32.const 1))
(assert_return (invoke $clocks "success-preserves-errno") (i32.const 1))
(assert_return (invoke $clocks "invalid-clock-ids") (i32.const 1))
(assert_return (invoke $clocks "null-destination") (i32.const 1))
(assert_return (invoke $clocks "overflow-destination") (i32.const 1))
(assert_return (invoke $clocks "partial-range-does-not-write") (i32.const 1))
