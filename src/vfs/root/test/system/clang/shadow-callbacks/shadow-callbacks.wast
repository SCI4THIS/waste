;; Actual callback initializer: owned names, types and repeated declarations.
(module $shadow
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/shadow-callbacks.bin\00")
  (data (i32.const 180128) "/tmp/shadow-callbacks-status.txt\00")
  (data (i32.const 200000) "__asan_set_shadow_00__asan_set_shadow_01__asan_set_shadow_02__asan_set_shadow_03__asan_set_shadow_04__asan_set_shadow_05__asan_set_shadow_06__asan_set_shadow_07__asan_set_shadow_f1__asan_set_shadow_f2__asan_set_shadow_f3__asan_set_shadow_f5__asan_set_shadow_f8")
  (func $capture (param $path i32) (param $output i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $length i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $output local.get $capacity call $read local.set $length
    local.get $fd call $close drop local.get $length)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 184000 i32.const 8192 call $capture)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 193000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 193000 i32.load16_u i32.const 0x3733 i32.eq i32.and
    i32.const 193002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func $case (param $case i32) (result i32)
    i32.const 184000 local.get $case i32.const 948 i32.mul i32.add)
  (func (export "header") (param $case i32)
    (result i64 i64 i64 i64 i64 i64 i64 i64)
    local.get $case call $case i64.load offset=0
    local.get $case call $case i64.load offset=8
    local.get $case call $case i64.load offset=16
    local.get $case call $case i64.load offset=24
    local.get $case call $case i64.load offset=32
    local.get $case call $case i64.load offset=40
    local.get $case call $case i64.load offset=48
    local.get $case call $case i64.load offset=56)
  (func (export "name") (param $case i32) (param $index i32) (result i32)
    local.get $case call $case i32.const 112 i32.add
    local.get $index i32.const 68 i32.mul i32.add
    i32.const 200000 local.get $index i32.const 20 i32.mul i32.add
    i32.const 20 call $compare i32.eqz)
  (func (export "signatures") (param $case i32) (result i32)
    (local $index i32) (local $base i32) (local $matches i32) (local $bits i64)
    local.get $case call $case i64.load local.set $bits
    loop $next
      local.get $case call $case i32.const 64 i32.add
      local.get $index i32.const 68 i32.mul i32.add local.set $base
      local.get $base i64.load i64.const 20 i64.eq
      local.get $base i64.load offset=8 i64.const 1 i64.eq i32.and
      local.get $base i64.load offset=16 i64.const 2 i64.eq i32.and
      local.get $base i64.load offset=24 local.get $bits i64.eq i32.and
      local.get $base i64.load offset=32 local.get $bits i64.eq i32.and
      local.get $base i64.load offset=40 i64.eqz i32.and
      if local.get $matches i32.const 1 i32.add local.set $matches end
      local.get $index i32.const 1 i32.add local.tee $index
      i32.const 13 i32.lt_u br_if $next
    end
    local.get $matches)
)
(assert_return (invoke "capture") (i32.const 2844))
(assert_return (invoke "status") (i32.const 1))
(assert_return (invoke "header" (i32.const 0)) (i64.const 32) (i64.const 2) (i64.const 16) (i64.const 16) (i64.const 13) (i64.const 1) (i64.const 0) (i64.const 13))
(assert_return (invoke "signatures" (i32.const 0)) (i32.const 13))
(assert_return (invoke "name" (i32.const 0) (i32.const 0)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 1)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 2)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 3)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 4)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 5)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 6)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 7)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 8)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 9)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 10)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 11)) (i32.const 1))
(assert_return (invoke "name" (i32.const 0) (i32.const 12)) (i32.const 1))
(assert_return (invoke "header" (i32.const 1)) (i64.const 32) (i64.const 2) (i64.const 38) (i64.const 38) (i64.const 13) (i64.const 1) (i64.const 0) (i64.const 13))
(assert_return (invoke "signatures" (i32.const 1)) (i32.const 13))
(assert_return (invoke "name" (i32.const 1) (i32.const 0)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 1)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 2)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 3)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 4)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 5)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 6)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 7)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 8)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 9)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 10)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 11)) (i32.const 1))
(assert_return (invoke "name" (i32.const 1) (i32.const 12)) (i32.const 1))
(assert_return (invoke "header" (i32.const 2)) (i64.const 64) (i64.const 2) (i64.const 40) (i64.const 40) (i64.const 13) (i64.const 1) (i64.const 0) (i64.const 13))
(assert_return (invoke "signatures" (i32.const 2)) (i32.const 13))
(assert_return (invoke "name" (i32.const 2) (i32.const 0)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 1)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 2)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 3)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 4)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 5)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 6)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 7)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 8)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 9)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 10)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 11)) (i32.const 1))
(assert_return (invoke "name" (i32.const 2) (i32.const 12)) (i32.const 1))
