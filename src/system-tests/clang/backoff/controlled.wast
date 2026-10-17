;; Actual LLVM backoff observations; expectations are exclusively in WAST.
(module $backoff
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/backoff-controlled.bin\00")
  (data (i32.const 180064) "/tmp/backoff-controlled-status.txt\00")
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
  (func (export "value") (param $index i32) (result i64)
    i32.const 182000 local.get $index i32.const 8 i32.mul i32.add i64.load)
  (func (export "jitter-range-and-variation") (result i32)
    (local $index i32) (local $first i64) (local $sample i64)
    (local $valid i32) (local $changed i32)
    i32.const 182040 i64.load local.set $first
    i32.const 1 local.set $valid
    (loop $samples
      i32.const 182040 local.get $index i32.const 8 i32.mul i32.add i64.load local.set $sample
      local.get $valid local.get $sample i64.const 17 i64.ge_u i32.and
      local.get $sample i64.const 137 i64.le_u i32.and local.set $valid
      local.get $changed local.get $sample local.get $first i64.ne i32.or local.set $changed
      local.get $index i32.const 1 i32.add local.tee $index i32.const 32 i32.lt_u br_if $samples)
    local.get $valid local.get $changed i32.and)

)
(assert_return (invoke $backoff "capture") (i32.const 392))
(assert_return (invoke $backoff "exit-status") (i32.const 1))
(assert_return (invoke $backoff "value" (i32.const 0)) (i64.const 1))
(assert_return (invoke $backoff "value" (i32.const 1)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 2)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 3)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 4)) (i64.const 4294967295))
(assert_return (invoke $backoff "value" (i32.const 37)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 38)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 39)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 40)) (i64.const 1))
(assert_return (invoke $backoff "value" (i32.const 41)) (i64.const 1000000000))
(assert_return (invoke $backoff "value" (i32.const 42)) (i64.const 1))
(assert_return (invoke $backoff "value" (i32.const 43)) (i64.const 1000000000))
(assert_return (invoke $backoff "value" (i32.const 44)) (i64.const 1))
(assert_return (invoke $backoff "value" (i32.const 45)) (i64.const 500000000))
(assert_return (invoke $backoff "value" (i32.const 46)) (i64.const 0))
(assert_return (invoke $backoff "value" (i32.const 47)) (i64.const 1))
(assert_return (invoke $backoff "value" (i32.const 48)) (i64.const 2500000000))
(assert_return (invoke $backoff "jitter-range-and-variation") (i32.const 1))
