;; Foundational kernel integration tests. These deliberately use the same env
;; ABI as src/bash.wat and run under --schedule so blocking syscalls and fork
;; are serviced by the cooperative process scheduler.
(module
  (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "close" (func $close (param i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "lseek" (func $lseek (param i32 i64 i32) (result i64)))
  (import "env" "pipe" (func $pipe (param i32) (result i32)))
  (import "env" "dup" (func $dup (param i32) (result i32)))
  (import "env" "fork" (func $fork (result i32)))
  (import "env" "waitpid" (func $waitpid (param i32 i32 i32) (result i32)))
  (import "env" "exit" (func $exit (param i32)))
  (import "env" "getpid" (func $getpid (result i32)))
  (import "env" "getppid" (func $getppid (result i32)))
  (import "env" "getpgrp" (func $getpgrp (result i32)))
  (import "env" "raise" (func $raise (param i32) (result i32)))
  (import "env" "kill" (func $kill (param i32 i32) (result i32)))
  (import "env" "killpg" (func $killpg (param i32 i32) (result i32)))
  (import "env" "sleep" (func $sleep (param i32) (result i32)))
  (import "env" "setpgid" (func $setpgid (param i32 i32) (result i32)))
  (import "env" "tcgetpgrp" (func $tcgetpgrp (param i32) (result i32)))
  (import "env" "tcsetpgrp" (func $tcsetpgrp (param i32 i32) (result i32)))
  (memory 1)
  (data (i32.const 100) "/tmp/kernel-test\00")
  (data (i32.const 128) "hello")

  (func (export "identity") (result i32)
    call $getpid
    i32.const 100
    i32.mul
    call $getppid
    i32.add)

  (func (export "terminal-group") (result i32)
    call $getpgrp
    i32.const 0
    call $tcgetpgrp
    i32.eq)

  (func (export "vfs") (result i32) (local $fd i32)
    i32.const 100
    i32.const 578 ;; O_CREAT | O_TRUNC | O_RDWR
    i32.const 420 ;; 0644
    call $open
    local.set $fd
    local.get $fd
    i32.const 128
    i32.const 5
    call $write
    drop
    local.get $fd
    i64.const 0
    i32.const 0
    call $lseek
    drop
    local.get $fd
    i32.const 160
    i32.const 5
    call $read
    drop
    local.get $fd
    call $close
    drop
    i32.const 160
    i32.load)

  (func (export "pipe-and-dup") (result i32) (local $copy i32)
    i32.const 32
    call $pipe
    drop
    i32.const 32
    i32.load
    call $dup
    local.set $copy
    i32.const 32
    i32.load offset=4
    i32.const 128
    i32.const 5
    call $write
    drop
    local.get $copy
    i32.const 176
    i32.const 5
    call $read
    drop
    i32.const 176
    i32.load)

  (func (export "fork-wait-isolation") (result i32) (local $child i32)
    i32.const 0
    i32.const 11
    i32.store
    call $fork
    local.tee $child
    i32.eqz
    if
      i32.const 0
      i32.const 22
      i32.store
      i32.const 7
      call $exit
      unreachable
    end
    local.get $child
    i32.const 8
    i32.const 0
    call $waitpid
    drop
    i32.const 0
    i32.load
    i32.const 8
    i32.load
    i32.add)

  (func (export "stop-continue-wait") (result i32) (local $child i32) (local $stopped i32)
    call $fork
    local.tee $child
    i32.eqz
    if
      i32.const 19 ;; SIGSTOP
      call $raise
      drop
      i32.const 3
      call $exit
      unreachable
    end
    local.get $child
    i32.const 12
    i32.const 2 ;; WUNTRACED
    call $waitpid
    drop
    i32.const 12
    i32.load
    local.set $stopped
    local.get $child
    i32.const 18 ;; SIGCONT
    call $kill
    drop
    local.get $child
    i32.const 12
    i32.const 0
    call $waitpid
    drop
    local.get $stopped
    i32.const 12
    i32.load
    i32.add)

  (func (export "foreground-job") (result i32) (local $child i32)
    call $fork
    local.tee $child
    i32.eqz
    if
      i32.const 30
      call $sleep
      drop
      i32.const 0
      call $exit
      unreachable
    end
    local.get $child
    local.get $child
    call $setpgid
    drop
    i32.const 0
    local.get $child
    call $tcsetpgrp
    drop
    local.get $child
    i32.const 15 ;; SIGTERM
    call $killpg
    drop
    local.get $child
    i32.const 16
    i32.const 0
    call $waitpid
    drop
    i32.const 0
    i32.const 1
    call $tcsetpgrp
    drop
    i32.const 16
    i32.load))

(assert_return (invoke "identity") (i32.const 100))
(assert_return (invoke "terminal-group") (i32.const 1))
(assert_return (invoke "vfs") (i32.const 1819043176))
(assert_return (invoke "pipe-and-dup") (i32.const 1819043176))
(assert_return (invoke "fork-wait-isolation") (i32.const 1803))
(assert_return (invoke "stop-continue-wait") (i32.const 5759))
(assert_return (invoke "foreground-job") (i32.const 15))
