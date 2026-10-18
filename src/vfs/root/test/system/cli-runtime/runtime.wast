;; No Bash launcher: production resources and installed shared libc are supplied by C.
(module (import "waste-runtime" "memory" (memory 4))
  (import "libc" "strlen" (func $strlen (param i32) (result i32)))
  (data (i32.const 180000) "standalone\00")
  (func (export "length") (result i32) i32.const 180000 call $strlen))
(assert_return (invoke "length") (i32.const 10))
