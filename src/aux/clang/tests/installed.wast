;; Run after setup.sh, inside Bash with /bin/wast --verbose.
;; Check actual command output and rejection of invalid input.
(module $clang
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/clang-version.txt\00")
  (data (i32.const 180256) "/tmp/clang-invalid-status.txt\00")
  (data (i32.const 180320) "clang version")
  (data (i32.const 180400) "1\0a")
  (data (i32.const 180416) "/tmp/clang-invalid-output.txt\00")
  (data (i32.const 180464) "error:")
  (data (i32.const 180512) "/tmp/clang-compile-status.txt\00")
  (data (i32.const 180576) "/tmp/clang-run-status.txt\00")
  (data (i32.const 180640) "/tmp/clang-run-output.txt\00")
  (data (i32.const 180704) "0\0a")
  (data (i32.const 180720) "37\0a")
  (data (i32.const 180736) "guest-clang:11\0aSat Jan  1 00:00:00 2000|Jan  1 2000|00:00:00\0a")
  (data (i32.const 180800) "/tmp/clang-parent.txt\00")
  (data (i32.const 180864) "parent-alive\0a")
  (func $matches (param $path i32) (param $bytes i32) (param $length i32)
                (param $exact i32) (result i32)
    (local $fd i32) (local $got i32) (local $ok i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181000
    local.get $length local.get $exact i32.add call $read local.set $got
    local.get $got local.get $length i32.eq
    i32.const 181000 local.get $bytes local.get $length call $memcmp i32.eqz
    i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok)
  (func (export "version") (result i32)
    i32.const 180000 i32.const 180320 i32.const 13 i32.const 0 call $matches)
  (func (export "invalid-c-rejected") (result i32)
    i32.const 180256 i32.const 180400 i32.const 2 i32.const 1 call $matches)
  (func (export "invalid-c-diagnostic") (result i32)
    (local $fd i32) (local $length i32) (local $offset i32)
    i32.const 180416 i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181000 i32.const 4096 call $read local.set $length
    local.get $fd call $close drop
    block $end
      loop $scan
        local.get $offset i32.const 6 i32.add local.get $length i32.gt_s br_if $end
        i32.const 181000 local.get $offset i32.add
        i32.const 180464 i32.const 6 call $memcmp i32.eqz
        if i32.const 1 return end
        local.get $offset i32.const 1 i32.add local.set $offset
        br $scan
      end
    end
    i32.const 0)
  (func (export "compile-and-link") (result i32)
    i32.const 180512 i32.const 180704 i32.const 2 i32.const 1 call $matches)
  (func (export "compiled-exit") (result i32)
    i32.const 180576 i32.const 180720 i32.const 3 i32.const 1 call $matches)
  (func (export "compiled-output") (result i32)
    i32.const 180640 i32.const 180736 i32.const 61 i32.const 1 call $matches)
  (func (export "parent-survived") (result i32)
    i32.const 180800 i32.const 180864 i32.const 13 i32.const 1 call $matches))
(assert_return (invoke $clang "version") (i32.const 1))
(assert_return (invoke $clang "invalid-c-rejected") (i32.const 1))
(assert_return (invoke $clang "invalid-c-diagnostic") (i32.const 1))
(assert_return (invoke $clang "compile-and-link") (i32.const 1))
(assert_return (invoke $clang "compiled-exit") (i32.const 1))
(assert_return (invoke $clang "compiled-output") (i32.const 1))
(assert_return (invoke $clang "parent-survived") (i32.const 1))
