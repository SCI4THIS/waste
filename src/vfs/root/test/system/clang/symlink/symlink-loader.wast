;; Bash executes a final-link command and a command beneath a directory link.
(module
  (import "waste-runtime" "memory" (memory 4))
  (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "env" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/symlink-loader.txt\00")
  (func (export "read") (result i32)
    (local $fd i32) (local $size i32)
    (local.set $fd (call $open (i32.const 180000) (i32.const 0) (i32.const 0)))
    (local.set $size (call $read (local.get $fd) (i32.const 180064) (i32.const 8)))
    (call $close (local.get $fd)) drop
    local.get $size)
  (func (export "statuses") (result i32) (i32.load (i32.const 180064)))
)
(assert_return (invoke "read") (i32.const 4))
(assert_return (invoke "statuses") (i32.const 0x0a300a30))
