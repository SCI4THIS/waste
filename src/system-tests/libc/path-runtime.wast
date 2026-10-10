;; Run from Bash with /bin/wast --verbose; imports the installed shared libc.
;; Verify guest path wrappers observe the engine-owned namespace.
(module $path_runtime_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "access" (func $access (param i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  ;; This fixture also runs with the complete installed command namespace.
  (data (i32.const 32) "/tmp/waste-missing-path\00")
  (func (export "missing") (result i32)
    (call $access (i32.const 32) (i32.const 1)))
  (func (export "errno") (result i32)
    (i32.load (call $errno))))
(assert_return (invoke $path_runtime_tests "missing") (i32.const -1))
(assert_return (invoke $path_runtime_tests "errno") (i32.const 2))
