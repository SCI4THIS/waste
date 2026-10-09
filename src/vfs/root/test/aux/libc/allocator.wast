;; Run from Bash with /bin/wast --verbose; imports the installed shared libc.

(module $allocator_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "waste_allocator_init" (func $init (param i32) (result i32)))
  (import "libc" "malloc" (func $malloc (param i32) (result i32)))
  (import "libc" "calloc" (func $calloc (param i32 i32) (result i32)))
  (import "libc" "realloc" (func $realloc (param i32 i32) (result i32)))
  (import "libc" "free" (func $free (param i32)))
  (import "libc" "sbrk" (func $sbrk (param i32) (result i32)))
  (import "libc" "__errno_location" (func $errno_location (result i32)))
  (import "libc" "waste_heap_end" (func $heap_end (result i32)))
  (import "libc" "waste_memory_pages" (func $memory_pages (result i32)))
  (import "libc" "waste_memory_grow_calls" (func $grow_calls (result i32)))

  (func (export "alignment-and-distinct") (result i32)
    (local $a i32)
    (local $b i32)
    i32.const 1024
    call $init
    drop
    i32.const 1
    call $malloc
    local.set $a
    i32.const 33
    call $malloc
    local.set $b
    local.get $a
    i32.const 15
    i32.and
    i32.eqz
    local.get $b
    i32.const 15
    i32.and
    i32.eqz
    i32.and
    local.get $a
    local.get $b
    i32.ne
    i32.and)

  (func (export "free-reuse-and-coalesce") (result i32)
    (local $a i32)
    (local $b i32)
    (local $combined i32)
    i32.const 4096
    call $init
    drop
    i32.const 32
    call $malloc
    local.set $a
    i32.const 32
    call $malloc
    local.set $b
    local.get $a
    call $free
    local.get $b
    call $free
    i32.const 80
    call $malloc
    local.set $combined
    local.get $combined
    local.get $a
    i32.eq)

  (func (export "calloc-and-realloc") (result i32)
    (local $pointer i32)
    i32.const 8192
    call $init
    drop
    i32.const 8
    i32.const 4
    call $calloc
    local.tee $pointer
    i32.load
    i32.eqz
    local.get $pointer
    i32.const 0x78563412
    i32.store
    local.get $pointer
    i32.const 200
    call $realloc
    local.tee $pointer
    i32.load
    i32.const 0x78563412
    i32.eq
    i32.and)

  (func (export "single-page-growth") (result i32)
    (local $pointer i32)
    (local $pages i32)
    memory.size local.tee $pages i32.const 65536 i32.mul i32.const 16 i32.sub
    call $init
    drop
    i32.const 131000
    call $malloc
    local.set $pointer
    call $grow_calls
    i32.const 2
    i32.eq
    call $memory_pages
    local.get $pages i32.const 2 i32.add
    i32.eq
    i32.and
    local.get $pointer
    i32.eqz
    i32.eqz
    i32.and)

  (func (export "sbrk-and-errno") (result i32)
    (local $base i32)
    i32.const 16384
    call $init
    drop
    call $heap_end
    local.set $base
    i32.const 17
    call $sbrk
    local.get $base
    i32.eq
    i32.const -100000
    call $sbrk
    i32.const -1
    i32.eq
    i32.and
    call $errno_location
    i32.load
    i32.const 12
    i32.eq
    i32.and)
  ;; Stress real shared-libc allocation, reuse, overlap and live-byte integrity.
  ;; A bounded ring keeps interpreter execution practical in either runtime.
  (func (export "allocation-stress") (result i32)
    (local $vector i32) (local $slot i32) (local $record i32)
    (local $pointer i32) (local $size i32) (local $other i32)
    (local $index i32) (local $check i32) (local $a i32)
    memory.size i32.const 65536 i32.mul call $init drop
    i32.const 64 i32.const 16 call $calloc local.tee $vector
    i32.eqz if i32.const 0 return end
    block $done
      loop $allocate
        local.get $index i32.const 5000 i32.ge_u br_if $done
        local.get $index i32.const 63 i32.and local.set $slot
        local.get $vector local.get $slot i32.const 16 i32.mul i32.add local.set $record
        local.get $record i32.load local.tee $pointer
        if local.get $pointer call $free end
        local.get $record i32.const 0 i32.store
        local.get $index i32.const 37 i32.mul i32.const 399 i32.rem_u
        i32.const 2 i32.add local.tee $size call $malloc local.tee $pointer
        i32.eqz if i32.const 0 return end
        local.get $pointer i32.const 15 i32.and if i32.const 0 return end
        i32.const 0 local.set $check
        block $checked
          loop $live
            local.get $check i32.const 64 i32.ge_u br_if $checked
            local.get $vector local.get $check i32.const 16 i32.mul i32.add local.set $a
            local.get $a i32.load local.tee $other
            if
              local.get $pointer local.get $other local.get $a i32.load offset=4 i32.add i32.lt_u
              local.get $other local.get $pointer local.get $size i32.add i32.lt_u i32.and
              if i32.const 0 return end
              local.get $other i32.load8_u local.get $a i32.load offset=8 i32.ne
              if i32.const 0 return end
              local.get $other local.get $a i32.load offset=4 i32.add i32.const 1 i32.sub i32.load8_u
              local.get $a i32.load offset=12 i32.ne if i32.const 0 return end
            end
            local.get $check i32.const 1 i32.add local.set $check br $live
          end
        end
        local.get $record local.get $pointer i32.store
        local.get $record local.get $size i32.store offset=4
        local.get $record local.get $index i32.const 255 i32.and i32.store offset=8
        local.get $record local.get $index i32.const 3 i32.mul i32.const 255 i32.and i32.store offset=12
        local.get $pointer local.get $record i32.load offset=8 i32.store8
        local.get $pointer local.get $size i32.add i32.const 1 i32.sub
        local.get $record i32.load offset=12 i32.store8
        local.get $index i32.const 1 i32.add local.set $index br $allocate
      end
    end
    i32.const 0 local.set $check
    loop $release
      local.get $vector local.get $check i32.const 16 i32.mul i32.add i32.load call $free
      local.get $check i32.const 1 i32.add local.tee $check i32.const 64 i32.lt_u br_if $release
    end
    local.get $vector call $free
    i32.const 1))

(assert_return (invoke $allocator_tests "alignment-and-distinct") (i32.const 1))
(assert_return (invoke $allocator_tests "free-reuse-and-coalesce") (i32.const 1))
(assert_return (invoke $allocator_tests "calloc-and-realloc") (i32.const 1))
(assert_return (invoke $allocator_tests "single-page-growth") (i32.const 1))
(assert_return (invoke $allocator_tests "sbrk-and-errno") (i32.const 1))

(assert_return (invoke $allocator_tests "allocation-stress") (i32.const 1))
