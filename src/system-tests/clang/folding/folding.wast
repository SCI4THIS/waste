;; Production optional folding outcomes, exact values and retained calls.
(module $folding
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/folding.bin\00")
  (data (i32.const 180128) "/tmp/folding-status.txt\00")
  (func $capture (param $path i32) (param $destination i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $length i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $destination local.get $capacity call $read local.set $length
    local.get $fd call $close drop local.get $length)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 184000 i32.const 4096 call $capture)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 190000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 190000 i32.load16_u i32.const 0x3433 i32.eq i32.and
    i32.const 190002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func $value (export "value") (param $index i32) (result i64)
    i32.const 184000 local.get $index i32.const 8 i32.mul i32.add i64.load)
  (func (export "exp-double-declined") (result i32)
    i32.const 0 call $value i64.const 0 i64.eq
    i32.const 1 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "exp2-double-declined") (result i32)
    i32.const 2 call $value i64.const 0 i64.eq
    i32.const 3 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "exp10-double-declined") (result i32)
    i32.const 4 call $value i64.const 0 i64.eq
    i32.const 5 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "log-general-declined") (result i32)
    i32.const 6 call $value i64.const 0 i64.eq
    i32.const 7 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "log10-general-declined") (result i32)
    i32.const 8 call $value i64.const 0 i64.eq
    i32.const 9 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "sin-declined") (result i32)
    i32.const 10 call $value i64.const 0 i64.eq
    i32.const 11 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "cos-declined") (result i32)
    i32.const 12 call $value i64.const 0 i64.eq
    i32.const 13 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "sinh-declined") (result i32)
    i32.const 14 call $value i64.const 0 i64.eq
    i32.const 15 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "cosh-declined") (result i32)
    i32.const 16 call $value i64.const 0 i64.eq
    i32.const 17 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "atan-general-declined") (result i32)
    i32.const 18 call $value i64.const 0 i64.eq
    i32.const 19 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "exp-float-declined") (result i32)
    i32.const 20 call $value i64.const 0 i64.eq
    i32.const 21 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "log-half-declined") (result i32)
    i32.const 22 call $value i64.const 0 i64.eq
    i32.const 23 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "pow-double-declined") (result i32)
    i32.const 24 call $value i64.const 0 i64.eq
    i32.const 25 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "pow-float-declined") (result i32)
    i32.const 26 call $value i64.const 0 i64.eq
    i32.const 27 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "log-one-exact") (result i32)
    i32.const 28 call $value i64.const 1 i64.eq
    i32.const 29 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "log-zero-exact") (result i32)
    i32.const 30 call $value i64.const 1 i64.eq
    i32.const 31 call $value i64.const 0xfff0000000000000 i64.eq i32.and)
  (func (export "log-negative-exact") (result i32)
    i32.const 32 call $value i64.const 1 i64.eq
    i32.const 33 call $value i64.const 0x7ff8000000000000 i64.eq i32.and)
  (func (export "log10-one-exact") (result i32)
    i32.const 34 call $value i64.const 1 i64.eq
    i32.const 35 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "log10-zero-exact") (result i32)
    i32.const 36 call $value i64.const 1 i64.eq
    i32.const 37 call $value i64.const 0xfff0000000000000 i64.eq i32.and)
  (func (export "log10-negative-exact") (result i32)
    i32.const 38 call $value i64.const 1 i64.eq
    i32.const 39 call $value i64.const 0x7ff8000000000000 i64.eq i32.and)
  (func (export "atan-negative-zero-exact") (result i32)
    i32.const 40 call $value i64.const 1 i64.eq
    i32.const 41 call $value i64.const 0x8000000000000000 i64.eq i32.and)
  (func (export "log2-real-provider") (result i32)
    i32.const 42 call $value i64.const 1 i64.eq
    i32.const 43 call $value i64.const 0x4008000000000000 i64.eq i32.and)
  (func (export "sqrt-real-provider") (result i32)
    i32.const 44 call $value i64.const 1 i64.eq
    i32.const 45 call $value i64.const 0x4008000000000000 i64.eq i32.and)
  (func (export "fabs-apfloat") (result i32)
    i32.const 46 call $value i64.const 1 i64.eq
    i32.const 47 call $value i64.const 0x4004000000000000 i64.eq i32.and)
  (func (export "floor-apfloat") (result i32)
    i32.const 48 call $value i64.const 1 i64.eq
    i32.const 49 call $value i64.const 0xc000000000000000 i64.eq i32.and)
  (func (export "powi-apfloat") (result i32)
    i32.const 50 call $value i64.const 1 i64.eq
    i32.const 51 call $value i64.const 0x4020000000000000 i64.eq i32.and)
  (func (export "poison-still-propagates") (result i32)
    i32.const 52 call $value i64.const 3 i64.eq
    i32.const 53 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "vector-partial-log-declined") (result i32)
    i32.const 54 call $value i64.const 0 i64.eq
    i32.const 55 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "vector-all-exact-folds") (result i32)
    i32.const 56 call $value i64.const 4 i64.eq
    i32.const 57 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "vector-log-one-exact") (result i32)
    i32.const 58 call $value i64.const 1 i64.eq
    i32.const 59 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "vector-log-zero-exact") (result i32)
    i32.const 60 call $value i64.const 1 i64.eq
    i32.const 61 call $value i64.const 0xfff0000000000000 i64.eq i32.and)
  (func (export "libcall-exp-declined") (result i32)
    i32.const 76 call $value i64.const 0 i64.eq
    i32.const 77 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-exp2f-declined") (result i32)
    i32.const 78 call $value i64.const 0 i64.eq
    i32.const 79 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-log-declined") (result i32)
    i32.const 80 call $value i64.const 0 i64.eq
    i32.const 81 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-pow-declined") (result i32)
    i32.const 82 call $value i64.const 0 i64.eq
    i32.const 83 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-acos-declined") (result i32)
    i32.const 85 call $value i64.const 0 i64.eq
    i32.const 86 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-asin-declined") (result i32)
    i32.const 87 call $value i64.const 0 i64.eq
    i32.const 88 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-atan2-declined") (result i32)
    i32.const 89 call $value i64.const 0 i64.eq
    i32.const 90 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-logb-declined") (result i32)
    i32.const 91 call $value i64.const 0 i64.eq
    i32.const 92 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-log1p-declined") (result i32)
    i32.const 93 call $value i64.const 0 i64.eq
    i32.const 94 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-erf-declined") (result i32)
    i32.const 95 call $value i64.const 0 i64.eq
    i32.const 96 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-tan-declined") (result i32)
    i32.const 97 call $value i64.const 0 i64.eq
    i32.const 98 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-tanh-declined") (result i32)
    i32.const 99 call $value i64.const 0 i64.eq
    i32.const 100 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-sqrt-provider") (result i32)
    i32.const 102 call $value i64.const 1 i64.eq
    i32.const 103 call $value i64.const 0x4008000000000000 i64.eq i32.and)
  (func (export "missing-tli-declined") (result i32)
    i32.const 104 call $value i64.const 0 i64.eq
    i32.const 105 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "nondeterministic-forbidden-declined") (result i32)
    i32.const 106 call $value i64.const 0 i64.eq
    i32.const 107 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "nobuiltin-declined") (result i32)
    i32.const 108 call $value i64.const 0 i64.eq
    i32.const 109 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "target-unavailable-declined") (result i32)
    i32.const 110 call $value i64.const 0 i64.eq
    i32.const 111 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "libcall-fmod-apfloat") (result i32)
    i32.const 112 call $value i64.const 1 i64.eq
    i32.const 113 call $value i64.const 0x3ff0000000000000 i64.eq i32.and)
  (func (export "retained-pow-first-argument") (result i32)
    i32.const 115 call $value i64.const 1 i64.eq
    i32.const 116 call $value i64.const 0x4000000000000000 i64.eq i32.and)
  (func (export "retained-pow-second-argument") (result i32)
    i32.const 117 call $value i64.const 1 i64.eq
    i32.const 118 call $value i64.const 0x4008000000000000 i64.eq i32.and)
  (func (export "amdgpu-sin-general-declined") (result i32)
    i32.const 62 call $value i64.const 0 i64.eq
    i32.const 63 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "amdgpu-cos-general-declined") (result i32)
    i32.const 64 call $value i64.const 0 i64.eq
    i32.const 65 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "amdgpu-sin-quarter-exact") (result i32)
    i32.const 66 call $value i64.const 1 i64.eq
    i32.const 67 call $value i64.const 0x000000003f800000 i64.eq i32.and)
  (func (export "amdgpu-cos-quarter-exact") (result i32)
    i32.const 68 call $value i64.const 1 i64.eq
    i32.const 69 call $value i64.const 0x0000000000000000 i64.eq i32.and)
  (func (export "amdgpu-sin-negative-quarter-exact") (result i32)
    i32.const 70 call $value i64.const 1 i64.eq
    i32.const 71 call $value i64.const 0x00000000bf800000 i64.eq i32.and)
  (func (export "amdgpu-cos-zero-exact") (result i32)
    i32.const 72 call $value i64.const 1 i64.eq
    i32.const 73 call $value i64.const 0x000000003f800000 i64.eq i32.and)
  (func (export "amdgpu-out-of-range-declined") (result i32)
    i32.const 74 call $value i64.const 0 i64.eq
    i32.const 75 call $value i64.const 0x0000000000000000 i64.eq i32.and)
)
(assert_return (invoke "capture") (i32.const 952))
(assert_return (invoke "status") (i32.const 1))
(assert_return (invoke "exp-double-declined") (i32.const 1))
(assert_return (invoke "exp2-double-declined") (i32.const 1))
(assert_return (invoke "exp10-double-declined") (i32.const 1))
(assert_return (invoke "log-general-declined") (i32.const 1))
(assert_return (invoke "log10-general-declined") (i32.const 1))
(assert_return (invoke "sin-declined") (i32.const 1))
(assert_return (invoke "cos-declined") (i32.const 1))
(assert_return (invoke "sinh-declined") (i32.const 1))
(assert_return (invoke "cosh-declined") (i32.const 1))
(assert_return (invoke "atan-general-declined") (i32.const 1))
(assert_return (invoke "exp-float-declined") (i32.const 1))
(assert_return (invoke "log-half-declined") (i32.const 1))
(assert_return (invoke "pow-double-declined") (i32.const 1))
(assert_return (invoke "pow-float-declined") (i32.const 1))
(assert_return (invoke "log-one-exact") (i32.const 1))
(assert_return (invoke "log-zero-exact") (i32.const 1))
(assert_return (invoke "log-negative-exact") (i32.const 1))
(assert_return (invoke "log10-one-exact") (i32.const 1))
(assert_return (invoke "log10-zero-exact") (i32.const 1))
(assert_return (invoke "log10-negative-exact") (i32.const 1))
(assert_return (invoke "atan-negative-zero-exact") (i32.const 1))
(assert_return (invoke "log2-real-provider") (i32.const 1))
(assert_return (invoke "sqrt-real-provider") (i32.const 1))
(assert_return (invoke "fabs-apfloat") (i32.const 1))
(assert_return (invoke "floor-apfloat") (i32.const 1))
(assert_return (invoke "powi-apfloat") (i32.const 1))
(assert_return (invoke "poison-still-propagates") (i32.const 1))
(assert_return (invoke "vector-partial-log-declined") (i32.const 1))
(assert_return (invoke "vector-all-exact-folds") (i32.const 1))
(assert_return (invoke "vector-log-one-exact") (i32.const 1))
(assert_return (invoke "vector-log-zero-exact") (i32.const 1))
(assert_return (invoke "libcall-exp-declined") (i32.const 1))
(assert_return (invoke "libcall-exp2f-declined") (i32.const 1))
(assert_return (invoke "libcall-log-declined") (i32.const 1))
(assert_return (invoke "libcall-pow-declined") (i32.const 1))
(assert_return (invoke "libcall-acos-declined") (i32.const 1))
(assert_return (invoke "libcall-asin-declined") (i32.const 1))
(assert_return (invoke "libcall-atan2-declined") (i32.const 1))
(assert_return (invoke "libcall-logb-declined") (i32.const 1))
(assert_return (invoke "libcall-log1p-declined") (i32.const 1))
(assert_return (invoke "libcall-erf-declined") (i32.const 1))
(assert_return (invoke "libcall-tan-declined") (i32.const 1))
(assert_return (invoke "libcall-tanh-declined") (i32.const 1))
(assert_return (invoke "libcall-sqrt-provider") (i32.const 1))
(assert_return (invoke "missing-tli-declined") (i32.const 1))
(assert_return (invoke "nondeterministic-forbidden-declined") (i32.const 1))
(assert_return (invoke "nobuiltin-declined") (i32.const 1))
(assert_return (invoke "target-unavailable-declined") (i32.const 1))
(assert_return (invoke "libcall-fmod-apfloat") (i32.const 1))
(assert_return (invoke "retained-pow-first-argument") (i32.const 1))
(assert_return (invoke "retained-pow-second-argument") (i32.const 1))
(assert_return (invoke "value" (i32.const 84)) (i64.const 0))
(assert_return (invoke "value" (i32.const 114)) (i64.const 18))
(assert_return (invoke "value" (i32.const 101)) (i64.const 17))
(assert_return (invoke "amdgpu-sin-general-declined") (i32.const 1))
(assert_return (invoke "amdgpu-cos-general-declined") (i32.const 1))
(assert_return (invoke "amdgpu-sin-quarter-exact") (i32.const 1))
(assert_return (invoke "amdgpu-cos-quarter-exact") (i32.const 1))
(assert_return (invoke "amdgpu-sin-negative-quarter-exact") (i32.const 1))
(assert_return (invoke "amdgpu-cos-zero-exact") (i32.const 1))
(assert_return (invoke "amdgpu-out-of-range-declined") (i32.const 1))
