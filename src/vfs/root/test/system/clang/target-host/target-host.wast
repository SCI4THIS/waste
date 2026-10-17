;; LLVM's actual guest TargetParser Host owner must describe the configured Wasm guest.
(module
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/target-host.txt\00")
  (data (i32.const 180128) "/tmp/target-host-status.txt\00")
  (data (i32.const 200000) "default=wasm32-unknown-unknown\0aprocess=wasm32-unknown-unknown\0acpu=generic\0afeatures=empty\0a")
  (func $capture (param $path i32) (param $output i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $count i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $output local.get $capacity call $read local.set $count
    local.get $fd call $close drop local.get $count)
  (func (export "exit-status") (result i32)
    i32.const 180128 i32.const 190000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 190000 i32.load16_u i32.const 0x3332 i32.eq i32.and
    i32.const 190002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func (export "target") (result i32)
    (local $index i32)
    i32.const 180000 i32.const 210000 i32.const 256 call $capture
    i32.const 89 i32.ne if i32.const 0 return end
    block $done
      loop $bytes
        local.get $index i32.const 89 i32.ge_u br_if $done
        i32.const 210000 local.get $index i32.add i32.load8_u
        i32.const 200000 local.get $index i32.add i32.load8_u
        i32.ne if i32.const 0 return end
        local.get $index i32.const 1 i32.add local.set $index br $bytes
      end
    end
    i32.const 1)
)
(assert_return (invoke "exit-status") (i32.const 1))
(assert_return (invoke "target") (i32.const 1))
