;; This registration must stay authoritative, including when runtime is missing.
(module $mine (func (export "answer") (result i32) i32.const 42))
(register "libc" $mine)
(module (import "libc" "answer" (func $answer (result i32)))
  (func (export "test") (result i32) call $answer))
(assert_return (invoke "test") (i32.const 42))
