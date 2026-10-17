;; Unsupported bitcode must report a useful diagnostic and produce no output.
(module $no-lto
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/lld-bitcode-status.txt\00")
  (data (i32.const 180064) "/tmp/lld-bitcode-error.txt\00")
  (data (i32.const 180128) "/tmp/bitcode-output.wasm\00")
  (data (i32.const 180192) "/tmp/lld-bitcode-archive-status.txt\00")
  (data (i32.const 180256) "/tmp/lld-bitcode-archive-error.txt\00")
  (data (i32.const 180320) "/tmp/bitcode-archive-output.wasm\00")
  (data (i32.const 182000) "LLVM bitcode/LTO support is disabled")
  (func $capture (param $path i32) (result i32)
    (local $fd i32) (local $bytes i32)
    i32.const 184000 i64.const 0 i64.store
    local.get $path i32.const 0 i32.const 0 call $open local.set $fd
    local.get $fd i32.const 184000 i32.const 2048 call $read local.set $bytes
    local.get $fd call $close drop local.get $bytes)
  (func (export "status") (param $path i32) (result i32)
    local.get $path call $capture i32.const 2 i32.eq
    i32.const 184000 i32.load16_u i32.const 0x0a31 i32.eq i32.and)
  (func (export "diagnostic") (param $path i32) (result i32)
    (local $bytes i32) (local $at i32) (local $j i32)
    local.get $path call $capture local.set $bytes
    local.get $bytes i32.const 36 i32.lt_s if i32.const 0 return end
    block $missing
      loop $search
        local.get $at i32.const 36 i32.add local.get $bytes i32.gt_u br_if $missing
        i32.const 0 local.set $j
        block $different
          loop $compare
            i32.const 184000 local.get $at i32.add local.get $j i32.add i32.load8_u
            i32.const 182000 local.get $j i32.add i32.load8_u i32.ne br_if $different
            local.get $j i32.const 1 i32.add local.tee $j i32.const 36 i32.lt_u br_if $compare
            i32.const 1 return
          end
        end
        local.get $at i32.const 1 i32.add local.set $at br $search
      end
    end
    i32.const 0)
  (func (export "absent") (param $path i32) (result i32)
    (local $fd i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const -1 i32.eq if (result i32) i32.const 1
    else local.get $fd call $close drop i32.const 0 end))
(assert_return (invoke $no-lto "status" (i32.const 180000)) (i32.const 1))
(assert_return (invoke $no-lto "diagnostic" (i32.const 180064)) (i32.const 1))
(assert_return (invoke $no-lto "absent" (i32.const 180128)) (i32.const 1))
(assert_return (invoke $no-lto "status" (i32.const 180192)) (i32.const 1))
(assert_return (invoke $no-lto "diagnostic" (i32.const 180256)) (i32.const 1))
(assert_return (invoke $no-lto "absent" (i32.const 180320)) (i32.const 1))
