;; Verify guest path wrappers observe the engine-owned namespace.
(register "waste-libc" $waste_libc)
(module $path_runtime_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "access" (func $access (param i32 i32) (result i32)))
  (import "waste-libc" "__errno_location" (func $errno (result i32)))
  (data (i32.const 32) "/bin/ls\00")
  (func (export "missing") (result i32)
    (call $access (i32.const 32) (i32.const 1)))
  (func (export "errno") (result i32)
    (i32.load (call $errno))))
(assert_return (invoke $path_runtime_tests "missing") (i32.const -1))
(assert_return (invoke $path_runtime_tests "errno") (i32.const 2))
