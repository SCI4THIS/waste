;; Appended to a named copy of libc/waste-libc.wat by
;; tools/build-waste-libc.py. This client imports the same memory and functions
;; that a future relinked src/bash.wat will consume.
(register "waste-libc" $waste_libc)

(module $allocator_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "waste_allocator_init" (func $init (param i32) (result i32)))
  (import "waste-libc" "malloc" (func $malloc (param i32) (result i32)))
  (import "waste-libc" "calloc" (func $calloc (param i32 i32) (result i32)))
  (import "waste-libc" "realloc" (func $realloc (param i32 i32) (result i32)))
  (import "waste-libc" "free" (func $free (param i32)))
  (import "waste-libc" "sbrk" (func $sbrk (param i32) (result i32)))
  (import "waste-libc" "__errno_location" (func $errno_location (result i32)))
  (import "waste-libc" "waste_heap_end" (func $heap_end (result i32)))
  (import "waste-libc" "waste_memory_pages" (func $memory_pages (result i32)))
  (import "waste-libc" "waste_memory_grow_calls" (func $grow_calls (result i32)))

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
    i32.const 262128
    call $init
    drop
    i32.const 131000
    call $malloc
    local.set $pointer
    call $grow_calls
    i32.const 2
    i32.eq
    call $memory_pages
    i32.const 6
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
    i32.and))

(assert_return (invoke $allocator_tests "alignment-and-distinct") (i32.const 1))
(assert_return (invoke $allocator_tests "free-reuse-and-coalesce") (i32.const 1))
(assert_return (invoke $allocator_tests "calloc-and-realloc") (i32.const 1))
(assert_return (invoke $allocator_tests "single-page-growth") (i32.const 1))
(assert_return (invoke $allocator_tests "sbrk-and-errno") (i32.const 1))
