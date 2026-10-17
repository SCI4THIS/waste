;; Real shared allocator blocks: alignment, ownership, reuse and failures.
(module $aligned
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "waste_allocator_init" (func $init (param i32) (result i32)))
  (import "libc" "aligned_alloc" (func $aligned (param i32 i32) (result i32)))
  (import "libc" "posix_memalign" (func $posix (param i32 i32 i32) (result i32)))
  (import "libc" "malloc" (func $malloc (param i32) (result i32)))
  (import "libc" "realloc" (func $realloc (param i32 i32) (result i32)))
  (import "libc" "free" (func $free (param i32)))
  (import "libc" "waste_heap_end" (func $heap_end (result i32)))
  (import "libc" "waste_memory_grow_calls" (func $growth (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (func $fresh memory.size i32.const 65536 i32.mul call $init drop)
  (func (export "powers-of-two") (result i32)
    (local $alignment i32) (local $p i32)
    call $fresh
    i32.const 1 local.set $alignment
    loop $next
      local.get $alignment local.get $alignment call $aligned local.tee $p
      i32.eqz if i32.const 0 return end
      local.get $p local.get $alignment i32.const 1 i32.sub i32.and
      if i32.const 0 return end
      local.get $p i32.const 71 i32.store8
      local.get $p local.get $alignment i32.add i32.const 1 i32.sub
      i32.const 71 i32.store8
      local.get $p call $free
      local.get $alignment i32.const 1 i32.shl local.tee $alignment
      i32.const 65536 i32.le_u br_if $next
    end i32.const 1)
  (func (export "padding-reuse-and-coalesce") (result i32)
    (local $first i32) (local $p i32) (local $end i32)
    call $fresh
    i32.const 32 call $malloc local.set $first
    i32.const 4096 i32.const 8192 call $aligned local.set $p
    call $heap_end local.set $end
    local.get $p call $free
    i32.const 4096 i32.const 8192 call $aligned local.get $p i32.eq
    local.get $p call $free
    call $heap_end local.get $end i32.eq i32.and
    local.get $first call $free
    i32.const 12000 call $malloc local.get $first i32.eq i32.and
    call $heap_end local.get $end i32.eq i32.and)
  (func (export "aligned-realloc-preserves-bytes") (result i32)
    (local $p i32)
    call $fresh
    i32.const 4096 i32.const 4096 call $aligned local.set $p
    local.get $p i32.const 0x12345678 i32.store
    local.get $p i32.const 4092 i32.add i32.const 0xabcdef12 i32.store
    local.get $p i32.const 8192 call $realloc local.tee $p
    i32.eqz if i32.const 0 return end
    local.get $p i32.load i32.const 0x12345678 i32.eq
    local.get $p i32.const 4092 i32.add i32.load i32.const 0xabcdef12 i32.eq i32.and
    local.get $p call $free)
  (func (export "minimum-padding-preserves-neighbor") (result i32)
    (local $first i32) (local $p i32)
    call $fresh
    i32.const 13 call $malloc local.set $first
    local.get $first i32.const 0x12345678 i32.store
    local.get $first i32.const 12 i32.add i32.const 71 i32.store8
    ;; This raw allocation starts sixteen bytes short of its alignment.
    ;; Padding must move to the next boundary to hold a complete free block.
    i32.const 32 i32.const 32 call $aligned local.tee $p
    i32.eqz if i32.const 0 return end
    local.get $p i32.const 31 i32.and i32.eqz
    local.get $p i32.const -1 i32.store
    local.get $p i32.const 28 i32.add i32.const -1 i32.store
    local.get $first i32.load i32.const 0x12345678 i32.eq i32.and
    local.get $first i32.const 12 i32.add i32.load8_u i32.const 71 i32.eq i32.and
    local.get $p call $free local.get $first call $free
    i32.const 96 call $malloc local.get $first i32.eq i32.and)
  (func (export "posix-alignment-and-errno") (result i32)
    (local $slot i32) (local $p i32) (local $alignment i32)
    call $fresh
    i32.const 4 call $malloc local.set $slot
    call $errno i32.const 17 i32.store
    i32.const 4 local.set $alignment
    loop $next
      local.get $slot local.get $alignment i32.const 13 call $posix
      if i32.const 0 return end
      local.get $slot i32.load local.tee $p i32.eqz
      if i32.const 0 return end
      local.get $p local.get $alignment i32.const 1 i32.sub i32.and
      if i32.const 0 return end
      local.get $p i32.const 13 i32.store8
      local.get $p i32.const 12 i32.add i32.const 37 i32.store8
      local.get $p call $free
      call $errno i32.load i32.const 17 i32.ne
      if i32.const 0 return end
      local.get $alignment i32.const 1 i32.shl local.tee $alignment
      i32.const 65536 i32.le_u br_if $next
    end
    local.get $slot call $free i32.const 1)
  (func (export "aligned-invalid") (result i32)
    call $fresh
    i32.const 0 i32.const 16 call $aligned i32.eqz
    call $errno i32.load i32.const 22 i32.eq i32.and
    i32.const 3 i32.const 15 call $aligned i32.eqz i32.and
    call $errno i32.load i32.const 22 i32.eq i32.and
    i32.const 64 i32.const 65 call $aligned i32.eqz i32.and
    call $errno i32.load i32.const 22 i32.eq i32.and
    call $growth i32.eqz i32.and)
  (func (export "posix-invalid-preserves-output") (result i32)
    (local $slot i32)
    call $fresh i32.const 4 call $malloc local.set $slot
    local.get $slot i32.const 0x12345678 i32.store
    call $errno i32.const 17 i32.store
    local.get $slot i32.const 0 i32.const 16 call $posix i32.const 22 i32.eq
    local.get $slot i32.const 2 i32.const 16 call $posix i32.const 22 i32.eq i32.and
    local.get $slot i32.const 24 i32.const 16 call $posix i32.const 22 i32.eq i32.and
    i32.const 0 i32.const 64 i32.const 16 call $posix i32.const 22 i32.eq i32.and
    local.get $slot i32.load i32.const 0x12345678 i32.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and
    local.get $slot call $free)
  (func (export "overflow-rejected-before-growth") (result i32)
    call $fresh
    i32.const 16 i32.const 0xffffffe0 call $aligned i32.eqz
    call $errno i32.load i32.const 12 i32.eq i32.and
    i32.const 0x80000000 i32.const 0 call $aligned i32.eqz i32.and
    call $errno i32.load i32.const 12 i32.eq i32.and
    i32.const 4096 i32.const 0xfffff000 call $aligned i32.eqz i32.and
    i32.const 0xffffffe0 call $malloc i32.eqz i32.and
    call $errno i32.load i32.const 12 i32.eq i32.and
    call $growth i32.eqz i32.and)
  (func (export "posix-oom-preserves-output-and-errno") (result i32)
    (local $slot i32) (local $growth i32)
    call $fresh i32.const 4 call $malloc local.set $slot
    call $growth local.set $growth
    local.get $slot i32.const 0x12345678 i32.store
    call $errno i32.const 17 i32.store
    local.get $slot i32.const 4096 i32.const -1 call $posix i32.const 12 i32.eq
    local.get $slot i32.const 0x80000000 i32.const 0 call $posix i32.const 12 i32.eq i32.and
    local.get $slot i32.load i32.const 0x12345678 i32.eq i32.and
    call $errno i32.load i32.const 17 i32.eq i32.and
    call $growth local.get $growth i32.eq i32.and
    local.get $slot call $free)
  (func (export "zero-size-freeable") (result i32)
    (local $p i32) (local $slot i32)
    call $fresh
    i32.const 4096 i32.const 0 call $aligned local.tee $p i32.eqz i32.eqz
    local.get $p i32.const 4095 i32.and i32.eqz i32.and
    local.get $p call $free
    i32.const 4 call $malloc local.set $slot
    local.get $slot i32.const 8192 i32.const 0 call $posix i32.eqz i32.and
    local.get $slot i32.load local.tee $p i32.eqz i32.eqz i32.and
    local.get $p i32.const 8191 i32.and i32.eqz i32.and
    local.get $p call $free local.get $slot call $free)
  (func (export "mixed-live-block-stress") (result i32)
    (local $records i32) (local $record i32) (local $other i32)
    (local $p i32) (local $alignment i32) (local $size i32)
    (local $index i32) (local $check i32) (local $q i32)
    call $fresh i32.const 256 call $malloc local.set $records
    local.get $records i32.const 0 i32.const 256 memory.fill
    loop $next
      local.get $records local.get $index i32.const 15 i32.and i32.const 16 i32.mul
      i32.add local.set $record
      local.get $record i32.load call $free
      local.get $record i32.const 0 i32.store
      i32.const 16 local.get $index i32.const 9 i32.rem_u i32.shl local.set $alignment
      local.get $alignment i32.const 2 i32.mul local.set $size
      local.get $index i32.const 1 i32.and
      if
        local.get $record local.get $alignment local.get $size call $posix
        if i32.const 0 return end
        local.get $record i32.load local.set $p
      else
        local.get $alignment local.get $size call $aligned local.set $p
      end
      local.get $p i32.eqz if i32.const 0 return end
      local.get $p local.get $alignment i32.const 1 i32.sub i32.and
      if i32.const 0 return end
      i32.const 0 local.set $check
      loop $live
        local.get $records local.get $check i32.const 16 i32.mul i32.add local.set $other
        local.get $other local.get $record i32.ne
        if
          local.get $other i32.load local.tee $q
          if
            local.get $p local.get $q local.get $other i32.load offset=4 i32.add i32.lt_u
            local.get $q local.get $p local.get $size i32.add i32.lt_u i32.and
            if i32.const 0 return end
            local.get $q i32.load local.get $other i32.load offset=8 i32.ne
            if i32.const 0 return end
            local.get $q local.get $other i32.load offset=4 i32.add i32.const 4 i32.sub i32.load
            local.get $other i32.load offset=8 i32.ne if i32.const 0 return end
          end
        end
        local.get $check i32.const 1 i32.add local.tee $check i32.const 16 i32.lt_u br_if $live
      end
      local.get $record local.get $p i32.store
      local.get $record local.get $size i32.store offset=4
      local.get $record local.get $index i32.const 1 i32.add i32.store offset=8
      local.get $p local.get $record i32.load offset=8 i32.store
      local.get $p local.get $size i32.add i32.const 4 i32.sub
      local.get $record i32.load offset=8 i32.store
      local.get $index i32.const 1 i32.add local.tee $index i32.const 256 i32.lt_u br_if $next
    end
    i32.const 0 local.set $check
    loop $release
      local.get $records local.get $check i32.const 16 i32.mul i32.add i32.load call $free
      local.get $check i32.const 1 i32.add local.tee $check i32.const 16 i32.lt_u br_if $release
    end
    local.get $records call $free i32.const 1))

(assert_return (invoke $aligned "powers-of-two") (i32.const 1))
(assert_return (invoke $aligned "padding-reuse-and-coalesce") (i32.const 1))
(assert_return (invoke $aligned "aligned-realloc-preserves-bytes") (i32.const 1))
(assert_return (invoke $aligned "minimum-padding-preserves-neighbor") (i32.const 1))
(assert_return (invoke $aligned "posix-alignment-and-errno") (i32.const 1))
(assert_return (invoke $aligned "aligned-invalid") (i32.const 1))
(assert_return (invoke $aligned "posix-invalid-preserves-output") (i32.const 1))
(assert_return (invoke $aligned "overflow-rejected-before-growth") (i32.const 1))
(assert_return (invoke $aligned "posix-oom-preserves-output-and-errno") (i32.const 1))
(assert_return (invoke $aligned "zero-size-freeable") (i32.const 1))
(assert_return (invoke $aligned "mixed-live-block-stress") (i32.const 1))
