;; Actual CFG node attributes and retained output after owner destruction.
(module $cfg
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/cfg-format.bin\00")
  (data (i32.const 180128) "/tmp/cfg-format-status.txt\00")
  (data (i32.const 180256) "/tmp/cfg-attributes.txt\00")
  (data (i32.const 200000) "\0a\0aid=\22owned-entry\22\0aid=\22\22\0aid=\22A\00B\22\0acolor=\22#3d50c3ff\22, style=filled, fillcolor=\22#3d50c370\22, fontname=\22Courier\22\0aid=\22owned-entry\22,color=\22#3d50c3ff\22, style=filled, fillcolor=\22#3d50c370\22, fontname=\22Courier\22\0acolor=\22#3d50c3ff\22, style=filled, fillcolor=\22#3d50c370\22, fontname=\22Courier\22\0aid=\22\22,color=\22#3d50c3ff\22, style=filled, fillcolor=\22#3d50c370\22, fontname=\22Courier\22\0aid=\22qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq\22,color=\22#3d50c3ff\22, style=filled, fillcolor=\22#3d50c370\22, fontname=\22Courier\22\0aid=\22yield-entry\22,color=\22#3d50c3ff\22, style=filled, fillcolor=\22#3d50c370\22, fontname=\22Courier\22\0a")
  (func $capture (param $path i32) (param $output i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $count i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $output local.get $capacity call $read local.set $count
    local.get $fd call $close drop local.get $count)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 184000 i32.const 4096 call $capture)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 190000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 190000 i32.load16_u i32.const 0x3233 i32.eq i32.and
    i32.const 190002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func (export "value") (param $index i32) (result i64)
    i32.const 184000 local.get $index i32.const 8 i32.mul i32.add i64.load)
  (func (export "exact-owned-attributes") (result i32)
    i32.const 180256 i32.const 210000 i32.const 4096 call $capture i32.const 786 i32.eq
    i32.const 210000 i32.const 200000 i32.const 786 call $compare i32.eqz i32.and)
)
(assert_return (invoke "capture") (i32.const 560))
(assert_return (invoke "status") (i32.const 1))

;; Case 0: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 0)) (i64.const 0))
(assert_return (invoke "value" (i32.const 1)) (i64.const 0))
(assert_return (invoke "value" (i32.const 2)) (i64.const 0))
(assert_return (invoke "value" (i32.const 3)) (i64.const 0))
(assert_return (invoke "value" (i32.const 4)) (i64.const 0))
(assert_return (invoke "value" (i32.const 5)) (i64.const 0))

;; Case 1: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 6)) (i64.const 0))
(assert_return (invoke "value" (i32.const 7)) (i64.const 0))
(assert_return (invoke "value" (i32.const 8)) (i64.const 1))
(assert_return (invoke "value" (i32.const 9)) (i64.const 1))
(assert_return (invoke "value" (i32.const 10)) (i64.const 0))
(assert_return (invoke "value" (i32.const 11)) (i64.const 1))

;; Case 2: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 12)) (i64.const 16))
(assert_return (invoke "value" (i32.const 13)) (i64.const 0))
(assert_return (invoke "value" (i32.const 14)) (i64.const 1))
(assert_return (invoke "value" (i32.const 15)) (i64.const 1))
(assert_return (invoke "value" (i32.const 16)) (i64.const 0))
(assert_return (invoke "value" (i32.const 17)) (i64.const 1))

;; Case 3: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 18)) (i64.const 5))
(assert_return (invoke "value" (i32.const 19)) (i64.const 0))
(assert_return (invoke "value" (i32.const 20)) (i64.const 1))
(assert_return (invoke "value" (i32.const 21)) (i64.const 1))
(assert_return (invoke "value" (i32.const 22)) (i64.const 0))
(assert_return (invoke "value" (i32.const 23)) (i64.const 1))

;; Case 4: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 24)) (i64.const 8))
(assert_return (invoke "value" (i32.const 25)) (i64.const 0))
(assert_return (invoke "value" (i32.const 26)) (i64.const 1))
(assert_return (invoke "value" (i32.const 27)) (i64.const 1))
(assert_return (invoke "value" (i32.const 28)) (i64.const 0))
(assert_return (invoke "value" (i32.const 29)) (i64.const 1))

;; Case 5: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 30)) (i64.const 74))
(assert_return (invoke "value" (i32.const 31)) (i64.const 0))
(assert_return (invoke "value" (i32.const 32)) (i64.const 0))
(assert_return (invoke "value" (i32.const 33)) (i64.const 0))
(assert_return (invoke "value" (i32.const 34)) (i64.const 1))
(assert_return (invoke "value" (i32.const 35)) (i64.const 0))

;; Case 6: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 36)) (i64.const 91))
(assert_return (invoke "value" (i32.const 37)) (i64.const 0))
(assert_return (invoke "value" (i32.const 38)) (i64.const 1))
(assert_return (invoke "value" (i32.const 39)) (i64.const 1))
(assert_return (invoke "value" (i32.const 40)) (i64.const 1))
(assert_return (invoke "value" (i32.const 41)) (i64.const 1))

;; Case 7: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 42)) (i64.const 74))
(assert_return (invoke "value" (i32.const 43)) (i64.const 0))
(assert_return (invoke "value" (i32.const 44)) (i64.const 1))
(assert_return (invoke "value" (i32.const 45)) (i64.const 1))
(assert_return (invoke "value" (i32.const 46)) (i64.const 1))
(assert_return (invoke "value" (i32.const 47)) (i64.const 1))

;; Case 8: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 48)) (i64.const 80))
(assert_return (invoke "value" (i32.const 49)) (i64.const 0))
(assert_return (invoke "value" (i32.const 50)) (i64.const 1))
(assert_return (invoke "value" (i32.const 51)) (i64.const 1))
(assert_return (invoke "value" (i32.const 52)) (i64.const 1))
(assert_return (invoke "value" (i32.const 53)) (i64.const 1))

;; Case 9: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 54)) (i64.const 336))
(assert_return (invoke "value" (i32.const 55)) (i64.const 0))
(assert_return (invoke "value" (i32.const 56)) (i64.const 1))
(assert_return (invoke "value" (i32.const 57)) (i64.const 1))
(assert_return (invoke "value" (i32.const 58)) (i64.const 1))
(assert_return (invoke "value" (i32.const 59)) (i64.const 1))

;; Case 10: size, frequency, formatter, callbacks, heat, callback node.
(assert_return (invoke "value" (i32.const 60)) (i64.const 91))
(assert_return (invoke "value" (i32.const 61)) (i64.const 0))
(assert_return (invoke "value" (i32.const 62)) (i64.const 1))
(assert_return (invoke "value" (i32.const 63)) (i64.const 1))
(assert_return (invoke "value" (i32.const 64)) (i64.const 1))
(assert_return (invoke "value" (i32.const 65)) (i64.const 1))
(assert_return (invoke "value" (i32.const 66)) (i64.const 0))
(assert_return (invoke "value" (i32.const 67)) (i64.const 9))
(assert_return (invoke "value" (i32.const 68)) (i64.const 1))
(assert_return (invoke "value" (i32.const 69)) (i64.const 786))
(assert_return (invoke "exact-owned-attributes") (i32.const 1))
