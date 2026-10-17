;; Installed shared libc only. Each ASCII expectation checks both strto* and
;; wcsto*, including exact IEEE bits, typed end pointers, errno and canaries.
(module $floating
  (import "waste-runtime" "memory" (memory 8))
  (import "libc" "strtof" (func $strtof (param i32 i32) (result f32)))
  (import "libc" "strtod" (func $strtod (param i32 i32) (result f64)))
  (import "libc" "strtold" (func $strtold (param i32 i32 i32)))
  (import "libc" "wcstof" (func $wcstof (param i32 i32) (result f32)))
  (import "libc" "wcstod" (func $wcstod (param i32 i32) (result f64)))
  (import "libc" "wcstold" (func $wcstold (param i32 i32 i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (import "libc" "strlen" (func $length (param i32) (result i32)))
  (import "libc" "wcslen" (func $wide-length (param i32) (result i32)))
  (import "libc" "memcpy" (func $copy (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "\00")
  (data (i32.const 180256) "\20\09\0a\0b\0c\0d\00")
  (data (i32.const 180512) "\2b\00")
  (data (i32.const 180768) "\2d\00")
  (data (i32.const 181024) "\2e\00")
  (data (i32.const 181280) "\20\09\2d\2e\65\33\00")
  (data (i32.const 181536) "\2b\31\32\2e\35\21\00")
  (data (i32.const 181792) "\20\09\0a\0b\0c\0d\2d\31\32\2e\35\21\00")
  (data (i32.const 182048) "\2e\31\32\35\21\00")
  (data (i32.const 182304) "\31\2e\21\00")
  (data (i32.const 182560) "\2d\30\21\00")
  (data (i32.const 182816) "\2d\30\65\39\39\39\39\39\21\00")
  (data (i32.const 183072) "\31\65\2d\32\21\00")
  (data (i32.const 183328) "\30\78\31\2e\38\70\2d\31\21\00")
  (data (i32.const 183584) "\30\58\2e\38\70\2b\31\21\00")
  (data (i32.const 183840) "\30\78\31\21\00")
  (data (i32.const 184096) "\31\65\2b\00")
  (data (i32.const 184352) "\31\65\32\2e\35\00")
  (data (i32.const 184608) "\30\78\00")
  (data (i32.const 184864) "\2d\30\78\2e\70\00")
  (data (i32.const 185120) "\30\78\31\70\2b\00")
  (data (i32.const 185376) "\30\62\31\31\00")
  (data (i32.const 185632) "\69\6e\46\21\00")
  (data (i32.const 185888) "\20\2d\49\4e\46\49\4e\49\54\59\21\00")
  (data (i32.const 186144) "\69\6e\66\69\6e\69\74\65\00")
  (data (i32.const 186400) "\6e\61\6e\00")
  (data (i32.const 186656) "\6e\61\6e\28\29\00")
  (data (i32.const 186912) "\2d\4e\61\4e\28\74\61\67\5f\34\32\29\21\00")
  (data (i32.const 187168) "\6e\61\6e\28\6e\6f\20\73\70\61\63\65\29\00")
  (data (i32.const 187424) "\6e\61\6e\28\00")
  (data (i32.const 187680) "\30\78\31\70\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\21\00")
  (data (i32.const 187936) "\2d\31\65\2d\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\39\21\00")
  (data (i32.const 188192) "\30\78\31\2e\66\66\66\66\66\65\70\31\32\37\21\00")
  (data (i32.const 188448) "\30\78\31\2e\66\66\66\66\66\66\70\31\32\37\21\00")
  (data (i32.const 188704) "\30\78\31\70\2d\31\32\36\21\00")
  (data (i32.const 188960) "\30\78\30\2e\66\66\66\66\66\66\70\2d\31\32\36\21\00")
  (data (i32.const 189216) "\30\78\31\70\2d\31\34\39\21\00")
  (data (i32.const 189472) "\30\78\31\70\2d\31\35\30\21\00")
  (data (i32.const 189728) "\30\78\31\2e\30\30\30\30\30\32\70\2d\31\35\30\21\00")
  (data (i32.const 189984) "\31\2e\30\30\30\30\30\30\30\35\39\36\30\34\36\34\34\37\37\35\33\39\30\36\32\35\21\00")
  (data (i32.const 190240) "\31\2e\30\30\30\30\30\30\31\37\38\38\31\33\39\33\34\33\32\36\31\37\31\38\37\35\21\00")
  (data (i32.const 190496) "\31\2e\30\30\30\30\30\30\30\35\39\36\30\34\36\34\34\37\37\35\33\39\30\36\32\35\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\21\00")
  (data (i32.const 190752) "\30\2e\31\21\00")
  (data (i32.const 191008) "\30\78\31\2e\66\66\66\66\66\66\66\66\66\66\66\66\66\70\31\30\32\33\21\00")
  (data (i32.const 191264) "\30\78\31\2e\66\66\66\66\66\66\66\66\66\66\66\66\66\38\70\31\30\32\33\21\00")
  (data (i32.const 191520) "\30\78\31\70\2d\31\30\32\32\21\00")
  (data (i32.const 191776) "\30\78\30\2e\66\66\66\66\66\66\66\66\66\66\66\66\66\38\70\2d\31\30\32\32\21\00")
  (data (i32.const 192032) "\30\78\31\70\2d\31\30\37\34\21\00")
  (data (i32.const 192288) "\30\78\31\70\2d\31\30\37\35\21\00")
  (data (i32.const 192544) "\30\78\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\31\70\2d\31\30\37\35\21\00")
  (data (i32.const 192800) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\31\31\30\32\32\33\30\32\34\36\32\35\31\35\36\35\34\30\34\32\33\36\33\31\36\36\38\30\39\30\38\32\30\33\31\32\35\21\00")
  (data (i32.const 193056) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\33\33\33\30\36\36\39\30\37\33\38\37\35\34\36\39\36\32\31\32\37\30\38\39\35\30\30\34\32\37\32\34\36\30\39\33\37\35\21\00")
  (data (i32.const 193312) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\31\31\30\32\32\33\30\32\34\36\32\35\31\35\36\35\34\30\34\32\33\36\33\31\36\36\38\30\39\30\38\32\30\33\31\32\35\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\21\00")
  (data (i32.const 193568) "\30\78\31\2e\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\70\31\36\33\38\33\21\00")
  (data (i32.const 193824) "\30\78\31\2e\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\38\70\31\36\33\38\33\21\00")
  (data (i32.const 194080) "\30\78\31\70\2d\31\36\33\38\32\21\00")
  (data (i32.const 194336) "\30\78\30\2e\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\66\38\70\2d\31\36\33\38\32\21\00")
  (data (i32.const 194592) "\30\78\31\70\2d\31\36\34\39\34\21\00")
  (data (i32.const 194848) "\30\78\31\70\2d\31\36\34\39\35\21\00")
  (data (i32.const 195104) "\30\78\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\70\2d\31\36\34\39\35\21\00")
  (data (i32.const 195360) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\39\36\32\39\36\34\39\37\32\31\39\33\36\31\37\39\32\36\35\32\37\39\38\38\39\37\31\32\39\32\34\36\33\36\35\39\32\36\39\30\35\30\38\32\34\31\30\37\36\39\34\30\39\37\36\31\39\39\36\39\33\39\37\37\38\33\32\37\39\34\31\38\39\34\35\33\31\32\35\21\00")
  (data (i32.const 195616) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\32\38\38\38\38\39\34\39\31\36\35\38\30\38\35\33\37\37\39\35\38\33\39\36\36\39\31\33\38\37\37\33\39\30\39\37\37\38\30\37\31\35\32\34\37\32\33\32\33\30\38\32\32\39\32\38\35\39\39\30\38\31\39\33\33\34\39\38\33\38\32\35\36\38\33\35\39\33\37\35\21\00")
  (data (i32.const 195872) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\39\36\32\39\36\34\39\37\32\31\39\33\36\31\37\39\32\36\35\32\37\39\38\38\39\37\31\32\39\32\34\36\33\36\35\39\32\36\39\30\35\30\38\32\34\31\30\37\36\39\34\30\39\37\36\31\39\39\36\39\33\39\37\37\38\33\32\37\39\34\31\38\39\34\35\33\31\32\35\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\21\00")
  (data (i32.const 196128) "\33\2e\34\30\32\38\32\33\34\36\36\33\38\35\32\38\38\35\39\38\31\31\37\30\34\31\38\33\34\38\34\35\31\36\39\32\35\34\34\65\33\38\21\00")
  (data (i32.const 196384) "\31\2e\34\30\31\32\39\38\34\36\34\33\32\34\38\31\37\30\37\30\39\32\33\37\32\39\35\38\33\32\38\39\39\31\36\31\33\31\32\38\30\32\36\31\39\34\31\38\37\36\35\65\2d\34\35\21\00")
  (data (i32.const 196640) "\32\2e\32\32\35\30\37\33\38\35\38\35\30\37\32\30\31\34\65\2d\33\30\38\21\00")
  (data (i32.const 196896) "\34\2e\39\34\30\36\35\36\34\35\38\34\31\32\34\36\35\34\65\2d\33\32\34\21\00")
  (data (i32.const 197152) "\30\78\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\31\70\30\21\00")
  (data (i32.const 197408) "\31\65\34\30\30\30\21\00")
  (data (i32.const 197664) "\31\65\2d\34\30\30\30\21\00")
  (data (i32.const 197920) "\31\2e\35\21\00")
  (data (i32.const 210000) "\31\01\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 210064) "\20\01\00\00\31\00\00\00\00\00\00\00")
  (data (i32.const 210128) "\2d\01\00\00\31\00\00\00\00\00\00\00")
  (data (i32.const 210192) "\31\00\00\00\2e\01\00\00\35\00\00\00\00\00\00\00")
  (data (i32.const 210256) "\31\00\00\00\00\01\00\00\35\00\00\00\00\00\00\00")
  (data (i32.const 210320) "\31\00\00\00\65\01\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 210384) "\03\20\00\00\31\00\00\00\00\00\00\00")
  (data (i32.const 210448) "\11\ff\00\00\32\00\00\00\00\00\00\00")
  (data (i32.const 210512) "\ff\ff\ff\ff\31\00\00\00\00\00\00\00")
  (data (i32.const 210576) "\6e\00\00\00\61\00\00\00\6e\00\00\00\28\00\00\00\41\01\00\00\29\00\00\00\00\00\00\00")
  (data (i32.const 198176) "\31\2e\30\30\30\30\30\30\30\35\39\36\30\34\36\34\34\37\37\35\33\39\30\36\32\35\00")
  (data (i32.const 198432) "\31\2e\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\30\39\36\32\39\36\34\39\37\32\31\39\33\36\31\37\39\32\36\35\32\37\39\38\38\39\37\31\32\39\32\34\36\33\36\35\39\32\36\39\30\35\30\38\32\34\31\30\37\36\39\34\30\39\37\36\31\39\39\36\39\33\39\37\37\38\33\32\37\39\34\31\38\39\34\35\33\31\32\35\00")
  (func $convert (param $kind i32) (param $text i32) (param $end i32)
    i32.const 311008 i64.const 0 i64.store
    i32.const 311016 i64.const 0 i64.store
    local.get $kind i32.const 0 i32.eq if
      i32.const 311008 local.get $text local.get $end call $strtof
      i32.reinterpret_f32 i64.extend_i32_u i64.store return
    end
    local.get $kind i32.const 1 i32.eq if
      i32.const 311008 local.get $text local.get $end call $strtod f64.store return
    end
    local.get $kind i32.const 2 i32.eq if
      i32.const 311008 local.get $text local.get $end call $strtold return
    end
    local.get $kind i32.const 3 i32.eq if
      i32.const 311008 local.get $text local.get $end call $wcstof
      i32.reinterpret_f32 i64.extend_i32_u i64.store return
    end
    local.get $kind i32.const 4 i32.eq if
      i32.const 311008 local.get $text local.get $end call $wcstod f64.store return
    end
    local.get $kind i32.const 5 i32.eq if
      i32.const 311008 local.get $text local.get $end call $wcstold return
    end
    unreachable)
  (func $check-one (param $kind i32) (param $text i32) (param $low i64)
                   (param $high i64) (param $offset i32) (param $error i32) (result i32)
    (local $width i32) (local $bytes i32)
    local.get $kind i32.const 3 i32.ge_u if
      i32.const 4 local.set $width
      local.get $text call $wide-length i32.const 1 i32.add i32.const 4 i32.mul local.set $bytes
    else
      i32.const 1 local.set $width
      local.get $text call $length i32.const 1 i32.add local.set $bytes
    end
    i32.const 420000 local.get $text local.get $bytes call $copy drop
    i32.const 310000 i32.const 0xa5a5a5a5 i32.store
    i32.const 310004 i32.const -1 i32.store
    i32.const 310008 i32.const 0xa5a5a5a5 i32.store
    i32.const 311000 i64.const 0x76543210fedcba98 i64.store
    i32.const 311024 i64.const 0x76543210fedcba98 i64.store
    call $errno i32.const 17 i32.store
    local.get $kind local.get $text i32.const 310004 call $convert
    i32.const 311008 i64.load local.get $low i64.eq
    i32.const 311016 i64.load local.get $high i64.eq i32.and
    i32.const 310004 i32.load
    local.get $text local.get $offset local.get $width i32.mul i32.add i32.eq i32.and
    call $errno i32.load local.get $error i32.eq i32.and
    i32.const 310000 i32.load i32.const 0xa5a5a5a5 i32.eq i32.and
    i32.const 310008 i32.load i32.const 0xa5a5a5a5 i32.eq i32.and
    i32.const 311000 i64.load i64.const 0x76543210fedcba98 i64.eq i32.and
    i32.const 311024 i64.load i64.const 0x76543210fedcba98 i64.eq i32.and
    i32.const 420000 local.get $text local.get $bytes call $compare i32.eqz i32.and)
  (func $widen (param $text i32) (result i32)
    (local $at i32) (local $byte i32) (local $guard i32)
    i32.const 319996 i32.const 0x76543210 i32.store
    loop $next
      local.get $text local.get $at i32.add i32.load8_u local.set $byte
      i32.const 320000 local.get $at i32.const 4 i32.mul i32.add local.get $byte i32.store
      local.get $at i32.const 1 i32.add local.set $at
      local.get $byte br_if $next
    end
    i32.const 320000 local.get $at i32.const 4 i32.mul i32.add local.tee $guard
    i32.const 0x76543210 i32.store
    local.get $guard)
  (func $check (export "check") (param $kind i32) (param $text i32) (param $low i64)
              (param $high i64) (param $offset i32) (param $error i32) (result i32)
    (local $guard i32)
    local.get $kind local.get $text local.get $low local.get $high
    local.get $offset local.get $error call $check-one
    i32.eqz if i32.const 0 return end
    local.get $text call $widen local.set $guard
    local.get $kind i32.const 3 i32.add i32.const 320000 local.get $low local.get $high
    local.get $offset local.get $error call $check-one
    i32.const 319996 i32.load i32.const 0x76543210 i32.eq i32.and
    local.get $guard i32.load i32.const 0x76543210 i32.eq i32.and)
  (func (export "wide-check") (param $kind i32) (param $text i32) (param $low i64)
              (param $high i64) (param $offset i32) (result i32)
    local.get $kind i32.const 3 i32.add local.get $text local.get $low local.get $high
    local.get $offset i32.const 17 call $check-one)
  (func (export "null-end") (param $kind i32) (param $text i32)
             (param $low i64) (param $high i64) (result i32)
    call $errno i32.const 17 i32.store
    local.get $kind local.get $text i32.const 0 call $convert
    i32.const 311008 i64.load local.get $low i64.eq
    i32.const 311016 i64.load local.get $high i64.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and
    i32.eqz if i32.const 0 return end
    local.get $text call $widen drop
    local.get $kind i32.const 3 i32.add i32.const 320000 i32.const 0 call $convert
    i32.const 311008 i64.load local.get $low i64.eq
    i32.const 311016 i64.load local.get $high i64.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func (export "long-tail") (param $kind i32) (param $seed i32)
             (param $low i64) (param $high i64) (result i32)
    (local $length i32) (local $at i32)
    local.get $seed call $length local.set $length
    i32.const 220000 local.get $seed local.get $length call $copy drop
    local.get $length local.set $at
    loop $zero
      i32.const 220000 local.get $at i32.add i32.const 48 i32.store8
      local.get $at i32.const 1 i32.add local.tee $at
      local.get $length i32.const 20000 i32.add i32.lt_u br_if $zero
    end
    i32.const 220000 local.get $at i32.add i32.const 0x002131 i32.store
    local.get $kind i32.const 220000 local.get $low local.get $high
    local.get $at i32.const 1 i32.add i32.const 17 call $check)
  (func (export "long-exponent") (param $kind i32) (param $low i64) (param $high i64) (result i32)
    (local $at i32)
    i32.const 220000 i32.const 0x6531 i32.store16
    i32.const 2 local.set $at
    loop $next
      i32.const 220000 local.get $at i32.add i32.const 57 i32.store8
      local.get $at i32.const 1 i32.add local.tee $at i32.const 20002 i32.lt_u br_if $next
    end
    i32.const 220000 local.get $at i32.add i32.const 33 i32.store16
    local.get $kind i32.const 220000 local.get $low local.get $high
    local.get $at i32.const 34 call $check)
)
;; empty, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 180000) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; empty, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 180000) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; empty, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 180000) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; whitespace, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 180256) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; whitespace, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 180256) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; whitespace, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 180256) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; plus, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 180512) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; plus, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 180512) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; plus, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 180512) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; minus, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 180768) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; minus, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 180768) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; minus, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 180768) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; point, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 181024) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; point, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 181024) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; point, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 181024) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; no-digits, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 181280) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; no-digits, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 181280) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; no-digits, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 181280) (i64.const 0x0) (i64.const 0x0) (i32.const 0) (i32.const 17)) (i32.const 1))
;; decimal, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 181536) (i64.const 0x41480000) (i64.const 0x0) (i32.const 5) (i32.const 17)) (i32.const 1))
;; decimal, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 181536) (i64.const 0x4029000000000000) (i64.const 0x0) (i32.const 5) (i32.const 17)) (i32.const 1))
;; decimal, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 181536) (i64.const 0x0) (i64.const 0x4002900000000000) (i32.const 5) (i32.const 17)) (i32.const 1))
;; whitespace-decimal, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 181792) (i64.const 0xc1480000) (i64.const 0x0) (i32.const 11) (i32.const 17)) (i32.const 1))
;; whitespace-decimal, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 181792) (i64.const 0xc029000000000000) (i64.const 0x0) (i32.const 11) (i32.const 17)) (i32.const 1))
;; whitespace-decimal, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 181792) (i64.const 0x0) (i64.const 0xc002900000000000) (i32.const 11) (i32.const 17)) (i32.const 1))
;; fraction, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 182048) (i64.const 0x3e000000) (i64.const 0x0) (i32.const 4) (i32.const 17)) (i32.const 1))
;; fraction, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 182048) (i64.const 0x3fc0000000000000) (i64.const 0x0) (i32.const 4) (i32.const 17)) (i32.const 1))
;; fraction, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 182048) (i64.const 0x0) (i64.const 0x3ffc000000000000) (i32.const 4) (i32.const 17)) (i32.const 1))
;; trailing-point, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 182304) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; trailing-point, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 182304) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; trailing-point, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 182304) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 2) (i32.const 17)) (i32.const 1))
;; negative-zero, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 182560) (i64.const 0x80000000) (i64.const 0x0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; negative-zero, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 182560) (i64.const 0x8000000000000000) (i64.const 0x0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; negative-zero, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 182560) (i64.const 0x0) (i64.const 0x8000000000000000) (i32.const 2) (i32.const 17)) (i32.const 1))
;; zero-huge-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 182816) (i64.const 0x80000000) (i64.const 0x0) (i32.const 8) (i32.const 17)) (i32.const 1))
;; zero-huge-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 182816) (i64.const 0x8000000000000000) (i64.const 0x0) (i32.const 8) (i32.const 17)) (i32.const 1))
;; zero-huge-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 182816) (i64.const 0x0) (i64.const 0x8000000000000000) (i32.const 8) (i32.const 17)) (i32.const 1))
;; decimal-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 183072) (i64.const 0x3c23d70a) (i64.const 0x0) (i32.const 4) (i32.const 17)) (i32.const 1))
;; decimal-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 183072) (i64.const 0x3f847ae147ae147b) (i64.const 0x0) (i32.const 4) (i32.const 17)) (i32.const 1))
;; decimal-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 183072) (i64.const 0xae147ae147ae147b) (i64.const 0x3ff847ae147ae147) (i32.const 4) (i32.const 17)) (i32.const 1))
;; hex-fraction, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 183328) (i64.const 0x3f400000) (i64.const 0x0) (i32.const 8) (i32.const 17)) (i32.const 1))
;; hex-fraction, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 183328) (i64.const 0x3fe8000000000000) (i64.const 0x0) (i32.const 8) (i32.const 17)) (i32.const 1))
;; hex-fraction, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 183328) (i64.const 0x0) (i64.const 0x3ffe800000000000) (i32.const 8) (i32.const 17)) (i32.const 1))
;; hex-leading-point, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 183584) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 7) (i32.const 17)) (i32.const 1))
;; hex-leading-point, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 183584) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 7) (i32.const 17)) (i32.const 1))
;; hex-leading-point, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 183584) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 7) (i32.const 17)) (i32.const 1))
;; hex-no-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 183840) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; hex-no-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 183840) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; hex-no-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 183840) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; invalid-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 184096) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; invalid-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 184096) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; invalid-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 184096) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 1) (i32.const 17)) (i32.const 1))
;; second-point, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 184352) (i64.const 0x42c80000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; second-point, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 184352) (i64.const 0x4059000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; second-point, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 184352) (i64.const 0x0) (i64.const 0x4005900000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; hex-no-digit, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 184608) (i64.const 0x0) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; hex-no-digit, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 184608) (i64.const 0x0) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; hex-no-digit, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 184608) (i64.const 0x0) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; hex-invalid-point, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 184864) (i64.const 0x80000000) (i64.const 0x0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; hex-invalid-point, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 184864) (i64.const 0x8000000000000000) (i64.const 0x0) (i32.const 2) (i32.const 17)) (i32.const 1))
;; hex-invalid-point, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 184864) (i64.const 0x0) (i64.const 0x8000000000000000) (i32.const 2) (i32.const 17)) (i32.const 1))
;; hex-invalid-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 185120) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; hex-invalid-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 185120) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; hex-invalid-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 185120) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; no-binary-prefix, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 185376) (i64.const 0x0) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; no-binary-prefix, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 185376) (i64.const 0x0) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; no-binary-prefix, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 185376) (i64.const 0x0) (i64.const 0x0) (i32.const 1) (i32.const 17)) (i32.const 1))
;; infinity, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 185632) (i64.const 0x7f800000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; infinity, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 185632) (i64.const 0x7ff0000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; infinity, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 185632) (i64.const 0x0) (i64.const 0x7fff000000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; negative-infinity, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 185888) (i64.const 0xff800000) (i64.const 0x0) (i32.const 10) (i32.const 17)) (i32.const 1))
;; negative-infinity, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 185888) (i64.const 0xfff0000000000000) (i64.const 0x0) (i32.const 10) (i32.const 17)) (i32.const 1))
;; negative-infinity, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 185888) (i64.const 0x0) (i64.const 0xffff000000000000) (i32.const 10) (i32.const 17)) (i32.const 1))
;; partial-infinity, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 186144) (i64.const 0x7f800000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; partial-infinity, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 186144) (i64.const 0x7ff0000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; partial-infinity, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 186144) (i64.const 0x0) (i64.const 0x7fff000000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 186400) (i64.const 0x7fc00000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 186400) (i64.const 0x7ff8000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 186400) (i64.const 0x0) (i64.const 0x7fff800000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan-empty-payload, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 186656) (i64.const 0x7fc00000) (i64.const 0x0) (i32.const 5) (i32.const 17)) (i32.const 1))
;; nan-empty-payload, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 186656) (i64.const 0x7ff8000000000000) (i64.const 0x0) (i32.const 5) (i32.const 17)) (i32.const 1))
;; nan-empty-payload, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 186656) (i64.const 0x0) (i64.const 0x7fff800000000000) (i32.const 5) (i32.const 17)) (i32.const 1))
;; nan-payload, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 186912) (i64.const 0xffc00000) (i64.const 0x0) (i32.const 12) (i32.const 17)) (i32.const 1))
;; nan-payload, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 186912) (i64.const 0xfff8000000000000) (i64.const 0x0) (i32.const 12) (i32.const 17)) (i32.const 1))
;; nan-payload, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 186912) (i64.const 0x0) (i64.const 0xffff800000000000) (i32.const 12) (i32.const 17)) (i32.const 1))
;; nan-invalid-payload, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 187168) (i64.const 0x7fc00000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan-invalid-payload, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 187168) (i64.const 0x7ff8000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan-invalid-payload, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 187168) (i64.const 0x0) (i64.const 0x7fff800000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan-open-payload, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 187424) (i64.const 0x7fc00000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan-open-payload, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 187424) (i64.const 0x7ff8000000000000) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; nan-open-payload, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 187424) (i64.const 0x0) (i64.const 0x7fff800000000000) (i32.const 3) (i32.const 17)) (i32.const 1))
;; huge-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 187680) (i64.const 0x7f800000) (i64.const 0x0) (i32.const 24) (i32.const 34)) (i32.const 1))
;; huge-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 187680) (i64.const 0x7ff0000000000000) (i64.const 0x0) (i32.const 24) (i32.const 34)) (i32.const 1))
;; huge-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 187680) (i64.const 0x0) (i64.const 0x7fff000000000000) (i32.const 24) (i32.const 34)) (i32.const 1))
;; tiny-exponent, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 187936) (i64.const 0x80000000) (i64.const 0x0) (i32.const 24) (i32.const 34)) (i32.const 1))
;; tiny-exponent, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 187936) (i64.const 0x8000000000000000) (i64.const 0x0) (i32.const 24) (i32.const 34)) (i32.const 1))
;; tiny-exponent, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 187936) (i64.const 0x0) (i64.const 0x8000000000000000) (i32.const 24) (i32.const 34)) (i32.const 1))
;; max-finite, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 188192) (i64.const 0x7f7fffff) (i64.const 0x0) (i32.const 14) (i32.const 17)) (i32.const 1))
;; overflow-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 188448) (i64.const 0x7f800000) (i64.const 0x0) (i32.const 14) (i32.const 34)) (i32.const 1))
;; min-normal, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 188704) (i64.const 0x800000) (i64.const 0x0) (i32.const 8) (i32.const 17)) (i32.const 1))
;; below-normal-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 188960) (i64.const 0x800000) (i64.const 0x0) (i32.const 15) (i32.const 34)) (i32.const 1))
;; min-subnormal, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 189216) (i64.const 0x1) (i64.const 0x0) (i32.const 8) (i32.const 17)) (i32.const 1))
;; subnormal-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 189472) (i64.const 0x0) (i64.const 0x0) (i32.const 8) (i32.const 34)) (i32.const 1))
;; above-subnormal-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 189728) (i64.const 0x1) (i64.const 0x0) (i32.const 15) (i32.const 34)) (i32.const 1))
;; even-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 189984) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 26) (i32.const 17)) (i32.const 1))
;; odd-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 190240) (i64.const 0x3f800002) (i64.const 0x0) (i32.const 26) (i32.const 17)) (i32.const 1))
;; above-halfway, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 190496) (i64.const 0x3f800001) (i64.const 0x0) (i32.const 45) (i32.const 17)) (i32.const 1))
;; decimal-tenth, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 190752) (i64.const 0x3dcccccd) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; max-finite, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 191008) (i64.const 0x7fefffffffffffff) (i64.const 0x0) (i32.const 22) (i32.const 17)) (i32.const 1))
;; overflow-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 191264) (i64.const 0x7ff0000000000000) (i64.const 0x0) (i32.const 23) (i32.const 34)) (i32.const 1))
;; min-normal, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 191520) (i64.const 0x10000000000000) (i64.const 0x0) (i32.const 9) (i32.const 17)) (i32.const 1))
;; below-normal-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 191776) (i64.const 0x10000000000000) (i64.const 0x0) (i32.const 24) (i32.const 34)) (i32.const 1))
;; min-subnormal, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 192032) (i64.const 0x1) (i64.const 0x0) (i32.const 9) (i32.const 17)) (i32.const 1))
;; subnormal-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 192288) (i64.const 0x0) (i64.const 0x0) (i32.const 9) (i32.const 34)) (i32.const 1))
;; above-subnormal-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 192544) (i64.const 0x1) (i64.const 0x0) (i32.const 23) (i32.const 34)) (i32.const 1))
;; even-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 192800) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 55) (i32.const 17)) (i32.const 1))
;; odd-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 193056) (i64.const 0x3ff0000000000002) (i64.const 0x0) (i32.const 55) (i32.const 17)) (i32.const 1))
;; above-halfway, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 193312) (i64.const 0x3ff0000000000001) (i64.const 0x0) (i32.const 74) (i32.const 17)) (i32.const 1))
;; decimal-tenth, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 190752) (i64.const 0x3fb999999999999a) (i64.const 0x0) (i32.const 3) (i32.const 17)) (i32.const 1))
;; max-finite, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 193568) (i64.const 0xffffffffffffffff) (i64.const 0x7ffeffffffffffff) (i32.const 38) (i32.const 17)) (i32.const 1))
;; overflow-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 193824) (i64.const 0x0) (i64.const 0x7fff000000000000) (i32.const 39) (i32.const 34)) (i32.const 1))
;; min-normal, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 194080) (i64.const 0x0) (i64.const 0x1000000000000) (i32.const 10) (i32.const 17)) (i32.const 1))
;; below-normal-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 194336) (i64.const 0x0) (i64.const 0x1000000000000) (i32.const 40) (i32.const 34)) (i32.const 1))
;; min-subnormal, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 194592) (i64.const 0x1) (i64.const 0x0) (i32.const 10) (i32.const 17)) (i32.const 1))
;; subnormal-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 194848) (i64.const 0x0) (i64.const 0x0) (i32.const 10) (i32.const 34)) (i32.const 1))
;; above-subnormal-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 195104) (i64.const 0x1) (i64.const 0x0) (i32.const 39) (i32.const 34)) (i32.const 1))
;; even-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 195360) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 115) (i32.const 17)) (i32.const 1))
;; odd-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 195616) (i64.const 0x2) (i64.const 0x3fff000000000000) (i32.const 115) (i32.const 17)) (i32.const 1))
;; above-halfway, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 195872) (i64.const 0x1) (i64.const 0x3fff000000000000) (i32.const 134) (i32.const 17)) (i32.const 1))
;; decimal-tenth, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 190752) (i64.const 0x999999999999999a) (i64.const 0x3ffb999999999999) (i32.const 3) (i32.const 17)) (i32.const 1))
;; float-decimal-max, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 196128) (i64.const 0x7f7fffff) (i64.const 0x0) (i32.const 42) (i32.const 17)) (i32.const 1))
;; float-decimal-subnormal, binary32: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 0) (i32.const 196384) (i64.const 0x1) (i64.const 0x0) (i32.const 55) (i32.const 34)) (i32.const 1))
;; double-decimal-min-normal, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 196640) (i64.const 0x10000000000000) (i64.const 0x0) (i32.const 23) (i32.const 17)) (i32.const 1))
;; double-decimal-subnormal, binary64: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 1) (i32.const 196896) (i64.const 0x1) (i64.const 0x0) (i32.const 23) (i32.const 34)) (i32.const 1))
;; quad-low-bit, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 197152) (i64.const 0x1) (i64.const 0x3fff000000000000) (i32.const 34) (i32.const 17)) (i32.const 1))
;; quad-large-decimal, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 197408) (i64.const 0x18c21ab905450cc3) (i64.const 0x73e6a3750647fcab) (i32.const 6) (i32.const 17)) (i32.const 1))
;; quad-small-decimal, binary128: bytes and wide elements
(assert_return (invoke $floating "check" (i32.const 2) (i32.const 197664) (i64.const 0xb8049732d11a23d) (i64.const 0xc17387ae70c9e70) (i32.const 7) (i32.const 17)) (i32.const 1))
(assert_return (invoke $floating "null-end" (i32.const 0) (i32.const 197920) (i64.const 0x3fc00000) (i64.const 0x0)) (i32.const 1))
(assert_return (invoke $floating "long-exponent" (i32.const 0) (i64.const 0x7f800000) (i64.const 0x0)) (i32.const 1))
(assert_return (invoke $floating "null-end" (i32.const 1) (i32.const 197920) (i64.const 0x3ff8000000000000) (i64.const 0x0)) (i32.const 1))
(assert_return (invoke $floating "long-exponent" (i32.const 1) (i64.const 0x7ff0000000000000) (i64.const 0x0)) (i32.const 1))
(assert_return (invoke $floating "null-end" (i32.const 2) (i32.const 197920) (i64.const 0x0) (i64.const 0x3fff800000000000)) (i32.const 1))
(assert_return (invoke $floating "long-exponent" (i32.const 2) (i64.const 0x0) (i64.const 0x7fff000000000000)) (i32.const 1))
;; wide low-digit
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210000) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-digit
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210000) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-digit
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210000) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-space
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210064) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-space
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210064) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-space
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210064) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-sign
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210128) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-sign
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210128) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-sign
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210128) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide low-point
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210192) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 1)) (i32.const 1))
;; wide low-point
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210192) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 1)) (i32.const 1))
;; wide low-point
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210192) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 1)) (i32.const 1))
;; wide low-nul
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210256) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 1)) (i32.const 1))
;; wide low-nul
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210256) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 1)) (i32.const 1))
;; wide low-nul
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210256) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 1)) (i32.const 1))
;; wide low-exponent
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210320) (i64.const 0x3f800000) (i64.const 0x0) (i32.const 1)) (i32.const 1))
;; wide low-exponent
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210320) (i64.const 0x3ff0000000000000) (i64.const 0x0) (i32.const 1)) (i32.const 1))
;; wide low-exponent
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210320) (i64.const 0x0) (i64.const 0x3fff000000000000) (i32.const 1)) (i32.const 1))
;; wide unicode-space
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210384) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide unicode-space
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210384) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide unicode-space
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210384) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide fullwidth-digit
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210448) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide fullwidth-digit
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210448) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide fullwidth-digit
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210448) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide negative-wchar
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210512) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide negative-wchar
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210512) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide negative-wchar
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210512) (i64.const 0x0) (i64.const 0x0) (i32.const 0)) (i32.const 1))
;; wide nan-non-ascii-payload
(assert_return (invoke $floating "wide-check" (i32.const 0) (i32.const 210576) (i64.const 0x7fc00000) (i64.const 0x0) (i32.const 3)) (i32.const 1))
;; wide nan-non-ascii-payload
(assert_return (invoke $floating "wide-check" (i32.const 1) (i32.const 210576) (i64.const 0x7ff8000000000000) (i64.const 0x0) (i32.const 3)) (i32.const 1))
;; wide nan-non-ascii-payload
(assert_return (invoke $floating "wide-check" (i32.const 2) (i32.const 210576) (i64.const 0x0) (i64.const 0x7fff800000000000) (i32.const 3)) (i32.const 1))
;; halfway plus a nonzero digit after 20,000 zeros
(assert_return (invoke $floating "long-tail" (i32.const 0) (i32.const 198176) (i64.const 0x3f800001) (i64.const 0x0)) (i32.const 1))
;; halfway plus a nonzero digit after 20,000 zeros
(assert_return (invoke $floating "long-tail" (i32.const 2) (i32.const 198432) (i64.const 0x1) (i64.const 0x3fff000000000000)) (i32.const 1))
