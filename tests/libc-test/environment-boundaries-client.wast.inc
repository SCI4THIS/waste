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
  (func $is_enosys (result i32)
    call $errno_location i32.load i32.const 38 i32.eq)
  (func (export "filesystem-and-process-boundaries") (result i32)
    i32.const 180000 i32.const 0 i32.const 0 call $execve i32.const -1 i32.eq call $is_enosys i32.and
    i32.const 180000 i32.const 1 i32.const 1 call $chown i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 180000 i32.const 181000 i32.const 32 call $readlink i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 180000 call $opendir i32.eqz i32.and call $is_enosys i32.and
    i32.const 0 call $closedir i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 0 call $readdir i32.eqz i32.and call $is_enosys i32.and
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 call $select i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 i32.const 0 call $pselect i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 0 i32.const 0 i32.const 0 call $ioctl i32.const -1 i32.eq i32.and call $is_enosys i32.and)

  (func (export "network-and-loader-boundaries") (result i32)
    i32.const 2 i32.const 1 i32.const 0 call $socket i32.const -1 i32.eq call $is_enosys i32.and
    i32.const 0 i32.const 0 i32.const 0 call $connect i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 0 i32.const 0 i32.const 0 call $getpeername i32.const -1 i32.eq i32.and call $is_enosys i32.and
    i32.const 180000 i32.const 0 i32.const 0 i32.const 181000 call $getaddrinfo i32.const -4 i32.eq i32.and
    i32.const 181000 i32.load i32.eqz i32.and
    i32.const 0 call $freeaddrinfo
    i32.const -4 call $gai_strerror i32.eqz i32.eqz i32.and
    i32.const 180000 i32.const 0 call $dlopen i32.eqz i32.and call $is_enosys i32.and
    i32.const 0 i32.const 180013 call $dlsym i32.eqz i32.and call $is_enosys i32.and
    i32.const 0 call $dlclose i32.const -1 i32.eq i32.and call $is_enosys i32.and
    call $dlerror i32.eqz i32.eqz i32.and))
(assert_return (invoke $boundary_tests "filesystem-and-process-boundaries") (i32.const 1))
(assert_return (invoke $boundary_tests "network-and-loader-boundaries") (i32.const 1))
