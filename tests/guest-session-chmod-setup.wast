;; Create a private directory tree for the real GNU chmod Bash session.
(module
  (import "env" "mkdir" (func $mkdir (param i32 i32) (result i32)))
  (memory 1)
  (data (i32.const 32) "/tmp/chmod-tree\00")
  (data (i32.const 64) "/tmp/chmod-tree/nested\00")
  (func (export "setup") (result i32)
    (i32.or (call $mkdir (i32.const 32) (i32.const 493))
      (call $mkdir (i32.const 64) (i32.const 493)))))
(assert_return (invoke "setup") (i32.const 0))
