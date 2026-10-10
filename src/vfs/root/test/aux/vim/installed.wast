;; Run from Bash with /bin/wast --verbose; verifies the installed vim binary.
(module $vim_installed
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "access" (func $access (param i32 i32) (result i32)))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  ;; "/usr/bin/vim\0" at offset 180000.
  (data (i32.const 180000) "/usr/bin/vim\00")
  (data (i32.const 180032) "/tmp/vim-package-version.txt\00")
  (data (i32.const 180064) "/tmp/vim-package.txt\00")
  (data (i32.const 180096) "VIM_PACKAGE_SAVED\0a")
  (data (i32.const 180128) "VIM")
  (func (export "exists") (result i32)
    (i32.eqz (call $access (i32.const 180000) (i32.const 0))))
  (func (export "executable") (result i32)
    (i32.eqz (call $access (i32.const 180000) (i32.const 1))))
  (func (export "version-ran") (result i32)
    (local $fd i32) (local $ok i32)
    i32.const 180032 i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181000 i32.const 3 call $read
    i32.const 3 i32.eq
    i32.const 181000 i32.const 180128 i32.const 3 call $compare i32.eqz
    i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok)
  (func (export "edited-and-saved") (result i32)
    (local $fd i32) (local $ok i32)
    i32.const 180064 i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 181000 i32.const 64 call $read
    i32.const 18 i32.eq
    i32.const 181000 i32.const 180096 i32.const 18 call $compare i32.eqz
    i32.and local.set $ok
    local.get $fd call $close drop
    local.get $ok))

(assert_return (invoke $vim_installed "exists") (i32.const 1))
(assert_return (invoke $vim_installed "executable") (i32.const 1))
(assert_return (invoke $vim_installed "version-ran") (i32.const 1))
(assert_return (invoke $vim_installed "edited-and-saved") (i32.const 1))
