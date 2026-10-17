;; File/anonymous mappings must not masquerade as committed allocator heap.
(module $vm
  (import "env" "mmap" (func $mmap (param i32 i32 i32 i32 i32 i64) (result i32)))
  (import "env" "munmap" (func $munmap (param i32 i32) (result i32)))
  (import "waste-runtime" "memory" (memory 2))
  (global $before (mut i32) (i32.const 0))
  (global $mapping (mut i32) (i32.const 0))
  (func (export "map") (result i32)
    memory.size global.set $before
    i32.const 0 i32.const 65536 i32.const 3 i32.const 34
    i32.const -1 i64.const 0 call $mmap global.set $mapping
    global.get $mapping i32.const -1 i32.ne
    global.get $mapping global.get $before i32.const 16 i32.shl i32.ge_u i32.and
    memory.size global.get $before i32.eq i32.and)
  (func (export "write-mapping") (result i32)
    global.get $mapping i32.const 0x12345678 i32.store
    global.get $mapping i32.load)
  (func (export "grow-heap") (result i32)
    i32.const 1 memory.grow global.get $before i32.eq)
  (func (export "read-mapping") (result i32) global.get $mapping i32.load)
  (func (export "reject-collision") (result i32)
    global.get $mapping i32.const 16 i32.shr_u i32.const 1 i32.add
    memory.size i32.sub memory.grow)
  (func (export "unmap") (result i32)
    global.get $mapping i32.const 65536 call $munmap)
  (func (export "heap-write") (result i32)
    global.get $before i32.const 16 i32.shl i32.const 92 i32.add
    i32.const 37 i32.store
    global.get $before i32.const 16 i32.shl i32.const 92 i32.add i32.load)
  (func (export "heap-pages") (result i32)
    memory.size global.get $before i32.const 1 i32.add i32.eq))
(assert_return (invoke $vm "map") (i32.const 1))
(assert_return (invoke $vm "write-mapping") (i32.const 0x12345678))
(assert_return (invoke $vm "grow-heap") (i32.const 1))
(assert_return (invoke $vm "read-mapping") (i32.const 0x12345678))
(assert_return (invoke $vm "reject-collision") (i32.const -1))
(assert_return (invoke $vm "read-mapping") (i32.const 0x12345678))
(assert_return (invoke $vm "unmap") (i32.const 0))
(assert_return (invoke $vm "heap-write") (i32.const 37))
(assert_return (invoke $vm "heap-pages") (i32.const 1))
