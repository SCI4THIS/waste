;; Binary32 log2 through the installed shared libc, never a host math import.
(module $math
  (import "libc" "log2f" (func $log2f (param f32) (result f32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (import "waste-runtime" "memory" (memory 4))
  (export "log2f" (func $log2f))
  (func (export "error") (param $x f32) (result i32)
    call $errno i32.const 73 i32.store
    local.get $x call $log2f drop call $errno i32.load)
  (func (export "quiet-nan") (param $bits i32) (result i32)
    local.get $bits f32.reinterpret_i32 call $log2f i32.reinterpret_f32
    i32.const 0x7fc00000 i32.and i32.const 0x7fc00000 i32.eq)
  (func (export "near") (param $x f32) (param $expected f32)
                         (param $tolerance f64) (result i32)
    call $errno i32.const 73 i32.store
    local.get $x call $log2f f64.promote_f32
    local.get $expected f64.promote_f32 f64.sub f64.abs
    local.get $tolerance f64.le
    call $errno i32.load i32.const 73 i32.eq i32.and)
  ;; Check every positive binary32 power, independently constructing normal
  ;; and subnormal representations. Successful results must preserve errno.
  (func (export "all-powers") (result i32)
    (local $exponent i32) (local $bits i32)
    i32.const -149 local.set $exponent
    loop $next
      local.get $exponent i32.const -126 i32.lt_s
      if
        i32.const 1 local.get $exponent i32.const 149 i32.add i32.shl
        local.set $bits
      else
        local.get $exponent i32.const 127 i32.add i32.const 23 i32.shl
        local.set $bits
      end
      call $errno i32.const 73 i32.store
      local.get $bits f32.reinterpret_i32 call $log2f
      local.get $exponent f32.convert_i32_s f32.ne
      call $errno i32.load i32.const 73 i32.ne i32.or
      if i32.const 0 return end
      local.get $exponent i32.const 1 i32.add local.tee $exponent
      i32.const 127 i32.le_s br_if $next
    end
    i32.const 1))

(assert_return (invoke "all-powers") (i32.const 1))
(assert_return (invoke "log2f" (f32.const 0x1p-149)) (f32.const -149))
(assert_return (invoke "log2f" (f32.const 0x1p-126)) (f32.const -126))
(assert_return (invoke "log2f" (f32.const 0x1p-1)) (f32.const -1))
(assert_return (invoke "log2f" (f32.const 1)) (f32.const 0))
(assert_return (invoke "log2f" (f32.const 2)) (f32.const 1))
(assert_return (invoke "log2f" (f32.const 0x1p127)) (f32.const 127))

;; Domain/pole values and NaN quieting, with errno tested independently.
(assert_return (invoke "log2f" (f32.const 0)) (f32.const -inf))
(assert_return (invoke "log2f" (f32.const -0)) (f32.const -inf))
(assert_return (invoke "log2f" (f32.const -1)) (f32.const nan:arithmetic))
(assert_return (invoke "log2f" (f32.const -0x1p-149)) (f32.const nan:arithmetic))
(assert_return (invoke "log2f" (f32.const -inf)) (f32.const nan:arithmetic))
(assert_return (invoke "log2f" (f32.const inf)) (f32.const inf))
(assert_return (invoke "log2f" (f32.const nan)) (f32.const nan:arithmetic))
(assert_return (invoke "quiet-nan" (i32.const 0x7f800001)) (i32.const 1))
(assert_return (invoke "quiet-nan" (i32.const 0xff800001)) (i32.const 1))
(assert_return (invoke "quiet-nan" (i32.const 0x7fc12345)) (i32.const 1))
(assert_return (invoke "quiet-nan" (i32.const 0xffc12345)) (i32.const 1))
(assert_return (invoke "error" (f32.const 0)) (i32.const 34))
(assert_return (invoke "error" (f32.const -0)) (i32.const 34))
(assert_return (invoke "error" (f32.const -1)) (i32.const 33))
(assert_return (invoke "error" (f32.const -0x1p-149)) (i32.const 33))
(assert_return (invoke "error" (f32.const -inf)) (i32.const 33))
(assert_return (invoke "error" (f32.const 0x1p-149)) (i32.const 73))
(assert_return (invoke "error" (f32.const 1)) (i32.const 73))
(assert_return (invoke "error" (f32.const 2)) (i32.const 73))
(assert_return (invoke "error" (f32.const inf)) (i32.const 73))
(assert_return (invoke "error" (f32.const nan)) (i32.const 73))

;; 100-digit Decimal ln(x)/ln(2) references; results within one binary32 ULP.
;; Includes neighbors of one and both range-reduction boundaries.
(assert_return (invoke "near" (f32.const 0x1.8000000000000p-148) (f32.const -0x1.26d4800000000p+7) (f64.const 0x1.0000000000000p-16)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.2345000000000p-133) (f32.const -0x1.09a0a80000000p+7) (f64.const 0x1.0000000000000p-16)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.fffffc0000000p-127) (f32.const -0x1.f800000000000p+6) (f64.const 0x1.0000000000000p-17)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.0000020000000p-126) (f32.const -0x1.f800000000000p+6) (f64.const 0x1.0000000000000p-17)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.02468a0000000p-126) (f32.const -0x1.f7f2ee0000000p+6) (f64.const 0x1.0000000000000p-17)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.99999a0000000p-4) (f32.const -0x1.a934f00000000p+1) (f64.const 0x1.0000000000000p-22)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.99999a0000000p-3) (f32.const -0x1.2934f00000000p+1) (f64.const 0x1.0000000000000p-22)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.3333340000000p-2) (f32.const -0x1.bca9c60000000p+0) (f64.const 0x1.0000000000000p-23)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.0000020000000p-1) (f32.const -0x1.fffffa0000000p-1) (f64.const 0x1.0000000000000p-24)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6a09e40000000p-1) (f32.const -0x1.0000040000000p-1) (f64.const 0x1.0000000000000p-24)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6a09e60000000p-1) (f32.const -0x1.0000000000000p-1) (f64.const 0x1.0000000000000p-24)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6a09e80000000p-1) (f32.const -0x1.fffffa0000000p-2) (f64.const 0x1.0000000000000p-25)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.fffffc0000000p-1) (f32.const -0x1.7154780000000p-23) (f64.const 0x1.0000000000000p-46)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.fffffe0000000p-1) (f32.const -0x1.7154780000000p-24) (f64.const 0x1.0000000000000p-47)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.0000020000000p+0) (f32.const 0x1.7154740000000p-23) (f64.const 0x1.0000000000000p-46)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.0000040000000p+0) (f32.const 0x1.7154740000000p-22) (f64.const 0x1.0000000000000p-45)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.0000a80000000p+0) (f32.const 0x1.e4be3c0000000p-17) (f64.const 0x1.0000000000000p-40)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.028f5c0000000p+0) (f32.const 0x1.d664d00000000p-7) (f64.const 0x1.0000000000000p-30)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.2000000000000p+0) (f32.const 0x1.5c01a40000000p-3) (f64.const 0x1.0000000000000p-26)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.4000000000000p+0) (f32.const 0x1.49a7840000000p-2) (f64.const 0x1.0000000000000p-25)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6666660000000p+0) (f32.const 0x1.f113ba0000000p-2) (f64.const 0x1.0000000000000p-25)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6a09e40000000p+0) (f32.const 0x1.fffff60000000p-2) (f64.const 0x1.0000000000000p-25)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6a09e60000000p+0) (f32.const 0x1.fffffe0000000p-2) (f64.const 0x1.0000000000000p-25)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.6a09e80000000p+0) (f32.const 0x1.0000040000000p-1) (f64.const 0x1.0000000000000p-24)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.8000000000000p+0) (f32.const 0x1.2b80340000000p-1) (f64.const 0x1.0000000000000p-24)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.c000000000000p+0) (f32.const 0x1.9d5da00000000p-1) (f64.const 0x1.0000000000000p-24)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.5bf0a80000000p+1) (f32.const 0x1.7154760000000p+0) (f64.const 0x1.0000000000000p-23)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.921fb60000000p+1) (f32.const 0x1.a6c8740000000p+0) (f64.const 0x1.0000000000000p-23)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.4000000000000p+3) (f32.const 0x1.a934f00000000p+1) (f64.const 0x1.0000000000000p-22)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.9000000000000p+6) (f32.const 0x1.a934f00000000p+2) (f64.const 0x1.0000000000000p-21)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.93e5940000000p+99) (f32.const 0x1.8ea1a20000000p+6) (f64.const 0x1.0000000000000p-17)) (i32.const 1))
(assert_return (invoke "near" (f32.const 0x1.fffffe0000000p+127) (f32.const 0x1.0000000000000p+7) (f64.const 0x1.0000000000000p-16)) (i32.const 1))
