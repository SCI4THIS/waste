;; Complete production ExtTSP layout, scoring and real command-line configuration.
(module $layout
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/code-layout-default.bin\00")
  (data (i32.const 180128) "/tmp/code-layout-weighted.bin\00")
  (data (i32.const 180256) "/tmp/code-layout-status.txt\00")
  (func $capture (param $path i32) (param $output i32) (param $capacity i32) (result i32)
    (local $fd i32) (local $length i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $output local.get $capacity call $read local.set $length
    local.get $fd call $close drop local.get $length)
  (func (export "capture-default") (result i32)
    i32.const 180000 i32.const 184000 i32.const 4096 call $capture)
  (func (export "capture-weighted") (result i32)
    i32.const 180128 i32.const 188000 i32.const 4096 call $capture)
  (func (export "statuses") (result i32)
    i32.const 180256 i32.const 193000 i32.const 8 call $capture i32.const 6 i32.eq
    i32.const 193000 i32.load16_u i32.const 0x3533 i32.eq i32.and
    i32.const 193002 i32.load8_u i32.const 10 i32.eq i32.and
    i32.const 193003 i32.load16_u i32.const 0x3533 i32.eq i32.and
    i32.const 193005 i32.load8_u i32.const 10 i32.eq i32.and)
  (func $address (param $profile i32) (param $index i32) (result i32)
    local.get $profile if (result i32) i32.const 188000 else i32.const 184000 end
    local.get $index i32.const 8 i32.mul i32.add)
  (func $value (export "value") (param $profile i32) (param $index i32) (result i64)
    local.get $profile local.get $index call $address i64.load)
  (func (export "near-score") (param $profile i32) (param $index i32) (param $expected f64) (result i32)
    local.get $profile local.get $index call $address f64.load
    local.get $expected f64.sub f64.abs f64.const 0.000000000002 f64.le)
  (func (export "reversed-chain") (result i32)
    i32.const 0 i32.const 4 call $value i64.const 3 i64.eq
    i32.const 0 i32.const 5 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 6 call $value i64.const 2 i64.eq i32.and
    i32.const 0 i32.const 7 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 4 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 5 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 6 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 7 call $value i64.const 1 i64.eq i32.and
  )
  (func (export "forward-chain") (result i32)
    i32.const 0 i32.const 11 call $value i64.const 4 i64.eq
    i32.const 0 i32.const 12 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 13 call $value i64.const 1 i64.eq i32.and
    i32.const 0 i32.const 14 call $value i64.const 2 i64.eq i32.and
    i32.const 0 i32.const 15 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 11 call $value i64.const 4 i64.eq i32.and
    i32.const 1 i32.const 12 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 13 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 14 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 15 call $value i64.const 3 i64.eq i32.and
  )
  (func (export "cold-disconnected") (result i32)
    i32.const 0 i32.const 19 call $value i64.const 3 i64.eq
    i32.const 0 i32.const 20 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 21 call $value i64.const 1 i64.eq i32.and
    i32.const 0 i32.const 22 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 19 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 20 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 21 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 22 call $value i64.const 2 i64.eq i32.and
  )
  (func (export "hot-disconnected") (result i32)
    i32.const 0 i32.const 26 call $value i64.const 4 i64.eq
    i32.const 0 i32.const 27 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 28 call $value i64.const 2 i64.eq i32.and
    i32.const 0 i32.const 29 call $value i64.const 3 i64.eq i32.and
    i32.const 0 i32.const 30 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 26 call $value i64.const 4 i64.eq i32.and
    i32.const 1 i32.const 27 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 28 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 29 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 30 call $value i64.const 1 i64.eq i32.and
  )
  (func (export "hot-cold-diamond") (result i32)
    i32.const 0 i32.const 34 call $value i64.const 4 i64.eq
    i32.const 0 i32.const 35 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 36 call $value i64.const 1 i64.eq i32.and
    i32.const 0 i32.const 37 call $value i64.const 3 i64.eq i32.and
    i32.const 0 i32.const 38 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 34 call $value i64.const 4 i64.eq i32.and
    i32.const 1 i32.const 35 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 36 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 37 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 38 call $value i64.const 2 i64.eq i32.and
  )
  (func (export "backward-loop") (result i32)
    i32.const 0 i32.const 42 call $value i64.const 3 i64.eq
    i32.const 0 i32.const 43 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 44 call $value i64.const 1 i64.eq i32.and
    i32.const 0 i32.const 45 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 42 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 43 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 44 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 45 call $value i64.const 2 i64.eq i32.and
  )
  (func (export "zero-count-conditional-edge") (result i32)
    i32.const 0 i32.const 49 call $value i64.const 3 i64.eq
    i32.const 0 i32.const 50 call $value i64.const 0 i64.eq i32.and
    i32.const 0 i32.const 51 call $value i64.const 1 i64.eq i32.and
    i32.const 0 i32.const 52 call $value i64.const 2 i64.eq i32.and
    i32.const 1 i32.const 49 call $value i64.const 3 i64.eq i32.and
    i32.const 1 i32.const 50 call $value i64.const 0 i64.eq i32.and
    i32.const 1 i32.const 51 call $value i64.const 1 i64.eq i32.and
    i32.const 1 i32.const 52 call $value i64.const 2 i64.eq i32.and
  )
)
(assert_return (invoke "capture-default") (i32.const 512))
(assert_return (invoke "capture-weighted") (i32.const 512))
(assert_return (invoke "statuses") (i32.const 1))
(assert_return (invoke "value" (i32.const 0) (i32.const 0)) (i64.const 1))
(assert_return (invoke "value" (i32.const 0) (i32.const 1)) (i64.const 1))
(assert_return (invoke "value" (i32.const 0) (i32.const 2)) (i64.const 0))
(assert_return (invoke "value" (i32.const 0) (i32.const 3)) (i64.const 0))
(assert_return (invoke "value" (i32.const 1) (i32.const 0)) (i64.const 1))
(assert_return (invoke "value" (i32.const 1) (i32.const 1)) (i64.const 1))
(assert_return (invoke "value" (i32.const 1) (i32.const 2)) (i64.const 0))
(assert_return (invoke "value" (i32.const 1) (i32.const 3)) (i64.const 0))
(assert_return (invoke "reversed-chain") (i32.const 1))
(assert_return (invoke "forward-chain") (i32.const 1))
(assert_return (invoke "cold-disconnected") (i32.const 1))
(assert_return (invoke "hot-disconnected") (i32.const 1))
(assert_return (invoke "hot-cold-diamond") (i32.const 1))
(assert_return (invoke "backward-loop") (i32.const 1))
(assert_return (invoke "zero-count-conditional-edge") (i32.const 1))
;; reversed-chain, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 8) (f64.const 19.58984375)) (i32.const 1))
;; reversed-chain, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 9) (f64.const 210)) (i32.const 1))
;; reversed-chain, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 10) (f64.const 210)) (i32.const 1))
;; reversed-chain, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 8) (f64.const 19.58984375)) (i32.const 1))
;; reversed-chain, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 9) (f64.const 400)) (i32.const 1))
;; reversed-chain, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 10) (f64.const 400)) (i32.const 1))
;; forward-chain, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 16) (f64.const 315)) (i32.const 1))
;; forward-chain, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 17) (f64.const 315)) (i32.const 1))
;; forward-chain, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 18) (f64.const 315)) (i32.const 1))
;; forward-chain, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 16) (f64.const 600)) (i32.const 1))
;; forward-chain, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 17) (f64.const 600)) (i32.const 1))
;; forward-chain, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 18) (f64.const 600)) (i32.const 1))
;; cold-disconnected, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 23) (f64.const 0)) (i32.const 1))
;; cold-disconnected, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 24) (f64.const 0)) (i32.const 1))
;; cold-disconnected, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 25) (f64.const 0)) (i32.const 1))
;; cold-disconnected, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 23) (f64.const 0)) (i32.const 1))
;; cold-disconnected, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 24) (f64.const 0)) (i32.const 1))
;; cold-disconnected, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 25) (f64.const 0)) (i32.const 1))
;; hot-disconnected, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 31) (f64.const 114.90234375)) (i32.const 1))
;; hot-disconnected, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 32) (f64.const 210)) (i32.const 1))
;; hot-disconnected, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 33) (f64.const 210)) (i32.const 1))
;; hot-disconnected, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 31) (f64.const 209.90234375)) (i32.const 1))
;; hot-disconnected, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 32) (f64.const 400)) (i32.const 1))
;; hot-disconnected, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 33) (f64.const 400)) (i32.const 1))
;; hot-cold-diamond, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 39) (f64.const 110.3984375)) (i32.const 1))
;; hot-cold-diamond, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 40) (f64.const 186.415625)) (i32.const 1))
;; hot-cold-diamond, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 41) (f64.const 186.415625)) (i32.const 1))
;; hot-cold-diamond, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 39) (f64.const 119.8984375)) (i32.const 1))
;; hot-cold-diamond, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 40) (f64.const 271.915625)) (i32.const 1))
;; hot-cold-diamond, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 41) (f64.const 271.915625)) (i32.const 1))
;; backward-loop, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 46) (f64.const 218.71875)) (i32.const 1))
;; backward-loop, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 47) (f64.const 218.71875)) (i32.const 1))
;; backward-loop, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 48) (f64.const 218.71875)) (i32.const 1))
;; backward-loop, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 46) (f64.const 408.71875)) (i32.const 1))
;; backward-loop, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 47) (f64.const 408.71875)) (i32.const 1))
;; backward-loop, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 48) (f64.const 408.71875)) (i32.const 1))
;; zero-count-conditional-edge, default, identity
(assert_return (invoke "near-score" (i32.const 0) (i32.const 53) (f64.const 100)) (i32.const 1))
;; zero-count-conditional-edge, default, optimized
(assert_return (invoke "near-score" (i32.const 0) (i32.const 54) (f64.const 100)) (i32.const 1))
;; zero-count-conditional-edge, default, supplied order
(assert_return (invoke "near-score" (i32.const 0) (i32.const 55) (f64.const 100)) (i32.const 1))
;; zero-count-conditional-edge, weighted, identity
(assert_return (invoke "near-score" (i32.const 1) (i32.const 53) (f64.const 100)) (i32.const 1))
;; zero-count-conditional-edge, weighted, optimized
(assert_return (invoke "near-score" (i32.const 1) (i32.const 54) (f64.const 100)) (i32.const 1))
;; zero-count-conditional-edge, weighted, supplied order
(assert_return (invoke "near-score" (i32.const 1) (i32.const 55) (f64.const 100)) (i32.const 1))
;; forward-limit, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 56) (f64.const 0)) (i32.const 1))
;; forward-beyond-limit, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 57) (f64.const 0)) (i32.const 1))
;; forward-before-limit, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 58) (f64.const 0.009765625)) (i32.const 1))
;; backward-limit, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 59) (f64.const 0)) (i32.const 1))
;; backward-beyond-limit, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 60) (f64.const 0)) (i32.const 1))
;; backward-before-limit, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 61) (f64.const 0.015625)) (i32.const 1))
;; zero-count, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 62) (f64.const 0)) (i32.const 1))
;; self-loop, profile 0
(assert_return (invoke "near-score" (i32.const 0) (i32.const 63) (f64.const 9.84375)) (i32.const 1))
;; forward-limit, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 56) (f64.const 0)) (i32.const 1))
;; forward-beyond-limit, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 57) (f64.const 0)) (i32.const 1))
;; forward-before-limit, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 58) (f64.const 0.009765625)) (i32.const 1))
;; backward-limit, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 59) (f64.const 0)) (i32.const 1))
;; backward-beyond-limit, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 60) (f64.const 0)) (i32.const 1))
;; backward-before-limit, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 61) (f64.const 0.015625)) (i32.const 1))
;; zero-count, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 62) (f64.const 0)) (i32.const 1))
;; self-loop, profile 1
(assert_return (invoke "near-score" (i32.const 1) (i32.const 63) (f64.const 9.84375)) (i32.const 1))
