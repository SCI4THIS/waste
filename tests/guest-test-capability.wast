;; The privileged batch bridge rejects malformed/host-path requests in either
;; runner. Batch stores also leave the capability disabled by default.
(module
  (import "env" "exit" (func $exit (param i32)))
  (import "waste_kernel" "test_suite_v1" (func $suite (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "done") (call $exit (i32.const 0)))
  (data (i32.const 0) "\01\00\00\00--vfs-root=/host\00")
  (data (i32.const 64) "\02\00\00\00")
  (data (i32.const 80) "\01\00\00\00--jobs=0\00")
  (func (export "call") (param i32 i32 i32 i32) (result i32)
    (call $suite (local.get 0) (local.get 1) (local.get 2) (local.get 3))))
(assert_return (invoke "call" (i32.const 0) (i32.const 22) (i32.const 1024) (i32.const 1024)) (i32.const -1))
(assert_return (invoke "call" (i32.const 64) (i32.const 4) (i32.const 1024) (i32.const 1024)) (i32.const -1))
(assert_return (invoke "call" (i32.const 80) (i32.const 13) (i32.const 1024) (i32.const 1024)) (i32.const -1))
(assert_return (invoke "call" (i32.const 0) (i32.const -1) (i32.const 1024) (i32.const 1024)) (i32.const -1))
(assert_return (invoke "call" (i32.const 0) (i32.const 4) (i32.const 65000) (i32.const 1024)) (i32.const -1))

(invoke "done")
