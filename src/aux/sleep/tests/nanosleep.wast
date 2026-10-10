;; Run from Bash with /bin/wast --verbose; exercises the installed libc timer path.
(module $sleep_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "nanosleep" (func $nanosleep (param i32 i32) (result i32)))
  (export "nanosleep" (func $nanosleep))
  (data (i32.const 180000) "\00\00\00\00\00\00\00\00\00\00\00\00")
  (data (i32.const 180032) "\00\00\00\00\00\00\00\00\40\42\0f\00\00\00\00\00")
  (data (i32.const 180064) "\00\00\00\00\00\00\00\00\00\ca\9a\3b\00\00\00\00")
  (data (i32.const 180096) "\00\00\00\00\00\00\00\00\00\00\00\00"))

(assert_return
  (invoke $sleep_tests "nanosleep" (i32.const 180032) (i32.const 180096))
  (i32.const 0))
(assert_return
  (invoke $sleep_tests "nanosleep" (i32.const 180064) (i32.const 180096))
  (i32.const -1))
(assert_return
  (invoke $sleep_tests "nanosleep" (i32.const 180000) (i32.const 180016))
  (i32.const 0))
