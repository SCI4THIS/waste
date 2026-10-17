;; Public integer conversions, called against the installed shared libc.
;; Each ASCII vector checks both byte and wchar_t input without duplicating
;; expectations. Checks cover result, end offset, errno and unchanged inputs.
(module $integer
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "strtol" (func $strtol (param i32 i32 i32) (result i32)))
  (import "libc" "strtoul" (func $strtoul (param i32 i32 i32) (result i32)))
  (import "libc" "strtoll" (func $strtoll (param i32 i32 i32) (result i64)))
  (import "libc" "strtoull" (func $strtoull (param i32 i32 i32) (result i64)))
  (import "libc" "strtoimax" (func $strtoimax (param i32 i32 i32) (result i64)))
  (import "libc" "strtoumax" (func $strtoumax (param i32 i32 i32) (result i64)))
  (import "libc" "wcstol" (func $wcstol (param i32 i32 i32) (result i32)))
  (import "libc" "wcstoul" (func $wcstoul (param i32 i32 i32) (result i32)))
  (import "libc" "wcstoll" (func $wcstoll (param i32 i32 i32) (result i64)))
  (import "libc" "wcstoull" (func $wcstoull (param i32 i32 i32) (result i64)))
  (import "libc" "wcstoimax" (func $wcstoimax (param i32 i32 i32) (result i64)))
  (import "libc" "wcstoumax" (func $wcstoumax (param i32 i32 i32) (result i64)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (import "libc" "strlen" (func $length (param i32) (result i32)))
  (import "libc" "wcslen" (func $wide-length (param i32) (result i32)))
  (import "libc" "memcpy" (func $copy (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "2147483647!\00")
  (data (i32.const 180128) "-2147483648!\00")
  (data (i32.const 180256) "2147483648!\00")
  (data (i32.const 180384) "-2147483649!\00")
  (data (i32.const 180512) "4294967295!\00")
  (data (i32.const 180640) "4294967296!\00")
  (data (i32.const 180768) "-1!\00")
  (data (i32.const 180896) "-4294967295!\00")
  (data (i32.const 181024) "-4294967296!\00")
  (data (i32.const 181152) "9223372036854775807!\00")
  (data (i32.const 181280) "-9223372036854775808!\00")
  (data (i32.const 181408) "9223372036854775808!\00")
  (data (i32.const 181536) "-9223372036854775809!\00")
  (data (i32.const 181664) "18446744073709551615!\00")
  (data (i32.const 181792) "18446744073709551616!\00")
  (data (i32.const 181920) "-18446744073709551615!\00")
  (data (i32.const 182048) "-18446744073709551616!\00")
  (data (i32.const 182176) "999999999999999999999!\00")
  (data (i32.const 182304) "-999999999999999999999!\00")
  (data (i32.const 182432) " \09\0a\0b\0c\0d-42!\00")
  (data (i32.const 182560) "+42!\00")
  (data (i32.const 182688) "0778\00")
  (data (i32.const 182816) "0xAbCd!\00")
  (data (i32.const 182944) "-0X8000000000000000!\00")
  (data (i32.const 183072) "0xffffffff!\00")
  (data (i32.const 183200) "0xffffffffffffffff!\00")
  (data (i32.const 183328) "0x10000000000000000!\00")
  (data (i32.const 183456) "\00")
  (data (i32.const 183584) " \09\0d\0a\00")
  (data (i32.const 183712) " +\00")
  (data (i32.const 183840) " -\00")
  (data (i32.const 183968) " \09-x\00")
  (data (i32.const 184096) "z\00")
  (data (i32.const 184224) "0x\00")
  (data (i32.const 184352) "0X\00")
  (data (i32.const 184480) "-0xg\00")
  (data (i32.const 184608) "0x12\00")
  (data (i32.const 184736) "0b11\00")
  (data (i32.const 184864) "42\00")
  (data (i32.const 184992) "1012\00")
  (data (i32.const 185120) "zZ!\00")
  (data (i32.const 185248) "1844674407370955161600000000000000000000!\00")
  (data (i32.const 185376) "99999999999999999999!\00")
  (data (i32.const 185504) "-000!\00")
  (data (i32.const 185632) "10!\00")
  (data (i32.const 185760) "123!\00")
  (data (i32.const 186000) "\31\01\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 186064) "\20\01\00\00\34\00\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 186128) "\2d\01\00\00\34\00\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 186192) "\a0\00\00\00\34\00\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 186256) "\03\20\00\00\34\00\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 186320) "\11\ff\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 186384) "\00\00\00\80\34\00\00\00\00\00\00\00")
  (data (i32.const 186448) "\ff\ff\ff\ff\34\00\00\00\00\00\00\00")
  (data (i32.const 186512) "\34\00\00\00\32\00\00\00\33\01\00\00\34\00\00\00\00\00\00\00")
  (data (i32.const 186576) "\34\00\00\00\32\00\00\00\00\01\00\00\33\00\00\00\00\00\00\00")
  (data (i32.const 186640) "\30\00\00\00\78\01\00\00\66\00\00\00\00\00\00\00")
  (data (i32.const 186704) "\30\00\00\00\78\00\00\00\41\01\00\00\00\00\00\00")
  (data (i32.const 186768) "\31\00\00\00\7a\00\00\00\7a\01\00\00\00\00\00\00")
  (data (i32.const 186832) "\34\00\00\00\32\00\00\00\00\00\00\00\33\00\00\00\00\00\00\00")
  (data (i32.const 186896) "\34\00\00\00\32\00\00\00\ff\ff\ff\ff\33\00\00\00\00\00\00\00")
  (func $convert (param $kind i32) (param $text i32) (param $end i32) (param $base i32) (result i64)
    local.get $kind i32.const 0 i32.eq
    if
      local.get $text local.get $end local.get $base call $strtol i64.extend_i32_s
      return
    end
    local.get $kind i32.const 1 i32.eq
    if
      local.get $text local.get $end local.get $base call $strtoul i64.extend_i32_u
      return
    end
    local.get $kind i32.const 2 i32.eq
    if
      local.get $text local.get $end local.get $base call $strtoll
      return
    end
    local.get $kind i32.const 3 i32.eq
    if
      local.get $text local.get $end local.get $base call $strtoull
      return
    end
    local.get $kind i32.const 4 i32.eq
    if
      local.get $text local.get $end local.get $base call $strtoimax
      return
    end
    local.get $kind i32.const 5 i32.eq
    if
      local.get $text local.get $end local.get $base call $strtoumax
      return
    end
    local.get $kind i32.const 6 i32.eq
    if
      local.get $text local.get $end local.get $base call $wcstol i64.extend_i32_s
      return
    end
    local.get $kind i32.const 7 i32.eq
    if
      local.get $text local.get $end local.get $base call $wcstoul i64.extend_i32_u
      return
    end
    local.get $kind i32.const 8 i32.eq
    if
      local.get $text local.get $end local.get $base call $wcstoll
      return
    end
    local.get $kind i32.const 9 i32.eq
    if
      local.get $text local.get $end local.get $base call $wcstoull
      return
    end
    local.get $kind i32.const 10 i32.eq
    if
      local.get $text local.get $end local.get $base call $wcstoimax
      return
    end
    local.get $kind i32.const 11 i32.eq
    if
      local.get $text local.get $end local.get $base call $wcstoumax
      return
    end
    unreachable)
  (func $check-input (param $kind i32) (param $text i32) (param $base i32)
              (param $expected i64) (param $offset i32) (param $error i32) (result i32)
    (local $bytes i32) (local $width i32)
    local.get $kind i32.const 6 i32.ge_u
    if
      i32.const 4 local.set $width
      local.get $text call $wide-length i32.const 1 i32.add
      local.get $width i32.mul local.set $bytes
    else
      i32.const 1 local.set $width
      local.get $text call $length i32.const 1 i32.add local.set $bytes
    end
    i32.const 190032 local.get $text local.get $bytes call $copy drop
    i32.const 190000 i32.const 0xa5a5a5a5 i32.store
    i32.const 190004 i32.const -1 i32.store
    i32.const 190008 i32.const 0xa5a5a5a5 i32.store
    call $errno i32.const 17 i32.store
    local.get $kind local.get $text i32.const 190004 local.get $base call $convert
    local.get $expected i64.eq
    i32.const 190004 i32.load
    local.get $text local.get $offset local.get $width i32.mul i32.add i32.eq i32.and
    call $errno i32.load local.get $error i32.eq i32.and
    i32.const 190000 i32.load i32.const 0xa5a5a5a5 i32.eq i32.and
    i32.const 190008 i32.load i32.const 0xa5a5a5a5 i32.eq i32.and
    i32.const 190032 local.get $text local.get $bytes call $compare i32.eqz i32.and)
  ;; Widen fixture bytes in WAST; the provider itself does no transcoding.
  (func $prepare-wide (param $text i32) (result i32)
    (local $at i32) (local $byte i32) (local $guard i32)
    i32.const 190508 i32.const 0x76543210 i32.store
    loop $next
      local.get $text local.get $at i32.add i32.load8_u local.set $byte
      i32.const 190512 local.get $at i32.const 4 i32.mul i32.add
      local.get $byte i32.store
      local.get $at i32.const 1 i32.add local.set $at
      local.get $byte br_if $next
    end
    i32.const 190512 local.get $at i32.const 4 i32.mul i32.add local.tee $guard
    i32.const 0x76543210 i32.store
    local.get $guard)
  (func $check (export "check") (param $kind i32) (param $text i32) (param $base i32)
              (param $expected i64) (param $offset i32) (param $error i32) (result i32)
    (local $guard i32)
    local.get $kind local.get $text local.get $base
    local.get $expected local.get $offset local.get $error call $check-input
    i32.eqz if i32.const 0 return end
    local.get $text call $prepare-wide local.set $guard
    local.get $kind i32.const 6 i32.add i32.const 190512 local.get $base
    local.get $expected local.get $offset local.get $error call $check-input
    i32.const 190508 i32.load i32.const 0x76543210 i32.eq i32.and
    local.get $guard i32.load i32.const 0x76543210 i32.eq i32.and)
  ;; Apply each non-ASCII fixture to all six wide entry points.
  (func (export "wide-check") (param $text i32) (param $base i32)
              (param $expected i64) (param $offset i32) (result i32)
    (local $kind i32)
    i32.const 6 local.set $kind
    loop $next
      local.get $kind local.get $text local.get $base
      local.get $expected local.get $offset i32.const 17 call $check-input
      i32.eqz if i32.const 0 return end
      local.get $kind i32.const 1 i32.add local.tee $kind i32.const 12 i32.lt_u br_if $next
    end
    i32.const 1)
  (func (export "all-bases") (param $kind i32) (result i32)
    (local $base i32)
    i32.const 2 local.set $base
    loop $next
      local.get $kind i32.const 185632 local.get $base
      local.get $base i64.extend_i32_u i32.const 2 i32.const 17 call $check
      i32.eqz if i32.const 0 return end
      local.get $base i32.const 1 i32.add local.tee $base i32.const 36 i32.le_u br_if $next
    end
    i32.const 1)
  (func (export "null-end-pointer") (param $kind i32) (result i32)
    call $errno i32.const 17 i32.store
    local.get $kind i32.const 185760 i32.const 0 i32.const 10 call $convert
    i64.const 123 i64.eq
    call $errno i32.load i32.const 17 i32.eq i32.and
    i32.eqz if i32.const 0 return end
    i32.const 185760 call $prepare-wide drop
    local.get $kind i32.const 6 i32.add i32.const 190512 i32.const 0 i32.const 10 call $convert
    i64.const 123 i64.eq
    call $errno i32.load i32.const 17 i32.eq i32.and))
;; long-positive-limit
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 180000) (i32.const 10) (i64.const 2147483647) (i32.const 10) (i32.const 17)) (i32.const 1))
;; long-negative-limit
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 180128) (i32.const 10) (i64.const -2147483648) (i32.const 11) (i32.const 17)) (i32.const 1))
;; long-positive-overflow
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 180256) (i32.const 10) (i64.const 2147483647) (i32.const 10) (i32.const 34)) (i32.const 1))
;; long-negative-overflow
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 180384) (i32.const 10) (i64.const -2147483648) (i32.const 11) (i32.const 34)) (i32.const 1))
;; unsigned-long-limit
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 180512) (i32.const 10) (i64.const 4294967295) (i32.const 10) (i32.const 17)) (i32.const 1))
;; unsigned-long-overflow
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 180640) (i32.const 10) (i64.const 4294967295) (i32.const 10) (i32.const 34)) (i32.const 1))
;; unsigned-long-negation
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 180768) (i32.const 10) (i64.const 4294967295) (i32.const 2) (i32.const 17)) (i32.const 1))
;; unsigned-long-negative-limit
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 180896) (i32.const 10) (i64.const 1) (i32.const 11) (i32.const 17)) (i32.const 1))
;; unsigned-long-negative-overflow
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 181024) (i32.const 10) (i64.const 4294967295) (i32.const 11) (i32.const 34)) (i32.const 1))
;; long-long-positive-limit
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 181152) (i32.const 10) (i64.const 9223372036854775807) (i32.const 19) (i32.const 17)) (i32.const 1))
;; long-long-negative-limit
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 181280) (i32.const 10) (i64.const -9223372036854775808) (i32.const 20) (i32.const 17)) (i32.const 1))
;; long-long-positive-overflow
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 181408) (i32.const 10) (i64.const 9223372036854775807) (i32.const 19) (i32.const 34)) (i32.const 1))
;; long-long-negative-overflow
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 181536) (i32.const 10) (i64.const -9223372036854775808) (i32.const 20) (i32.const 34)) (i32.const 1))
;; unsigned-long-long-limit
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 181664) (i32.const 10) (i64.const 18446744073709551615) (i32.const 20) (i32.const 17)) (i32.const 1))
;; unsigned-long-long-overflow
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 181792) (i32.const 10) (i64.const 18446744073709551615) (i32.const 20) (i32.const 34)) (i32.const 1))
;; unsigned-long-long-negative-one
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 180768) (i32.const 10) (i64.const 18446744073709551615) (i32.const 2) (i32.const 17)) (i32.const 1))
;; unsigned-long-long-negative-limit
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 181920) (i32.const 10) (i64.const 1) (i32.const 21) (i32.const 17)) (i32.const 1))
;; unsigned-long-long-negative-overflow
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 182048) (i32.const 10) (i64.const 18446744073709551615) (i32.const 21) (i32.const 34)) (i32.const 1))
;; max-signed-limit
(assert_return (invoke $integer "check" (i32.const 4) (i32.const 181152) (i32.const 10) (i64.const 9223372036854775807) (i32.const 19) (i32.const 17)) (i32.const 1))
;; max-signed-negative-limit
(assert_return (invoke $integer "check" (i32.const 4) (i32.const 181280) (i32.const 10) (i64.const -9223372036854775808) (i32.const 20) (i32.const 17)) (i32.const 1))
;; max-signed-overflow
(assert_return (invoke $integer "check" (i32.const 4) (i32.const 182176) (i32.const 10) (i64.const 9223372036854775807) (i32.const 21) (i32.const 34)) (i32.const 1))
;; max-unsigned-limit
(assert_return (invoke $integer "check" (i32.const 5) (i32.const 181664) (i32.const 10) (i64.const 18446744073709551615) (i32.const 20) (i32.const 17)) (i32.const 1))
;; max-unsigned-negative-overflow
(assert_return (invoke $integer "check" (i32.const 5) (i32.const 182304) (i32.const 10) (i64.const 18446744073709551615) (i32.const 22) (i32.const 34)) (i32.const 1))
;; all-six-whitespace-bytes
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 182432) (i32.const 10) (i64.const -42) (i32.const 9) (i32.const 17)) (i32.const 1))
;; decimal-auto-base
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 182560) (i32.const 0) (i64.const 42) (i32.const 3) (i32.const 17)) (i32.const 1))
;; octal-auto-base
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 182688) (i32.const 0) (i64.const 63) (i32.const 3) (i32.const 17)) (i32.const 1))
;; hex-auto-base
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 182816) (i32.const 0) (i64.const 43981) (i32.const 6) (i32.const 17)) (i32.const 1))
;; hex-explicit-base
(assert_return (invoke $integer "check" (i32.const 4) (i32.const 182944) (i32.const 16) (i64.const -9223372036854775808) (i32.const 19) (i32.const 17)) (i32.const 1))
;; hex-long-overflow
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 183072) (i32.const 0) (i64.const 2147483647) (i32.const 10) (i32.const 34)) (i32.const 1))
;; hex-unsigned-limit
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 183200) (i32.const 16) (i64.const 18446744073709551615) (i32.const 18) (i32.const 17)) (i32.const 1))
;; hex-unsigned-overflow
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 183328) (i32.const 16) (i64.const 18446744073709551615) (i32.const 19) (i32.const 34)) (i32.const 1))
;; empty-input
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 183456) (i32.const 10) (i64.const 0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; whitespace-only
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 183584) (i32.const 0) (i64.const 0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; plus-only
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 183712) (i32.const 10) (i64.const 0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; minus-only
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 183840) (i32.const 16) (i64.const 0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; non-digit-after-sign
(assert_return (invoke $integer "check" (i32.const 4) (i32.const 183968) (i32.const 0) (i64.const 0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; invalid-first-digit
(assert_return (invoke $integer "check" (i32.const 5) (i32.const 184096) (i32.const 35) (i64.const 0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; hex-prefix-without-digit-auto
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 184224) (i32.const 0) (i64.const 0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; hex-prefix-without-digit-explicit
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 184352) (i32.const 16) (i64.const 0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; hex-prefix-invalid-digit
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 184480) (i32.const 0) (i64.const 0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; hex-prefix-in-decimal
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 184608) (i32.const 10) (i64.const 0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; no-binary-prefix
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 184736) (i32.const 0) (i64.const 0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; invalid-base-one
(assert_return (invoke $integer "check" (i32.const 0) (i32.const 184864) (i32.const 1) (i64.const 0) (i32.const 0) (i32.const 22)) (i32.const 1))
;; invalid-negative-base
(assert_return (invoke $integer "check" (i32.const 2) (i32.const 184864) (i32.const -1) (i64.const 0) (i32.const 0) (i32.const 22)) (i32.const 1))
;; invalid-large-base
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 184864) (i32.const 37) (i64.const 0) (i32.const 0) (i32.const 22)) (i32.const 1))
;; base-two-stops-at-invalid-digit
(assert_return (invoke $integer "check" (i32.const 5) (i32.const 184992) (i32.const 2) (i64.const 5) (i32.const 3) (i32.const 17)) (i32.const 1))
;; base-36-mixed-case
(assert_return (invoke $integer "check" (i32.const 4) (i32.const 185120) (i32.const 36) (i64.const 1295) (i32.const 2) (i32.const 17)) (i32.const 1))
;; overflow-remains-saturated
(assert_return (invoke $integer "check" (i32.const 5) (i32.const 185248) (i32.const 10) (i64.const 18446744073709551615) (i32.const 40) (i32.const 34)) (i32.const 1))
;; multiply-overflow-with-larger-wrapped-value
(assert_return (invoke $integer "check" (i32.const 3) (i32.const 185376) (i32.const 10) (i64.const 18446744073709551615) (i32.const 20) (i32.const 34)) (i32.const 1))
;; negative-zero
(assert_return (invoke $integer "check" (i32.const 1) (i32.const 185504) (i32.const 0) (i64.const 0) (i32.const 4) (i32.const 17)) (i32.const 1))
(assert_return (invoke $integer "all-bases" (i32.const 0)) (i32.const 1))
(assert_return (invoke $integer "null-end-pointer" (i32.const 0)) (i32.const 1))
(assert_return (invoke $integer "all-bases" (i32.const 1)) (i32.const 1))
(assert_return (invoke $integer "null-end-pointer" (i32.const 1)) (i32.const 1))
(assert_return (invoke $integer "all-bases" (i32.const 2)) (i32.const 1))
(assert_return (invoke $integer "null-end-pointer" (i32.const 2)) (i32.const 1))
(assert_return (invoke $integer "all-bases" (i32.const 3)) (i32.const 1))
(assert_return (invoke $integer "null-end-pointer" (i32.const 3)) (i32.const 1))
(assert_return (invoke $integer "all-bases" (i32.const 4)) (i32.const 1))
(assert_return (invoke $integer "null-end-pointer" (i32.const 4)) (i32.const 1))
(assert_return (invoke $integer "all-bases" (i32.const 5)) (i32.const 1))
(assert_return (invoke $integer "null-end-pointer" (i32.const 5)) (i32.const 1))

;; wide-low-byte-digit: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186000) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-low-byte-space: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186064) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-low-byte-sign: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186128) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-nonbreaking-space: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186192) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-em-space: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186256) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-fullwidth-digit: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186320) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-minimum-character: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186384) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-negative-character: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186448) (i32.const 10) (i64.const 0) (i32.const 0)) (i32.const 1))
;; wide-stops-before-low-byte-digit: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186512) (i32.const 10) (i64.const 42) (i32.const 2)) (i32.const 1))
;; wide-stops-before-low-byte-nul: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186576) (i32.const 10) (i64.const 42) (i32.const 2)) (i32.const 1))
;; wide-low-byte-hex-prefix: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186640) (i32.const 0) (i64.const 0) (i32.const 1)) (i32.const 1))
;; wide-low-byte-hex-digit: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186704) (i32.const 16) (i64.const 0) (i32.const 1)) (i32.const 1))
;; wide-base-36-stops-before-non-ascii: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186768) (i32.const 36) (i64.const 71) (i32.const 2)) (i32.const 1))
;; wide-embedded-nul: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186832) (i32.const 10) (i64.const 42) (i32.const 2)) (i32.const 1))
;; wide-stops-before-negative-character: every wide integer provider
(assert_return (invoke $integer "wide-check" (i32.const 186896) (i32.const 10) (i64.const 42) (i32.const 2)) (i32.const 1))
