;; remove() must handle files and empty directories, and preserve failures.
(module $remove
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "env" "mkdir" (func $mkdir (param i32 i32) (result i32)))
  (import "libc" "remove" (func $remove (param i32) (result i32)))
  (import "libc" "access" (func $access (param i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (data (i32.const 180000) "/tmp/libc-remove-file\00")
  (data (i32.const 180064) "/tmp/libc-remove-dir\00")
  (data (i32.const 180128) "/tmp/libc-remove-dir/child\00")
  (func (export "file") (result i32)
    i32.const 180000 i32.const 194 i32.const 384 call $open call $close drop
    i32.const 180000 call $remove i32.eqz
    i32.const 180000 i32.const 0 call $access i32.const -1 i32.eq i32.and
    call $errno i32.load i32.const 2 i32.eq i32.and)
  (func (export "nonempty-directory") (result i32)
    i32.const 180064 i32.const 448 call $mkdir drop
    i32.const 180128 i32.const 194 i32.const 384 call $open call $close drop
    i32.const 180064 call $remove i32.const -1 i32.eq
    call $errno i32.load i32.const 39 i32.eq i32.and
    i32.const 180128 i32.const 0 call $access i32.eqz i32.and)
  (func (export "empty-directory") (result i32)
    i32.const 180128 call $remove i32.eqz
    i32.const 180064 call $remove i32.eqz i32.and
    i32.const 180064 i32.const 0 call $access i32.const -1 i32.eq i32.and
    call $errno i32.load i32.const 2 i32.eq i32.and)
  (func (export "missing") (result i32)
    i32.const 180000 call $remove i32.const -1 i32.eq
    call $errno i32.load i32.const 2 i32.eq i32.and))
(assert_return (invoke $remove "file") (i32.const 1))
(assert_return (invoke $remove "nonempty-directory") (i32.const 1))
(assert_return (invoke $remove "empty-directory") (i32.const 1))
(assert_return (invoke $remove "missing") (i32.const 1))
