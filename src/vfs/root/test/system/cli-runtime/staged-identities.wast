;; Stage this same file at /tmp/staged-first and /tmp/staged-second before Bash.
;; Equal bytes must not cause distinct staged paths to share an inode identity.
(module $identities
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "stat" (func $stat (param i32 i32) (result i32)))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "fstat" (func $fstat (param i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (data (i32.const 180000) "/tmp/staged-first\00")
  (data (i32.const 180064) "/tmp/staged-second\00")
  (data (i32.const 180128) "/tmp/staged-created\00")
  (data (i32.const 180192) "/usr/bin/bash\00")
  (global $ready (mut i32) (i32.const 0))
  ;; The public 128-byte stat ABI stores device/inode at offsets 0/8.
  (func $same (param $a i32) (param $b i32) (result i32)
    local.get $a i64.load local.get $b i64.load i64.eq
    local.get $a i64.load offset=8 local.get $b i64.load offset=8 i64.eq i32.and)
  (func (export "staged-stat") (result i32)
    i32.const 180000 i32.const 181000 call $stat i32.eqz
    i32.const 180064 i32.const 181128 call $stat i32.eqz
    i32.and global.set $ready
    global.get $ready)
  (func (export "nonzero-identities") (result i32)
    global.get $ready
    i32.const 181008 i64.load i64.const 0 i64.ne i32.and
    i32.const 181136 i64.load i64.const 0 i64.ne i32.and)
  (func (export "distinct-staged-identities") (result i32)
    global.get $ready
    i32.const 181000 i32.const 181128 call $same i32.eqz i32.and)
  (func (export "descriptor-identity-stable") (result i32)
    (local $fd i32) (local $ok i32)
    global.get $ready i32.eqz if i32.const 0 return end
    i32.const 180000 i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181256 call $fstat i32.eqz
    i32.const 181000 i32.const 181256 call $same i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok)
  (func (export "guest-created-identity-distinct") (result i32)
    (local $fd i32) (local $ok i32)
    global.get $ready i32.eqz if i32.const 0 return end
    ;; O_WRONLY | O_CREAT | O_TRUNC, mode 0644.
    i32.const 180128 i32.const 577 i32.const 420 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181256 call $fstat i32.eqz
    i32.const 181264 i64.load i64.const 0 i64.ne i32.and
    i32.const 181000 i32.const 181256 call $same i32.eqz i32.and
    i32.const 181128 i32.const 181256 call $same i32.eqz i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok)
  (func (export "mounted-identity-distinct") (result i32)
    global.get $ready i32.eqz if i32.const 0 return end
    i32.const 180192 i32.const 181256 call $stat i32.eqz
    i32.const 181264 i64.load i64.const 0 i64.ne i32.and
    i32.const 181000 i32.const 181256 call $same i32.eqz i32.and
    i32.const 181128 i32.const 181256 call $same i32.eqz i32.and))
(assert_return (invoke $identities "staged-stat") (i32.const 1))
(assert_return (invoke $identities "nonzero-identities") (i32.const 1))
(assert_return (invoke $identities "distinct-staged-identities") (i32.const 1))
(assert_return (invoke $identities "descriptor-identity-stable") (i32.const 1))
(assert_return (invoke $identities "guest-created-identity-distinct") (i32.const 1))
(assert_return (invoke $identities "mounted-identity-distinct") (i32.const 1))
