(module $file_sync_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "fsync" (func $sync (param i32) (result i32)))
  (import "libc" "ftruncate" (func $truncate (param i32 i32) (result i32)))
  (import "libc" "lseek" (func $seek (param i32 i32 i32) (result i32)))
  (import "libc" "pipe" (func $pipe (param i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/file-sync-test\00")
  (data (i32.const 180032) "abcdef")
  (func (export "regular-file") (result i32)
    (local $fd i32) (local $ok i32)
    i32.const 181000 i32.const 384 i32.store
    i32.const 180000 i32.const 578 i32.const 181000 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 180032 i32.const 6 call $write i32.const 6 i32.eq
    local.get $fd call $sync i32.eqz i32.and
    local.get $fd i32.const -1 call $truncate i32.const -1 i32.eq i32.and
    call $errno i32.load i32.const 22 i32.eq i32.and
    local.get $fd i32.const 3 call $truncate i32.eqz i32.and
    local.get $fd call $sync i32.eqz i32.and
    local.get $fd i32.const 0 i32.const 0 call $seek i32.eqz i32.and
    local.get $fd i32.const 181100 i32.const 6 call $read i32.const 3 i32.eq i32.and
    i32.const 181100 i32.load8_u i32.const 97 i32.eq i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok)
  (func (export "invalid-descriptor") (result i32)
    i32.const -1 call $sync i32.const -1 i32.eq
    call $errno i32.load i32.const 9 i32.eq i32.and)
  (func (export "pipe") (result i32)
    (local $ok i32)
    i32.const 181000 call $pipe i32.eqz
    i32.const 181000 i32.load call $sync i32.const -1 i32.eq i32.and
    call $errno i32.load i32.const 22 i32.eq i32.and local.set $ok
    i32.const 181000 i32.load call $close drop
    i32.const 181004 i32.load call $close drop
    local.get $ok))
(assert_return (invoke $file_sync_tests "regular-file") (i32.const 1))
(assert_return (invoke $file_sync_tests "invalid-descriptor") (i32.const 1))
(assert_return (invoke $file_sync_tests "pipe") (i32.const 1))
