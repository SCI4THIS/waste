;; Additional exact clock assertions for the controlled kernel-clock transport.
;; filesystem.wast checks every stat timestamp against these observed bounds.
(module $filesystem_clock
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/filesystem.bin\00")
  (func (export "capture") (result i32)
    (local $fd i32) (local $count i32)
    i32.const 180000 i32.const 0 i32.const 0 call $open local.set $fd
    local.get $fd i32.const 182000 i32.const 2048 call $read local.set $count
    local.get $fd call $close drop local.get $count)
  (func (export "clock-value") (param $field i32) (result i64)
    i32.const 182160 local.get $field i32.const 8 i32.mul i32.add i64.load)
)
(assert_return (invoke $filesystem_clock "capture") (i32.const 1104))
(assert_return (invoke $filesystem_clock "clock-value" (i32.const 0)) (i64.const 4294967313))
(assert_return (invoke $filesystem_clock "clock-value" (i32.const 1)) (i64.const 123456789))
(assert_return (invoke $filesystem_clock "clock-value" (i32.const 2)) (i64.const 4294967313))
(assert_return (invoke $filesystem_clock "clock-value" (i32.const 3)) (i64.const 123456789))
