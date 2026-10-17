;; Shared wasm32 libc rounds ties away and checks range before integer conversion.
(module $math
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "lround" (func $lround (param f64) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (func (export "convert") (param $value f64) (param $expected i32) (result i32)
    (local $result i32)
    call $errno i32.const 73 i32.store
    local.get $value call $lround local.set $result
    local.get $result local.get $expected i32.eq
    call $errno i32.load i32.const 73 i32.eq i32.and)
  (func (export "domain") (param $value f64) (result i32)
    (local $result i32)
    call $errno i32.const 0 i32.store
    local.get $value call $lround local.set $result
    local.get $result i32.const -2147483648 i32.eq
    call $errno i32.load i32.const 33 i32.eq i32.and))
(assert_return (invoke $math "convert" (f64.const 0) (i32.const 0)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -0) (i32.const 0)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const 0.49999999999999994) (i32.const 0)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -0.49999999999999994) (i32.const 0)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const 0.5) (i32.const 1)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -0.5) (i32.const -1)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const 1.5) (i32.const 2)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -1.5) (i32.const -2)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const 0x1p-1074) (i32.const 0)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -0x1p-1074) (i32.const 0)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const 2147483647) (i32.const 2147483647)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const 2147483647.4999998) (i32.const 2147483647)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -2147483648) (i32.const -2147483648)) (i32.const 1))
(assert_return (invoke $math "convert" (f64.const -2147483648.4999995) (i32.const -2147483648)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const 2147483647.5)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const -2147483648.5)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const 0x1p63)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const -0x1p63)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const inf)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const -inf)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const nan)) (i32.const 1))
(assert_return (invoke $math "domain" (f64.const -nan)) (i32.const 1))
