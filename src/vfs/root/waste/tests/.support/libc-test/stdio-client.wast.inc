(register "waste-libc" $waste_libc)
(module $stdio_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "waste_allocator_init" (func $allocator_init (param i32) (result i32)))
  (import "waste-libc" "waste_stdio_init" (func $stdio_init (param i32) (result i32)))
  (import "waste-libc" "waste_stdout" (func $stdout (result i32)))
  (import "waste-libc" "waste_file_data" (func $file_data (param i32) (result i32)))
  (import "waste-libc" "waste_file_length" (func $file_length (param i32) (result i32)))
  (import "waste-libc" "waste_fmemopen" (func $fmemopen (param i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "snprintf" (func $snprintf (param i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "printf" (func $printf (param i32 i32) (result i32)))
  (import "waste-libc" "fgets" (func $fgets (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "hello\00")
  (data (i32.const 180016) "hello %s %d %08x\00")
  (data (i32.const 180048) "hello hello -42 0000002a\00")
  (data (i32.const 180080) "value=%d\00")
  (data (i32.const 180096) "value=17\00")
  (data (i32.const 180112) "one\0aTwo")

  (func $equal (param $left i32) (param $right i32) (result i32)
    (local $a i32) (local $b i32)
    loop $characters
      local.get $left
      i32.load8_u
      local.tee $a
      local.get $right
      i32.load8_u
      local.tee $b
      i32.ne
      if i32.const 0 return end
      local.get $a
      i32.eqz
      if i32.const 1 return end
      local.get $left i32.const 1 i32.add local.set $left
      local.get $right i32.const 1 i32.add local.set $right
      br $characters
    end
    unreachable)

  (func (export "formatting") (result i32)
    i32.const 220000 call $allocator_init drop
    i32.const 181100 i32.const 180000 i32.store
    i32.const 181104 i32.const -42 i32.store
    i32.const 181108 i32.const 42 i32.store
    i32.const 181000 i32.const 64 i32.const 180016 i32.const 181100 call $snprintf
    i32.const 24 i32.eq
    i32.const 181000 i32.const 180048 call $equal
    i32.and)

  (func (export "stdout-buffer") (result i32)
    (local $file i32) (local $data i32)
    i32.const 220000 call $allocator_init drop
    i32.const 128 call $stdio_init drop
    i32.const 181100 i32.const 17 i32.store
    i32.const 180080 i32.const 181100 call $printf drop
    call $stdout local.tee $file call $file_data local.set $data
    local.get $file call $file_length i32.const 8 i32.eq
    local.get $data i32.const 180096 call $equal i32.and)

  (func (export "memory-file-input") (result i32)
    (local $file i32)
    i32.const 220000 call $allocator_init drop
    i32.const 180112 i32.const 7 i32.const 7 i32.const 1 call $fmemopen local.set $file
    i32.const 181000 i32.const 8 local.get $file call $fgets drop
    i32.const 181000 i32.load
    i32.const 0x0a656e6f i32.eq))
(assert_return (invoke $stdio_tests "formatting") (i32.const 1))
(assert_return (invoke $stdio_tests "stdout-buffer") (i32.const 1))
(assert_return (invoke $stdio_tests "memory-file-input") (i32.const 1))
