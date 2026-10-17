;; Actual HLSL driver name validator: full strings, all suffix bytes and native parity.
(module $name
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (global $bytes (mut i32) (i32.const -1))
  (data (i32.const 180000) "/tmp/spirv-name.bin\00")
  (data (i32.const 180064) "/tmp/spirv-name-reference.bin\00")
  (data (i32.const 180128) "/tmp/spirv-name-status.txt\00")
  (data (i32.const 181000) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\01\01\01\01\01\01\01\01\01\00\00\00\00\00\00\00\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\00\00\00\00\01\00\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00")
  (func $capture (param $path i32) (param $dest i32) (result i32)
    (local $fd i32) (local $got i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $dest i32.const 287 call $read local.set $got
    local.get $fd call $close drop local.get $got)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 184000 call $capture global.set $bytes
    global.get $bytes)
  (func (export "case") (param $index i32) (result i32)
    global.get $bytes i32.const 286 i32.ne if i32.const -1 return end
    local.get $index i32.const 30 i32.ge_u if i32.const -1 return end
    i32.const 184000 local.get $index i32.add i32.load8_u)
  (func (export "all-suffix-bytes") (result i32)
    global.get $bytes i32.const 286 i32.eq
    i32.const 184030 i32.const 181000 i32.const 256 call $compare i32.eqz i32.and)
  (func (export "native-reference") (result i32)
    global.get $bytes i32.const 286 i32.ne if i32.const 0 return end
    i32.const 180064 i32.const 196000 call $capture i32.const 286 i32.ne
    if i32.const 0 return end
    i32.const 184000 i32.const 196000 i32.const 286 call $compare i32.eqz)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 220000 call $capture i32.const 3 i32.eq
    i32.const 220000 i32.load16_u i32.const 0x3833 i32.eq i32.and
    i32.const 220002 i32.load8_u i32.const 10 i32.eq i32.and))
(assert_return (invoke "capture") (i32.const 286))
(assert_return (invoke "status") (i32.const 1))
(assert_return (invoke "case" (i32.const 0)) (i32.const 1))
(assert_return (invoke "case" (i32.const 1)) (i32.const 1))
(assert_return (invoke "case" (i32.const 2)) (i32.const 1))
(assert_return (invoke "case" (i32.const 3)) (i32.const 1))
(assert_return (invoke "case" (i32.const 4)) (i32.const 1))
(assert_return (invoke "case" (i32.const 5)) (i32.const 1))
(assert_return (invoke "case" (i32.const 6)) (i32.const 1))
(assert_return (invoke "case" (i32.const 7)) (i32.const 1))
(assert_return (invoke "case" (i32.const 8)) (i32.const 1))
(assert_return (invoke "case" (i32.const 9)) (i32.const 1))
(assert_return (invoke "case" (i32.const 10)) (i32.const 0))
(assert_return (invoke "case" (i32.const 11)) (i32.const 0))
(assert_return (invoke "case" (i32.const 12)) (i32.const 0))
(assert_return (invoke "case" (i32.const 13)) (i32.const 0))
(assert_return (invoke "case" (i32.const 14)) (i32.const 0))
(assert_return (invoke "case" (i32.const 15)) (i32.const 0))
(assert_return (invoke "case" (i32.const 16)) (i32.const 0))
(assert_return (invoke "case" (i32.const 17)) (i32.const 0))
(assert_return (invoke "case" (i32.const 18)) (i32.const 0))
(assert_return (invoke "case" (i32.const 19)) (i32.const 0))
(assert_return (invoke "case" (i32.const 20)) (i32.const 0))
(assert_return (invoke "case" (i32.const 21)) (i32.const 0))
(assert_return (invoke "case" (i32.const 22)) (i32.const 0))
(assert_return (invoke "case" (i32.const 23)) (i32.const 0))
(assert_return (invoke "case" (i32.const 24)) (i32.const 0))
(assert_return (invoke "case" (i32.const 25)) (i32.const 0))
(assert_return (invoke "case" (i32.const 26)) (i32.const 0))
(assert_return (invoke "case" (i32.const 27)) (i32.const 0))
(assert_return (invoke "case" (i32.const 28)) (i32.const 0))
(assert_return (invoke "case" (i32.const 29)) (i32.const 0))
(assert_return (invoke "all-suffix-bytes") (i32.const 1))
(assert_return (invoke "native-reference") (i32.const 1))
