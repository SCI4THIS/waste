;; Compiler-rt i128 shift ABI used by guest cryptography.
(module $shift
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "__ashlti3" (func $shift (param i32 i64 i64 i32)))
  (func (export "check") (param $low i64) (param $high i64) (param $n i32)
       (param $want_low i64) (param $want_high i64) (result i32)
    (call $shift (i32.const 180000) (local.get $low) (local.get $high) (local.get $n))
    (i32.and (i64.eq (i64.load (i32.const 180000)) (local.get $want_low))
             (i64.eq (i64.load (i32.const 180008)) (local.get $want_high)))))
(assert_return (invoke $shift "check" (i64.const 1) (i64.const 2) (i32.const 0) (i64.const 1) (i64.const 2)) (i32.const 1))
(assert_return (invoke $shift "check" (i64.const -1) (i64.const 0) (i32.const 1) (i64.const -2) (i64.const 1)) (i32.const 1))
(assert_return (invoke $shift "check" (i64.const -1) (i64.const 0) (i32.const 63) (i64.const 0x8000000000000000) (i64.const 0x7fffffffffffffff)) (i32.const 1))
(assert_return (invoke $shift "check" (i64.const 7) (i64.const 2) (i32.const 64) (i64.const 0) (i64.const 7)) (i32.const 1))
(assert_return (invoke $shift "check" (i64.const 1) (i64.const -1) (i32.const 127) (i64.const 0) (i64.const 0x8000000000000000)) (i32.const 1))
