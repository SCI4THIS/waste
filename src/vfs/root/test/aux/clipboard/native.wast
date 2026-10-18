;; Run from guest Bash after setup.sh. All functional assertions live here.
(module $clipboard
 (import "waste-runtime" "memory" (memory 4))
 (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
 (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
 (import "libc" "close" (func $close (param i32) (result i32)))
 (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))

 (func $matches (param $path i32) (param $expected i32) (param $length i32) (result i32)
  (local $fd i32) (local $ok i32)
  local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
  i32.const 0 i32.lt_s if i32.const 0 return end
  local.get $fd i32.const 200000 local.get $length i32.const 1 i32.add call $read
  local.get $length i32.eq
  i32.const 200000 local.get $expected local.get $length call $memcmp i32.eqz
  i32.and local.set $ok
  local.get $fd call $close drop local.get $ok)
 (data (i32.const 180000) "/tmp/clipboard-native-status\00")
 (data (i32.const 180128) "\31\0a")
 (func (export "native-unavailable") (result i32) i32.const 180000 i32.const 180128 i32.const 2 call $matches)
)
(assert_return (invoke $clipboard "native-unavailable") (i32.const 1))
