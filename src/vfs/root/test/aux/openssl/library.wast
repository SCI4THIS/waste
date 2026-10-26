;; Explicitly load the provider in Bash's process before importing its API.
(module $load
  (import "waste-runtime" "memory" (memory 4))
  (import "waste-runtime" "__indirect_function_table" (table 1 funcref))
  (import "libc" "dlopen" (func $dlopen (param i32 i32) (result i32)))
  (data (i32.const 179000) "/usr/lib/libcrypto.so.wasm\00")
  (func (export "load") (result i32)
    (i32.ne (call $dlopen (i32.const 179000) (i32.const 2)) (i32.const 0))))
(assert_return (invoke $load "load") (i32.const 1))
;; Shared provider APIs, with the actual installed libcrypto and libc.
(module $crypto
  (import "waste-runtime" "memory" (memory 4))
  (import "waste-runtime" "__indirect_function_table" (table 1 funcref))
  (import "libcrypto" "SHA256" (func $sha256 (param i32 i32 i32) (result i32)))
  (import "libcrypto" "RAND_bytes" (func $random (param i32 i32) (result i32)))
  (import "libc" "memcmp" (func $cmp (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "abc")
  (data (i32.const 180032) "\ba\78\16\bf\8f\01\cf\ea\41\41\40\de\5d\ae\22\23\b0\03\61\a3\96\17\7a\9c\b4\10\ff\61\f2\00\15\ad")
  (func (export "sha256-known-vector") (result i32)
    (drop (call $sha256 (i32.const 180000) (i32.const 3) (i32.const 181000)))
    (i32.eqz (call $cmp (i32.const 181000) (i32.const 180032) (i32.const 32))))
  (func (export "secure-random") (result i32)
    (i32.and
      (i32.and (call $random (i32.const 181000) (i32.const 32))
               (call $random (i32.const 181032) (i32.const 32)))
      (i32.ne (call $cmp (i32.const 181000) (i32.const 181032) (i32.const 32)) (i32.const 0)))))
(assert_return (invoke $crypto "sha256-known-vector") (i32.const 1))
(assert_return (invoke $crypto "secure-random") (i32.const 1))
