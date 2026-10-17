;; Exact shared decimal/hex floating conversion with snprintf byte semantics.
(module $float
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "vsnprintf" (func $format (param i32 i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (import "libc" "vasprintf" (func $allocate (param i32 i32 i32) (result i32)))
  (import "libc" "asprintf" (func $allocate_direct (param i32 i32 i32) (result i32)))
  (import "libc" "vfprintf" (func $print_file (param i32 i32 i32) (result i32)))
  (import "libc" "fopen" (func $open (param i32 i32) (result i32)))
  (import "libc" "fclose" (func $close (param i32) (result i32)))
  (import "libc" "ftell" (func $tell (param i32) (result i32)))
  (import "libc" "fread" (func $read (param i32 i32 i32 i32) (result i32)))
  (import "libc" "free" (func $free (param i32)))
  (func $reset
    (local $at i32)
    loop $fill
      i32.const 185000 local.get $at i32.add i32.const 0xa5 i32.store8
      local.get $at i32.const 1 i32.add local.tee $at i32.const 160 i32.lt_u br_if $fill
    end
    call $errno i32.const 17 i32.store)
  (func $check (param $fmt i32) (param $expected i32) (param $length i32) (param $capacity i32) (result i32)
    (local $ret i32) (local $kept i32)
    local.get $capacity i32.eqz if i32.const 0 local.set $kept else
      local.get $capacity i32.const 1 i32.sub local.get $length i32.lt_u
      if local.get $capacity i32.const 1 i32.sub local.set $kept
      else local.get $length local.set $kept end
    end
    i32.const 185008 local.get $capacity local.get $fmt i32.const 184000 call $format local.set $ret
    local.get $ret local.get $length i32.eq
    i32.const 185008 local.get $expected local.get $kept call $compare i32.eqz i32.and
    local.get $capacity i32.eqz if (result i32) i32.const 1 else
      i32.const 185008 local.get $kept i32.add i32.load8_u i32.eqz end i32.and
    i32.const 185007 i32.load8_u i32.const 0xa5 i32.eq i32.and
    i32.const 185008 local.get $kept i32.add local.get $capacity i32.eqz if (result i32) i32.const 0 else i32.const 1 end
    i32.add i32.load8_u i32.const 0xa5 i32.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (data (i32.const 180000) "%.4g\00")
  (data (i32.const 180032) "33.33\00")
  (func (export "general-third") (result i32)
    call $reset i32.const 184000 i64.const 0x4040aaaaaaaaaaab i64.store
    i32.const 180000 i32.const 180032 i32.const 5 i32.const 128 call $check)
  (data (i32.const 180128) "%.4g\00")
  (data (i32.const 180160) "1e-05\00")
  (func (export "general-small") (result i32)
    call $reset i32.const 184000 i64.const 0x3ee4f8b588e368f1 i64.store
    i32.const 180128 i32.const 180160 i32.const 5 i32.const 128 call $check)
  (data (i32.const 180256) "%.4g\00")
  (data (i32.const 180288) "1e+04\00")
  (func (export "general-round-carry") (result i32)
    call $reset i32.const 184000 i64.const 0x40c387c000000000 i64.store
    i32.const 180256 i32.const 180288 i32.const 5 i32.const 128 call $check)
  (data (i32.const 180384) "%.4g\00")
  (data (i32.const 180416) "0.0001\00")
  (func (export "general-threshold-fixed") (result i32)
    call $reset i32.const 184000 i64.const 0x3f1a36e2eb1c432d i64.store
    i32.const 180384 i32.const 180416 i32.const 6 i32.const 128 call $check)
  (data (i32.const 180512) "%.0g\00")
  (data (i32.const 180544) "1\00")
  (func (export "general-zero-precision") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 180512 i32.const 180544 i32.const 1 i32.const 128 call $check)
  (data (i32.const 180640) "%#.4g\00")
  (data (i32.const 180672) "1.250\00")
  (func (export "general-alternate") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 180640 i32.const 180672 i32.const 5 i32.const 128 call $check)
  (data (i32.const 180768) "%.0f\00")
  (data (i32.const 180800) "2\00")
  (func (export "fixed-tie-even-low") (result i32)
    call $reset i32.const 184000 i64.const 0x4004000000000000 i64.store
    i32.const 180768 i32.const 180800 i32.const 1 i32.const 128 call $check)
  (data (i32.const 180896) "%.0f\00")
  (data (i32.const 180928) "4\00")
  (func (export "fixed-tie-even-high") (result i32)
    call $reset i32.const 184000 i64.const 0x400c000000000000 i64.store
    i32.const 180896 i32.const 180928 i32.const 1 i32.const 128 call $check)
  (data (i32.const 181024) "%.0f\00")
  (data (i32.const 181056) "1\00")
  (func (export "fixed-before-half") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff7ffffffffffff i64.store
    i32.const 181024 i32.const 181056 i32.const 1 i32.const 128 call $check)
  (data (i32.const 181152) "%.2f\00")
  (data (i32.const 181184) "1.12\00")
  (func (export "fixed-decimal") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff2000000000000 i64.store
    i32.const 181152 i32.const 181184 i32.const 4 i32.const 128 call $check)
  (data (i32.const 181280) "%.2f\00")
  (data (i32.const 181312) "10.00\00")
  (func (export "fixed-carry") (result i32)
    call $reset i32.const 184000 i64.const 0x4023ff7ced916873 i64.store
    i32.const 181280 i32.const 181312 i32.const 5 i32.const 128 call $check)
  (data (i32.const 181408) "%+010.2f\00")
  (data (i32.const 181440) "+000001.25\00")
  (func (export "fixed-zero-pad") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 181408 i32.const 181440 i32.const 10 i32.const 128 call $check)
  (data (i32.const 181536) "%-10.2f\00")
  (data (i32.const 181568) "1.25      \00")
  (func (export "fixed-left") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 181536 i32.const 181568 i32.const 10 i32.const 128 call $check)
  (data (i32.const 181664) "% .2f\00")
  (data (i32.const 181696) " 1.25\00")
  (func (export "fixed-space-sign") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 181664 i32.const 181696 i32.const 5 i32.const 128 call $check)
  (data (i32.const 181792) "%.2f\00")
  (data (i32.const 181824) "-0.00\00")
  (func (export "fixed-negative-zero") (result i32)
    call $reset i32.const 184000 i64.const 0x8000000000000000 i64.store
    i32.const 181792 i32.const 181824 i32.const 5 i32.const 128 call $check)
  (data (i32.const 181920) "%.3e\00")
  (data (i32.const 181952) "1.250e+00\00")
  (func (export "scientific-lower") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 181920 i32.const 181952 i32.const 9 i32.const 128 call $check)
  (data (i32.const 182048) "%.2E\00")
  (data (i32.const 182080) "1.25E+04\00")
  (func (export "scientific-upper") (result i32)
    call $reset i32.const 184000 i64.const 0x40c86a0000000000 i64.store
    i32.const 182048 i32.const 182080 i32.const 8 i32.const 128 call $check)
  (data (i32.const 182176) "%a\00")
  (data (i32.const 182208) "0x1.2p+0\00")
  (func (export "hex-default") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff2000000000000 i64.store
    i32.const 182176 i32.const 182208 i32.const 8 i32.const 128 call $check)
  (data (i32.const 182304) "%.3A\00")
  (data (i32.const 182336) "0X1.200P+0\00")
  (func (export "hex-upper") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff2000000000000 i64.store
    i32.const 182304 i32.const 182336 i32.const 10 i32.const 128 call $check)
  (data (i32.const 182432) "%.0a\00")
  (data (i32.const 182464) "0x2p+0\00")
  (func (export "hex-round-tie") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff8000000000000 i64.store
    i32.const 182432 i32.const 182464 i32.const 6 i32.const 128 call $check)
  (data (i32.const 182560) "%a\00")
  (data (i32.const 182592) "0x1p-1074\00")
  (func (export "hex-subnormal") (result i32)
    call $reset i32.const 184000 i64.const 0x1 i64.store
    i32.const 182560 i32.const 182592 i32.const 9 i32.const 128 call $check)
  (data (i32.const 182688) "%.4g\00")
  (data (i32.const 182720) "4.941e-324\00")
  (func (export "general-subnormal") (result i32)
    call $reset i32.const 184000 i64.const 0x1 i64.store
    i32.const 182688 i32.const 182720 i32.const 10 i32.const 128 call $check)
  (data (i32.const 182816) "%.4g\00")
  (data (i32.const 182848) "1.798e+308\00")
  (func (export "general-largest") (result i32)
    call $reset i32.const 184000 i64.const 0x7fefffffffffffff i64.store
    i32.const 182816 i32.const 182848 i32.const 10 i32.const 128 call $check)
  (data (i32.const 182944) "%+f\00")
  (data (i32.const 182976) "+inf\00")
  (func (export "infinity") (result i32)
    call $reset i32.const 184000 i64.const 0x7ff0000000000000 i64.store
    i32.const 182944 i32.const 182976 i32.const 4 i32.const 128 call $check)
  (data (i32.const 183072) "%F\00")
  (data (i32.const 183104) "-INF\00")
  (func (export "negative-infinity") (result i32)
    call $reset i32.const 184000 i64.const 0xfff0000000000000 i64.store
    i32.const 183072 i32.const 183104 i32.const 4 i32.const 128 call $check)
  (data (i32.const 183200) "%g\00")
  (data (i32.const 183232) "nan\00")
  (func (export "quiet-nan") (result i32)
    call $reset i32.const 184000 i64.const 0x7ff8000000000000 i64.store
    i32.const 183200 i32.const 183232 i32.const 3 i32.const 128 call $check)
  (data (i32.const 183328) "%.2lf\00")
  (data (i32.const 183360) "1.25\00")
  (func (export "long-spelling-double") (result i32)
    call $reset i32.const 184000 i64.const 0x3ff4000000000000 i64.store
    i32.const 183328 i32.const 183360 i32.const 4 i32.const 128 call $check)
  (data (i32.const 183600) "%.4g\00")
  (data (i32.const 183632) "33.33\00")
  (func (export "bounded") (param $capacity i32) (result i32)
    call $reset i32.const 184000 f64.const 0x1.0aaaaaaaaaaabp5 f64.store
    i32.const 183600 i32.const 183632 i32.const 5 local.get $capacity call $check)
  (func (export "null-sizing") (result i32)
    call $reset i32.const 184000 f64.const 0x1.0aaaaaaaaaaabp5 f64.store
    i32.const 0 i32.const 0 i32.const 183600 i32.const 184000 call $format i32.const 5 i32.eq
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (data (i32.const 183664) "%*.*g|%d|%.3Lf|%s\00")
  (data (i32.const 183700) "     33.33|17|1.125|tail\00")
  (data (i32.const 183744) "tail\00")
  (func (export "aligned-mixed-arguments") (result i32)
    call $reset
    i32.const 184000 i32.const 10 i32.store
    i32.const 184004 i32.const 4 i32.store
    i32.const 184008 f64.const 0x1.0aaaaaaaaaaabp5 f64.store
    i32.const 184016 i32.const 17 i32.store
    i32.const 184032 i64.const 0 i64.store
    i32.const 184040 i64.const 0x3fff200000000000 i64.store
    i32.const 184048 i32.const 183744 i32.store
    i32.const 183664 i32.const 183700 i32.const 24 i32.const 128 call $check)
  (data (i32.const 183776) "%*.*f\00")
  (data (i32.const 183808) "1.25    \00")
  (func (export "negative-star-width") (result i32)
    call $reset
    i32.const 184000 i32.const -8 i32.store
    i32.const 184004 i32.const 2 i32.store
    i32.const 184008 f64.const 1.25 f64.store
    i32.const 183776 i32.const 183808 i32.const 8 i32.const 128 call $check)
  (data (i32.const 183840) "%.2147483645f\00")
  (data (i32.const 183872) "%.2147483646f\00")
  (data (i32.const 183904) "%.2147483647a\00")
  (data (i32.const 183936) "%.2147483648g\00")
  (data (i32.const 183968) "%hf\00")
  (func (export "maximum-count-without-output-loop") (result i32)
    call $reset i32.const 184000 f64.const 0 f64.store
    i32.const 0 i32.const 0 i32.const 183840 i32.const 184000 call $format i32.const 2147483647 i32.eq
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func (export "format-error") (param $fmt i32) (param $error i32) (result i32)
    call $reset i32.const 184000 f64.const 0 f64.store
    i32.const 185008 i32.const 2 local.get $fmt i32.const 184000 call $format i32.const -1 i32.eq
    call $errno i32.load local.get $error i32.eq i32.and
    i32.const 185008 i32.load8_u i32.eqz i32.and
    i32.const 185009 i32.load8_u i32.const 0xa5 i32.eq i32.and)
  (data (i32.const 186000) "%f\00")
  (data (i32.const 186032) "1.250000\00")
  (func (export "default-fixed") (result i32)
    call $reset i32.const 184000 f64.const 1.25 f64.store
    i32.const 186000 i32.const 186032 i32.const 8 i32.const 128 call $check)
  (data (i32.const 186128) "%e\00")
  (data (i32.const 186160) "1.250000e+00\00")
  (func (export "default-scientific") (result i32)
    call $reset i32.const 184000 f64.const 1.25 f64.store
    i32.const 186128 i32.const 186160 i32.const 12 i32.const 128 call $check)
  (data (i32.const 186256) "%g\00")
  (data (i32.const 186288) "1.23457\00")
  (func (export "default-general") (result i32)
    call $reset i32.const 184000 f64.const 1.23456789 f64.store
    i32.const 186256 i32.const 186288 i32.const 7 i32.const 128 call $check)
  (data (i32.const 186400) "%.*f\00")
  (data (i32.const 186432) "1.250000\00")
  (func (export "negative-star-precision") (result i32)
    call $reset i32.const 184000 i32.const -1 i32.store
    i32.const 184008 f64.const 1.25 f64.store
    i32.const 186400 i32.const 186432 i32.const 8 i32.const 128 call $check)
  (func (export "minimum-star-width-error") (result i32)
    call $reset i32.const 184000 i32.const -2147483648 i32.store
    i32.const 184004 i32.const 2 i32.store
    i32.const 184008 f64.const 1.25 f64.store
    i32.const 185008 i32.const 2 i32.const 183776 i32.const 184000 call $format i32.const -1 i32.eq
    call $errno i32.load i32.const 75 i32.eq i32.and
    i32.const 185008 i32.load8_u i32.eqz i32.and
    i32.const 185009 i32.load8_u i32.const 0xa5 i32.eq i32.and)
  (data (i32.const 186640) "/tmp/narrow-float-file.txt\00")
  (data (i32.const 186704) "w\00")
  (data (i32.const 186712) "r\00")
  (data (i32.const 186736) "%.2f\00")
  (data (i32.const 186768) "1.25\00")
  (func (export "allocating-format-error") (param $direct i32) (result i32)
    call $reset i32.const 184000 f64.const 0 f64.store
    i32.const 186600 i32.const 0x12345678 i32.store
    i32.const 186600 i32.const 183872 i32.const 184000
    local.get $direct if (param i32 i32 i32) (result i32)
      call $allocate_direct
    else
      call $allocate
    end
    i32.const -1 i32.eq
    call $errno i32.load i32.const 75 i32.eq i32.and
    i32.const 186600 i32.load i32.const 0x12345678 i32.eq i32.and)
  (func (export "file-format-error") (result i32)
    (local $file i32) (local $ok i32)
    call $reset i32.const 184000 f64.const 0 f64.store
    i32.const 186640 i32.const 186704 call $open local.tee $file i32.eqz
    if i32.const 0 return end
    local.get $file i32.const 183872 i32.const 184000 call $print_file i32.const -1 i32.eq
    call $errno i32.load i32.const 75 i32.eq i32.and local.set $ok
    local.get $ok local.get $file call $tell i32.eqz i32.and
    local.get $file call $close i32.eqz i32.and)
  (func (export "allocating-format-positive") (result i32)
    (local $pointer i32) (local $ok i32)
    call $reset i32.const 184000 f64.const 1.25 f64.store
    i32.const 186600 i32.const 0 i32.store
    i32.const 186600 i32.const 186736 i32.const 184000 call $allocate i32.const 4 i32.eq local.set $ok
    i32.const 186600 i32.load local.tee $pointer i32.eqz if i32.const 0 return end
    local.get $ok local.get $pointer i32.const 186768 i32.const 5 call $compare i32.eqz i32.and local.set $ok
    local.get $pointer call $free local.get $ok)
  (func (export "file-format-positive") (result i32)
    (local $file i32) (local $ok i32)
    call $reset i32.const 184000 f64.const 1.25 f64.store
    i32.const 186640 i32.const 186704 call $open local.tee $file i32.eqz
    if i32.const 0 return end
    local.get $file i32.const 186736 i32.const 184000 call $print_file i32.const 4 i32.eq local.set $ok
    local.get $ok local.get $file call $tell i32.const 4 i32.eq i32.and
    local.get $file call $close i32.eqz i32.and local.set $ok
    i32.const 186640 i32.const 186712 call $open local.tee $file i32.eqz
    if i32.const 0 return end
    local.get $ok
    i32.const 185008 i32.const 1 i32.const 8 local.get $file call $read i32.const 4 i32.eq i32.and
    i32.const 185008 i32.const 186768 i32.const 4 call $compare i32.eqz i32.and
    local.get $file call $close i32.eqz i32.and)
)
(assert_return (invoke "general-third") (i32.const 1))
(assert_return (invoke "general-small") (i32.const 1))
(assert_return (invoke "general-round-carry") (i32.const 1))
(assert_return (invoke "general-threshold-fixed") (i32.const 1))
(assert_return (invoke "general-zero-precision") (i32.const 1))
(assert_return (invoke "general-alternate") (i32.const 1))
(assert_return (invoke "fixed-tie-even-low") (i32.const 1))
(assert_return (invoke "fixed-tie-even-high") (i32.const 1))
(assert_return (invoke "fixed-before-half") (i32.const 1))
(assert_return (invoke "fixed-decimal") (i32.const 1))
(assert_return (invoke "fixed-carry") (i32.const 1))
(assert_return (invoke "fixed-zero-pad") (i32.const 1))
(assert_return (invoke "fixed-left") (i32.const 1))
(assert_return (invoke "fixed-space-sign") (i32.const 1))
(assert_return (invoke "fixed-negative-zero") (i32.const 1))
(assert_return (invoke "scientific-lower") (i32.const 1))
(assert_return (invoke "scientific-upper") (i32.const 1))
(assert_return (invoke "hex-default") (i32.const 1))
(assert_return (invoke "hex-upper") (i32.const 1))
(assert_return (invoke "hex-round-tie") (i32.const 1))
(assert_return (invoke "hex-subnormal") (i32.const 1))
(assert_return (invoke "general-subnormal") (i32.const 1))
(assert_return (invoke "general-largest") (i32.const 1))
(assert_return (invoke "infinity") (i32.const 1))
(assert_return (invoke "negative-infinity") (i32.const 1))
(assert_return (invoke "quiet-nan") (i32.const 1))
(assert_return (invoke "long-spelling-double") (i32.const 1))
(assert_return (invoke "bounded" (i32.const 0)) (i32.const 1))
(assert_return (invoke "bounded" (i32.const 1)) (i32.const 1))
(assert_return (invoke "bounded" (i32.const 2)) (i32.const 1))
(assert_return (invoke "bounded" (i32.const 5)) (i32.const 1))
(assert_return (invoke "bounded" (i32.const 6)) (i32.const 1))
(assert_return (invoke "bounded" (i32.const 128)) (i32.const 1))
(assert_return (invoke "null-sizing") (i32.const 1))
(assert_return (invoke "aligned-mixed-arguments") (i32.const 1))
(assert_return (invoke "negative-star-width") (i32.const 1))
(assert_return (invoke "maximum-count-without-output-loop") (i32.const 1))
(assert_return (invoke "format-error" (i32.const 183872) (i32.const 75)) (i32.const 1))
(assert_return (invoke "format-error" (i32.const 183904) (i32.const 75)) (i32.const 1))
(assert_return (invoke "format-error" (i32.const 183936) (i32.const 75)) (i32.const 1))
(assert_return (invoke "format-error" (i32.const 183968) (i32.const 22)) (i32.const 1))
(assert_return (invoke "default-fixed") (i32.const 1))
(assert_return (invoke "default-scientific") (i32.const 1))
(assert_return (invoke "default-general") (i32.const 1))
(assert_return (invoke "negative-star-precision") (i32.const 1))
(assert_return (invoke "minimum-star-width-error") (i32.const 1))
(assert_return (invoke "allocating-format-error" (i32.const 0)) (i32.const 1))
(assert_return (invoke "allocating-format-error" (i32.const 1)) (i32.const 1))
(assert_return (invoke "file-format-error") (i32.const 1))
(assert_return (invoke "allocating-format-positive") (i32.const 1))
(assert_return (invoke "file-format-positive") (i32.const 1))
