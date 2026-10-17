;; Actual regular/ThinLTO graph IDs and owned real-node attributes.
(module $memprof
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (global $count (mut i32) (i32.const 0))
  (data (i32.const 180000) "/tmp/memprof-format.bin\00")
  (data (i32.const 180128) "/tmp/memprof-format-status.txt\00")
  (data (i32.const 200000) "N0x0N0x1N0xfN0x10N0xffN0x100N0xabcdefN0x7fffffffN0x80000000N0xffffffff")
  (data (i32.const 201000) "tooltip=\22")
  (data (i32.const 201128) " ContextIds: 1 4 9\22,fillcolor=\22cyan\22,style=\22filled\22")
  (data (i32.const 201256) " ContextIds:\22,fillcolor=\22mediumorchid1\22,color=\22blue\22,style=\22filled,bold,dashed\22")
  (data (i32.const 203000) "0123456789abcdef")
  (func $capture (param $path i32) (param $output i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $length i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $output local.get $capacity call $read local.set $length
    local.get $fd call $close drop local.get $length)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 184000 i32.const 8192 call $capture
    global.set $count global.get $count i32.const 264 i32.gt_u)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 193000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 193000 i32.load16_u i32.const 0x3833 i32.eq i32.and
    i32.const 193002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func (export "records") (result i64) i32.const 184000 i64.load)
  (func $length (param $index i32) (result i32)
    i32.const 184040 local.get $index i32.const 8 i32.mul i32.add i64.load i32.wrap_i64)
  (func $offset (param $index i32) (result i32)
    (local $i i32) (local $sum i32)
    block $done
      loop $next
        local.get $i local.get $index i32.ge_u br_if $done
        local.get $sum local.get $i call $length i32.add local.set $sum
        local.get $i i32.const 1 i32.add local.set $i br $next
      end
    end
    local.get $sum)
  (func (export "complete") (result i32)
    global.get $count i32.const 264 i32.const 28 call $offset i32.add i32.eq)
  (func $exact (export "exact") (param $index i32) (param $text i32)
    (param $length i32) (result i32)
    local.get $index call $length local.get $length i32.eq
    i32.const 184264 local.get $index call $offset i32.add
    local.get $text local.get $length call $compare i32.eqz i32.and)
  (func $address (param $index i32) (result i64)
    i32.const 184008 local.get $index i32.const 8 i32.mul i32.add i64.load)
  (func (export "distinct") (param $first i32) (param $second i32) (result i32)
    local.get $first call $address local.get $second call $address i64.ne)
  ;; Build expected hex in WAST from the raw address independently of LLVM.
  (func $hex (param $address i32) (result i32)
    (local $value i64) (local $position i32) (local $digit i32) (local $length i32)
    local.get $address call $address local.set $value
    i32.const 202000 i32.const 0x0078304e i32.store
    i32.const 3 local.set $length i32.const 7 local.set $position
    loop $next
      local.get $value local.get $position i32.const 4 i32.mul i64.extend_i32_u
      i64.shr_u i32.wrap_i64 i32.const 15 i32.and local.set $digit
      local.get $digit local.get $length i32.const 3 i32.gt_u i32.or
      local.get $position i32.eqz i32.or
      if
        i32.const 202000 local.get $length i32.add
        i32.const 203000 local.get $digit i32.add i32.load8_u i32.store8
        local.get $length i32.const 1 i32.add local.set $length
      end
      local.get $position i32.const 1 i32.sub local.tee $position
      i32.const 0 i32.ge_s br_if $next
    end
    local.get $length)
  (func (export "node-id") (param $index i32) (param $address i32) (result i32)
    local.get $index i32.const 202000 local.get $address call $hex call $exact)
  (func (export "attributes") (param $index i32) (param $address i32)
    (param $clone i32) (result i32)
    (local $name-length i32) (local $suffix i32) (local $suffix-length i32)
    local.get $address call $hex local.set $name-length
    i32.const 202009 i32.const 202000 local.get $name-length memory.copy
    i32.const 202000 i32.const 201000 i32.const 9 memory.copy
    local.get $clone
    if i32.const 201256 local.set $suffix i32.const 79 local.set $suffix-length
    else i32.const 201128 local.set $suffix i32.const 51 local.set $suffix-length end
    i32.const 202009 local.get $name-length i32.add
    local.get $suffix local.get $suffix-length memory.copy
    local.get $index i32.const 202000 i32.const 9 local.get $name-length i32.add
    local.get $suffix-length i32.add call $exact)
)
(assert_return (invoke "capture") (i32.const 1))
(assert_return (invoke "status") (i32.const 1))
(assert_return (invoke "records") (i64.const 28))
(assert_return (invoke "complete") (i32.const 1))
(assert_return (invoke "exact" (i32.const 0) (i32.const 200000) (i32.const 4)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 1) (i32.const 200004) (i32.const 4)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 2) (i32.const 200008) (i32.const 4)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 3) (i32.const 200012) (i32.const 5)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 4) (i32.const 200017) (i32.const 5)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 5) (i32.const 200022) (i32.const 6)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 6) (i32.const 200028) (i32.const 9)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 7) (i32.const 200037) (i32.const 11)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 8) (i32.const 200048) (i32.const 11)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 9) (i32.const 200059) (i32.const 11)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 10) (i32.const 200000) (i32.const 4)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 11) (i32.const 200004) (i32.const 4)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 12) (i32.const 200008) (i32.const 4)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 13) (i32.const 200012) (i32.const 5)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 14) (i32.const 200017) (i32.const 5)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 15) (i32.const 200022) (i32.const 6)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 16) (i32.const 200028) (i32.const 9)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 17) (i32.const 200037) (i32.const 11)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 18) (i32.const 200048) (i32.const 11)) (i32.const 1))
(assert_return (invoke "exact" (i32.const 19) (i32.const 200059) (i32.const 11)) (i32.const 1))
(assert_return (invoke "node-id" (i32.const 20) (i32.const 0)) (i32.const 1))
(assert_return (invoke "attributes" (i32.const 21) (i32.const 0) (i32.const 0)) (i32.const 1))
(assert_return (invoke "node-id" (i32.const 22) (i32.const 1)) (i32.const 1))
(assert_return (invoke "attributes" (i32.const 23) (i32.const 1) (i32.const 1)) (i32.const 1))
(assert_return (invoke "node-id" (i32.const 24) (i32.const 2)) (i32.const 1))
(assert_return (invoke "attributes" (i32.const 25) (i32.const 2) (i32.const 0)) (i32.const 1))
(assert_return (invoke "node-id" (i32.const 26) (i32.const 3)) (i32.const 1))
(assert_return (invoke "attributes" (i32.const 27) (i32.const 3) (i32.const 1)) (i32.const 1))
(assert_return (invoke "distinct" (i32.const 0) (i32.const 1)) (i32.const 1))
(assert_return (invoke "distinct" (i32.const 2) (i32.const 3)) (i32.const 1))
