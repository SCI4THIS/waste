;; Kernel ABI bounds and failure atomicity. Run via native/browser process driver.
(module $entropy
  (import "waste_kernel" "getrandom_v1" (func $random (param i32 i32 i32) (result i32)))
  (memory 1)
  (data (i32.const 65532) "keep")
  (func (export "random") (param i32 i32 i32) (result i32)
    local.get 0 local.get 1 local.get 2 call $random)
  (func (export "sentinel") (result i32) (i32.load (i32.const 65532))))
(assert_return (invoke $entropy "random" (i32.const 0) (i32.const 0) (i32.const 0)) (i32.const 0))
(assert_return (invoke $entropy "random" (i32.const 0) (i32.const 4) (i32.const 0)) (i32.const -14))
(assert_return (invoke $entropy "random" (i32.const 65532) (i32.const 8) (i32.const 0)) (i32.const -14))
(assert_return (invoke $entropy "sentinel") (i32.const 0x7065656b))
(assert_return (invoke $entropy "random" (i32.const 1024) (i32.const 32) (i32.const 8)) (i32.const -22))
(assert_return (invoke $entropy "random" (i32.const 1024) (i32.const 512) (i32.const 0)) (i32.const 512))
