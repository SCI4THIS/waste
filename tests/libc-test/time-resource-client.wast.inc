(register "waste-libc" $waste_libc)
(module $time_resource_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "getrlimit" (func $getrlimit (param i32 i32) (result i32)))
  (import "waste-libc" "setrlimit" (func $setrlimit (param i32 i32) (result i32)))
  (import "waste-libc" "getrusage" (func $getrusage (param i32 i32) (result i32)))
  (import "waste-libc" "setdtablesize" (func $setdtablesize (param i64) (result i32)))
  (import "waste-libc" "sysconf" (func $sysconf (param i32) (result i32)))
  (import "waste-libc" "pathconf" (func $pathconf (param i32 i32) (result i32)))
  (import "waste-libc" "confstr" (func $confstr (param i32 i32 i32) (result i32)))
  (import "waste-libc" "localtime" (func $localtime (param i32) (result i32)))
  (import "waste-libc" "tzset" (func $tzset))
  (import "waste-libc" "strftime" (func $strftime (param i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "mktemp" (func $mktemp (param i32) (result i32)))
  (import "waste-libc" "mkstemp" (func $mkstemp (param i32) (result i32)))
  (import "waste-libc" "mkdtemp" (func $mkdtemp (param i32) (result i32)))
  (import "waste-libc" "__errno_location" (func $errno_location (result i32)))
  (data (i32.const 180000) "/tmp/waste-XXXXXX\00%Y-%m-%d %H:%M:%S\00/\00")
  (data (i32.const 181000) "\20\00\00\00\00\00\00\00\40\00\00\00\00\00\00\00")

  (func (export "resources") (result i32)
    i32.const 7 i32.const 181000 call $setrlimit i32.eqz
    i32.const 7 i32.const 181016 call $getrlimit i32.eqz i32.and
    i32.const 181016 i64.load i64.const 32 i64.eq i32.and
    i32.const 181024 i64.load i64.const 64 i64.eq i32.and
    i32.const 0 i32.const 181040 call $getrusage i32.eqz i32.and
    i32.const 181040 i64.load i64.eqz i32.and
    i64.const 4096 call $setdtablesize i32.const 4096 i32.eq i32.and
    i32.const 0 call $sysconf i32.const 1024 i32.eq i32.and
    i32.const 180041 i32.const 0 call $pathconf i32.const 255 i32.eq i32.and
    i32.const 0 i32.const 181200 i32.const 32 call $confstr i32.const 14 i32.eq i32.and)

  (func (export "clock-and-temporaries") (result i32)
    (local $tm i32)
    i32.const 181400 i64.const 0 i64.store
    i32.const 181400 call $localtime local.tee $tm i32.eqz i32.eqz
    local.get $tm i32.load offset=12 i32.const 1 i32.eq i32.and
    local.get $tm i32.load offset=16 i32.eqz i32.and
    local.get $tm i32.load offset=20 i32.const 70 i32.eq i32.and
    call $tzset
    i32.const 181500 i32.const 32 i32.const 180018 local.get $tm call $strftime i32.const 19 i32.eq i32.and
    i32.const 180000 call $mktemp i32.const 180000 i32.eq i32.and
    i32.const 180016 i32.load8_u i32.const 88 i32.ne i32.and
    i32.const 180000 call $mkstemp i32.const -1 i32.eq i32.and
    call $errno_location i32.load i32.const 38 i32.eq i32.and
    i32.const 180000 call $mkdtemp i32.eqz i32.and
    call $errno_location i32.load i32.const 38 i32.eq i32.and))
(assert_return (invoke $time_resource_tests "resources") (i32.const 1))
(assert_return (invoke $time_resource_tests "clock-and-temporaries") (i32.const 1))
