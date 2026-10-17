;; Actual fixed-point IR construction, C++ ioctl linkage and command exit status.
(module $compiler-imports
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/compiler-imports.bin\00")
  (data (i32.const 180128) "/tmp/compiler-imports-status.txt\00")
  (func $capture (param $path i32) (param $destination i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $length i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $destination local.get $capacity call $read local.set $length
    local.get $fd call $close drop local.get $length)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 184000 i32.const 4096 call $capture)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 190000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 190000 i32.load16_u i32.const 0x3733 i32.eq i32.and
    i32.const 190002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func $value (export "value") (param $index i32) (result i64)
    i32.const 184000 local.get $index i32.const 8 i32.mul i32.add i64.load)

)
(assert_return (invoke $compiler-imports "capture") (i32.const 80))
(assert_return (invoke $compiler-imports "status") (i32.const 1))
(assert_return (invoke $compiler-imports "value" (i32.const 0)) (i64.const 4613937818241073152))
(assert_return (invoke $compiler-imports "value" (i32.const 1)) (i64.const -1))
(assert_return (invoke $compiler-imports "value" (i32.const 2)) (i64.const 4613937818241073152))
(assert_return (invoke $compiler-imports "value" (i32.const 3)) (i64.const -384))
(assert_return (invoke $compiler-imports "value" (i32.const 4)) (i64.const 4613937818241073152))
(assert_return (invoke $compiler-imports "value" (i32.const 5)) (i64.const -3221225472))
(assert_return (invoke $compiler-imports "value" (i32.const 6)) (i64.const 4613937818241073152))
(assert_return (invoke $compiler-imports "value" (i32.const 7)) (i64.const -1649267441664))
(assert_return (invoke $compiler-imports "value" (i32.const 8)) (i64.const -1))
(assert_return (invoke $compiler-imports "value" (i32.const 9)) (i64.const 9))
