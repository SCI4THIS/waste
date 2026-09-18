;; environment-boundaries-client.wast.inc — Per-call boundary checks.
;; Each unsupported POSIX call gets its own exported function and assertion
;; so that a regression identifies the exact call and errno.
(register "waste-libc" $waste_libc)
(module $boundary_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "__errno_location" (func $errno_location (result i32)))
  (import "waste-libc" "execve" (func $execve (param i32 i32 i32) (result i32)))
  (import "waste-libc" "chown" (func $chown (param i32 i32 i32) (result i32)))
  (import "waste-libc" "readlink" (func $readlink (param i32 i32 i32) (result i32)))
  (import "waste-libc" "opendir" (func $opendir (param i32) (result i32)))
  (import "waste-libc" "closedir" (func $closedir (param i32) (result i32)))
  (import "waste-libc" "readdir" (func $readdir (param i32) (result i32)))
  (import "waste-libc" "select" (func $select (param i32 i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "pselect" (func $pselect (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "ioctl" (func $ioctl (param i32 i32 i32) (result i32)))
  (import "waste-libc" "socket" (func $socket (param i32 i32 i32) (result i32)))
  (import "waste-libc" "connect" (func $connect (param i32 i32 i32) (result i32)))
  (import "waste-libc" "getpeername" (func $getpeername (param i32 i32 i32) (result i32)))
  (import "waste-libc" "getaddrinfo" (func $getaddrinfo (param i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "freeaddrinfo" (func $freeaddrinfo (param i32)))
  (import "waste-libc" "gai_strerror" (func $gai_strerror (param i32) (result i32)))
  (import "waste-libc" "dlopen" (func $dlopen (param i32 i32) (result i32)))
  (import "waste-libc" "dlsym" (func $dlsym (param i32 i32) (result i32)))
  (import "waste-libc" "dlclose" (func $dlclose (param i32) (result i32)))
  (import "waste-libc" "dlerror" (func $dlerror (result i32)))
  (data (i32.const 180000) "/unsupported\00symbol\00")

  ;; Helper: check errno == ENOSYS (38) after a call
  (func $is_enosys (result i32)
    call $errno_location i32.load i32.const 38 i32.eq)

  ;; --- Filesystem and process boundaries (each returns 1 if correct) ---

  (func (export "boundary-execve") (result i32)
    i32.const 180000 i32.const 0 i32.const 0 call $execve
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-chown") (result i32)
    i32.const 180000 i32.const 1 i32.const 1 call $chown
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-readlink") (result i32)
    i32.const 180000 i32.const 181000 i32.const 32 call $readlink
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-opendir") (result i32)
    i32.const 180000 call $opendir
    i32.eqz call $is_enosys i32.and)

  (func (export "boundary-closedir") (result i32)
    i32.const 0 call $closedir
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-readdir") (result i32)
    i32.const 0 call $readdir
    i32.eqz call $is_enosys i32.and)

  ;; select/pselect are readiness-reporting stubs, not ENOSYS.
  ;; select(0,0,0,0,0) returns 0 (no descriptors ready).
  (func (export "boundary-select") (result i32)
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 call $select
    i32.eqz)

  (func (export "boundary-pselect") (result i32)
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0
    call $pselect
    i32.eqz)

  (func (export "boundary-ioctl") (result i32)
    i32.const 0 i32.const 0 i32.const 0 call $ioctl
    i32.const -1 i32.eq call $is_enosys i32.and)

  ;; --- Network and loader boundaries ---

  (func (export "boundary-socket") (result i32)
    i32.const 2 i32.const 1 i32.const 0 call $socket
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-connect") (result i32)
    i32.const 0 i32.const 0 i32.const 0 call $connect
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-getpeername") (result i32)
    i32.const 0 i32.const 0 i32.const 0 call $getpeername
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-getaddrinfo") (result i32)
    i32.const 180000 i32.const 0 i32.const 0 i32.const 181000
    call $getaddrinfo i32.const -4 i32.eq
    i32.const 181000 i32.load i32.eqz i32.and)

  (func (export "boundary-freeaddrinfo") (result i32)
    i32.const 0 call $freeaddrinfo
    i32.const 1)

  (func (export "boundary-gai_strerror") (result i32)
    i32.const -4 call $gai_strerror i32.eqz i32.eqz)

  (func (export "boundary-dlopen") (result i32)
    i32.const 180000 i32.const 0 call $dlopen
    i32.eqz call $is_enosys i32.and)

  (func (export "boundary-dlsym") (result i32)
    i32.const 0 i32.const 180013 call $dlsym
    i32.eqz call $is_enosys i32.and)

  (func (export "boundary-dlclose") (result i32)
    i32.const 0 call $dlclose
    i32.const -1 i32.eq call $is_enosys i32.and)

  (func (export "boundary-dlerror") (result i32)
    call $dlerror i32.eqz i32.eqz))

;; Filesystem and process boundaries
(assert_return (invoke $boundary_tests "boundary-execve") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-chown") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-readlink") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-opendir") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-closedir") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-readdir") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-select") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-pselect") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-ioctl") (i32.const 1))
;; Network and loader boundaries
(assert_return (invoke $boundary_tests "boundary-socket") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-connect") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-getpeername") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-getaddrinfo") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-freeaddrinfo") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-gai_strerror") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-dlopen") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-dlsym") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-dlclose") (i32.const 1))
(assert_return (invoke $boundary_tests "boundary-dlerror") (i32.const 1))
