;; Public shared formatter provider; compiled va_list checks are system tests.
(module $format
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "vsnprintf" (func $format (param i32 i32 i32 i32) (result i32)))
  (import "libc" "vsprintf" (func $unbounded (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (data (i32.const 180000) "%d-%s\00")
  (data (i32.const 180032) "abcdef\00")
  (data (i32.const 180064) "42-abcdef\00")
  (data (i32.const 180096) "42-abc\00")
  (data (i32.const 180128) "\00")
  (data (i32.const 180160) "%d|%lld|%c|%u|%%\00")
  (data (i32.const 180192) "42|-4294967297|Q|17|%\00")
  (func $reset
    (local $offset i32)
    i32.const 181000 i32.const 42 i32.store
    i32.const 181004 i32.const 180032 i32.store
    i32.const 0 local.set $offset
    loop $fill
      i32.const 181096 local.get $offset i32.add i32.const 0xa5 i32.store8
      local.get $offset i32.const 1 i32.add local.tee $offset
      i32.const 80 i32.lt_u br_if $fill
    end
    call $errno i32.const 17 i32.store)
  (func $check (param $capacity i32) (param $expected i32) (param $bytes i32) (result i32)
    call $reset
    i32.const 181104 local.get $capacity i32.const 180000 i32.const 181000 call $format
    i32.const 9 i32.eq
    i32.const 181104 local.get $expected local.get $bytes call $compare i32.eqz i32.and
    i32.const 181103 i32.load8_u i32.const 0xa5 i32.eq i32.and
    i32.const 181104 local.get $bytes i32.add i32.load8_u i32.const 0xa5 i32.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func (export "complete") (result i32)
    i32.const 64 i32.const 180064 i32.const 10 call $check)
  (func (export "exact-fit") (result i32)
    i32.const 10 i32.const 180064 i32.const 10 call $check)
  (func (export "truncation-reports-full-length") (result i32)
    i32.const 7 i32.const 180096 i32.const 7 call $check)
  (func (export "one-byte-writes-only-nul") (result i32)
    i32.const 1 i32.const 180128 i32.const 1 call $check)
  (func (export "zero-capacity-preserves-output") (result i32)
    call $reset
    i32.const 181104 i32.const 0 i32.const 180000 i32.const 181000 call $format i32.const 9 i32.eq
    i32.const 181104 i32.load i32.const 0xa5a5a5a5 i32.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func (export "null-sizing-destination") (result i32)
    call $reset
    i32.const 0 i32.const 0 i32.const 180000 i32.const 181000 call $format i32.const 9 i32.eq
    call $errno i32.load i32.const 17 i32.eq i32.and)
  (func (export "empty-format") (result i32)
    call $reset
    i32.const 181104 i32.const 64 i32.const 180128 i32.const 181000 call $format i32.eqz
    i32.const 181104 i32.load8_u i32.eqz i32.and
    i32.const 181105 i32.load8_u i32.const 0xa5 i32.eq i32.and)
  (func (export "mixed-promoted-and-wide-arguments") (result i32)
    call $reset
    i32.const 181008 i64.const -4294967297 i64.store
    i32.const 181016 i32.const 81 i32.store
    i32.const 181020 i32.const 17 i32.store
    i32.const 181104 i32.const 64 i32.const 180160 i32.const 181000 call $format i32.const 21 i32.eq
    i32.const 181104 i32.const 180192 i32.const 22 call $compare i32.eqz i32.and)
  (func (export "repeated-argument-list") (result i32)
    call $reset
    i32.const 181104 i32.const 64 i32.const 180000 i32.const 181000 call $format i32.const 9 i32.eq
    i32.const 181136 i32.const 32 i32.const 180000 i32.const 181000 call $format i32.const 9 i32.eq i32.and
    i32.const 181104 i32.const 181136 i32.const 10 call $compare i32.eqz i32.and)
  (func (export "vsprintf-public-provider") (result i32)
    call $reset
    i32.const 181104 i32.const 180000 i32.const 181000 call $unbounded i32.const 9 i32.eq
    i32.const 181104 i32.const 180064 i32.const 10 call $compare i32.eqz i32.and
    i32.const 181114 i32.load8_u i32.const 0xa5 i32.eq i32.and))
(assert_return (invoke $format "complete") (i32.const 1))
(assert_return (invoke $format "exact-fit") (i32.const 1))
(assert_return (invoke $format "truncation-reports-full-length") (i32.const 1))
(assert_return (invoke $format "one-byte-writes-only-nul") (i32.const 1))
(assert_return (invoke $format "zero-capacity-preserves-output") (i32.const 1))
(assert_return (invoke $format "null-sizing-destination") (i32.const 1))
(assert_return (invoke $format "empty-format") (i32.const 1))
(assert_return (invoke $format "mixed-promoted-and-wide-arguments") (i32.const 1))
(assert_return (invoke $format "repeated-argument-list") (i32.const 1))
(assert_return (invoke $format "vsprintf-public-provider") (i32.const 1))
