;; Run after setup.sh: compiler/linker output and guest execution evidence.
(module $compiled
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (data (i32.const 180064) "/tmp/clang-compile-status.txt\00")
  (data (i32.const 180128) "/tmp/clang-probe.wasm\00")
  (data (i32.const 180192) "/tmp/clang-run-status.txt\00")
  (data (i32.const 180352) "0\0a")
  (data (i32.const 180368) "\00asm\01\00\00\00")
  (data (i32.const 180384) "42\0a")
  (func $matches (param $path i32) (param $bytes i32) (param $length i32)
                (param $exact i32) (result i32)
    (local $fd i32) (local $got i32) (local $ok i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181000
    local.get $length local.get $exact i32.add call $read local.set $got
    local.get $got local.get $length i32.eq
    i32.const 181000 local.get $bytes local.get $length call $memcmp i32.eqz
    i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok)
  (func (export "compile-and-link") (result i32)
    i32.const 180064 i32.const 180352 i32.const 2 i32.const 1 call $matches)
  (func (export "wasm-output") (result i32)
    i32.const 180128 i32.const 180368 i32.const 8 i32.const 0 call $matches)
  (func (export "compiled-program-exit") (result i32)
    i32.const 180192 i32.const 180384 i32.const 3 i32.const 1 call $matches))
(assert_return (invoke $compiled "compile-and-link") (i32.const 1))
(assert_return (invoke $compiled "wasm-output") (i32.const 1))
(assert_return (invoke $compiled "compiled-program-exit") (i32.const 1))
