;; Production helper formatting with shared libc, retained across a sleep yield.
(module $timestamp
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (global $bytes (mut i32) (i32.const 0))
  (data (i32.const 180000) "/tmp/timestamp.bin\00")
  (data (i32.const 180064) "/tmp/timestamp-reference.bin\00")
  (data (i32.const 180128) "/tmp/timestamp-status.txt\00")
  (data (i32.const 180192) "/tmp/pragma-sleep.bin\00")
  (data (i32.const 181000) "Thu Jan  1 00:00:00 1970")
  (data (i32.const 181064) "Wed Dec 31 23:59:59 1969")
  (data (i32.const 181128) "Tue Jan 19 03:14:07 2038")
  (data (i32.const 181192) "Tue Jan 19 03:14:08 2038")
  (data (i32.const 181256) "Tue Feb 29 12:34:56 2000")
  (data (i32.const 181320) "Fri Dec 31 23:59:59 9999")
  (data (i32.const 181384) "Mon Jan  1 00:00:00 1")
  (func $capture (param $path i32) (param $buffer i32) (result i32)
    (local $fd i32) (local $got i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $buffer i32.const 8192 call $read local.set $got
    local.get $fd call $close drop local.get $got)
  (func (export "load") (result i32)
    i32.const 180000 i32.const 190000 call $capture global.set $bytes
    global.get $bytes i32.const 8 i32.ge_s)
  (func (export "count") (result i32) i32.const 190000 i32.load)
  (func (export "sleep-status") (result i32) i32.const 190004 i32.load)
  (func (export "rendered") (param $index i32) (param $text i32) (param $length i32) (result i32)
    (local $count i32) (local $offset i32) (local $at i32) (local $actual i32)
    i32.const 190000 i32.load local.tee $count i32.const 64 i32.gt_u
    if i32.const 0 return end
    local.get $index local.get $count i32.ge_u if i32.const 0 return end
    i32.const 8 local.get $count i32.const 4 i32.mul i32.add local.set $offset
    block $done loop $lengths
      local.get $at local.get $index i32.ge_u br_if $done
      local.get $offset i32.const 190008 local.get $at i32.const 4 i32.mul i32.add i32.load
      i32.add local.set $offset
      local.get $at i32.const 1 i32.add local.set $at br $lengths
    end end
    i32.const 190008 local.get $index i32.const 4 i32.mul i32.add i32.load local.set $actual
    local.get $actual local.get $length i32.ne if i32.const 0 return end
    local.get $offset global.get $bytes i32.gt_u if i32.const 0 return end
    local.get $length global.get $bytes local.get $offset i32.sub i32.gt_u
    if i32.const 0 return end
    i32.const 190000 local.get $offset i32.add local.get $text local.get $length
    call $memcmp i32.eqz)
  (func (export "native-reference") (result i32)
    i32.const 180064 i32.const 206000 call $capture global.get $bytes i32.ne
    if i32.const 0 return end
    i32.const 190000 i32.const 206000 global.get $bytes call $memcmp i32.eqz)
  (func (export "pragma-delay") (result i32)
    i32.const 180192 i32.const 206000 call $capture i32.const 8 i32.eq
    i32.const 206000 i64.load i64.const 100000000 i64.ge_s i32.and)
  (func (export "exit-status") (result i32)
    i32.const 180128 i32.const 206000 call $capture i32.const 3 i32.eq
    i32.const 206000 i32.load16_u i32.const 0x3733 i32.eq i32.and
    i32.const 206002 i32.load8_u i32.const 10 i32.eq i32.and))
(assert_return (invoke $timestamp "load") (i32.const 1))
(assert_return (invoke $timestamp "count") (i32.const 9))
(assert_return (invoke $timestamp "sleep-status") (i32.const 0))
(assert_return (invoke $timestamp "rendered" (i32.const 0) (i32.const 181000) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 1) (i32.const 181064) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 2) (i32.const 181128) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 3) (i32.const 181192) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 4) (i32.const 181256) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 5) (i32.const 181320) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 6) (i32.const 181384) (i32.const 21)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 7) (i32.const 181448) (i32.const 0)) (i32.const 1))
(assert_return (invoke $timestamp "rendered" (i32.const 8) (i32.const 181000) (i32.const 24)) (i32.const 1))
(assert_return (invoke $timestamp "native-reference") (i32.const 1))
(assert_return (invoke $timestamp "pragma-delay") (i32.const 1))
(assert_return (invoke $timestamp "exit-status") (i32.const 1))
