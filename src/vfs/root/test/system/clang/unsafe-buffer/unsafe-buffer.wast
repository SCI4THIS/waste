;; Actual guest compiler unsafe-buffer diagnostics. Requires installed Clang.
(module $unsafe_buffer
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (global $length (mut i32) (i32.const 0))
  (data (i32.const 180000) "/tmp/unsafe-buffer.txt\00")
  (data (i32.const 180128) "/tmp/unsafe-buffer-status.txt\00")
  (data (i32.const 180192) "/tmp/unsafe-buffer-parent.txt\00")
  (data (i32.const 210000) "0\0a")
  (data (i32.const 210016) "parent-alive\0a")
  (data (i32.const 202000) ":{3:3-3:8}:\22std::span<int> \22")
  (data (i32.const 202256) ":{3:12-3:12}:\22{\22")
  (data (i32.const 202512) ":{3:23-3:23}:\22, 10}\22")
  (data (i32.const 202768) ":{4:15-4:20}:\22p.data()\22")
  (data (i32.const 203024) ":{5:16-5:25}:\22&p.data()[index]\22")
  (data (i32.const 203280) ":{6:18-6:21}:\22(p = p.subspan(1)).data()\22")
  (data (i32.const 203536) ":{8:1-8:1}:\22[[clang::unsafe_buffer_usage]] \22")
  (data (i32.const 203792) ":{8:17-8:17}:\22;\5cnint pick(std::span<int> p)\22")
  (data (i32.const 204048) ":{9:10-9:16}:\22std::span<int> p\22")
  (data (i32.const 204304) ":{9:34-9:34}:\22\5cn[[clang::unsafe_buffer_usage]] int pick(int *p) {return pick(std::span<int>(p, <# size #>));}\5cn\22")
  (func $capture (param $path i32) (param $buffer i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $got i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $buffer local.get $capacity call $read local.set $got
    local.get $fd call $close drop local.get $got)
  (func (export "load") (result i32)
    i32.const 180000 i32.const 184000 i32.const 16384 call $capture global.set $length
    global.get $length i32.const 0 i32.gt_s)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 220000 i32.const 8 call $capture i32.const 2 i32.eq
    i32.const 220000 i32.const 210000 i32.const 2 call $compare i32.eqz i32.and)
  (func (export "parent") (result i32)
    i32.const 180192 i32.const 220000 i32.const 32 call $capture i32.const 13 i32.eq
    i32.const 220000 i32.const 210016 i32.const 13 call $compare i32.eqz i32.and)
  (func (export "fixit") (param $expected i32) (param $count i32) (result i32)
    (local $at i32)
    global.get $length i32.const 0 i32.le_s if i32.const 0 return end
    local.get $count global.get $length i32.gt_u if i32.const 0 return end
    block $done loop $scan
      local.get $at global.get $length local.get $count i32.sub i32.gt_u br_if $done
      i32.const 184000 local.get $at i32.add local.get $expected local.get $count
      call $compare i32.eqz if i32.const 1 return end
      local.get $at i32.const 1 i32.add local.set $at br $scan
    end end i32.const 0))
(assert_return (invoke "load") (i32.const 1))
(assert_return (invoke "status") (i32.const 1))
(assert_return (invoke "fixit" (i32.const 202000) (i32.const 28)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 202256) (i32.const 16)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 202512) (i32.const 20)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 202768) (i32.const 23)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 203024) (i32.const 31)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 203280) (i32.const 40)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 203536) (i32.const 44)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 203792) (i32.const 44)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 204048) (i32.const 31)) (i32.const 1))
(assert_return (invoke "fixit" (i32.const 204304) (i32.const 112)) (i32.const 1))
(assert_return (invoke "parent") (i32.const 1))
