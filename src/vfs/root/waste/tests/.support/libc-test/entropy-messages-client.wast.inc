(register "waste-libc" $waste_libc)
(module $entropy_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "waste_random_seed" (func $seed (param i32)))
  (import "waste-libc" "arc4random" (func $arc4random (result i32)))
  (import "waste-libc" "getrandom" (func $getrandom (param i32 i32 i32) (result i32)))
  (import "waste-libc" "strerror" (func $strerror (param i32) (result i32)))
  (import "waste-libc" "strsignal" (func $strsignal (param i32) (result i32)))
  (import "waste-libc" "__libc_current_sigrtmin" (func $sigrtmin (result i32)))
  (import "waste-libc" "__libc_current_sigrtmax" (func $sigrtmax (result i32)))
  (func (export "entropy-and-messages") (result i32)
    (local $random i32)
    i32.const 12345 call $seed
    call $arc4random local.set $random
    i32.const 12345 call $seed
    call $arc4random local.get $random i32.eq
    i32.const 180000 i32.const 16 i32.const 0 call $getrandom i32.const 16 i32.eq i32.and
    i32.const 2 call $strerror i32.load8_u i32.const 101 i32.eq i32.and
    i32.const 15 call $strsignal i32.load8_u i32.const 115 i32.eq i32.and
    call $sigrtmin i32.const 32 i32.eq i32.and
    call $sigrtmax i32.const 64 i32.eq i32.and))
(assert_return (invoke $entropy_tests "entropy-and-messages") (i32.const 1))
