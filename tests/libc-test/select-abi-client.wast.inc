;; select-abi-client.wast.inc — Verify guest-ABI type sizes for select/pselect.
;; Appended to the named waste-libc module by build-waste-libc.py.
(register "waste-libc" $waste_libc)
(module $select_abi_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "waste_fd_set_size" (func $fd_set_size (result i32)))
  (import "waste-libc" "waste_timeval_size" (func $timeval_size (result i32)))
  (import "waste-libc" "waste_timespec_size" (func $timespec_size (result i32)))
  (import "waste-libc" "waste_sigset_size" (func $sigset_size (result i32)))
  (import "waste-libc" "waste_fd_setsize" (func $fd_setsize (result i32)))
  (import "waste-libc" "waste_nfdbits" (func $nfdbits (result i32)))

  (func (export "fd-set-size") (result i32) call $fd_set_size)
  (func (export "timeval-size") (result i32) call $timeval_size)
  (func (export "timespec-size") (result i32) call $timespec_size)
  (func (export "sigset-size") (result i32) call $sigset_size)
  (func (export "fd-setsize") (result i32) call $fd_setsize)
  (func (export "nfdbits") (result i32) call $nfdbits))

(assert_return (invoke $select_abi_tests "fd-set-size") (i32.const 128))
(assert_return (invoke $select_abi_tests "timeval-size") (i32.const 16))
(assert_return (invoke $select_abi_tests "timespec-size") (i32.const 16))
(assert_return (invoke $select_abi_tests "sigset-size") (i32.const 16))
(assert_return (invoke $select_abi_tests "fd-setsize") (i32.const 1024))
(assert_return (invoke $select_abi_tests "nfdbits") (i32.const 32))
