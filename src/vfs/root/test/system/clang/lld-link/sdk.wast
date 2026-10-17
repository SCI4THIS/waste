;; Guest LLD links public SDK calls directly to the shared provider.
(module $sdk
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (func $matches (param $path i32) (param $expected i32) (param $length i32)
                 (param $exact i32) (result i32)
    (local $fd i32) (local $got i32) (local $at i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 190000 i32.const 8192 call $read local.set $got
    local.get $fd call $close drop
    local.get $got local.get $length i32.lt_s if i32.const 0 return end
    local.get $exact if
      local.get $got local.get $length i32.ne if i32.const 0 return end
    end
    block $missing
      loop $scan
        local.get $at local.get $length i32.add local.get $got i32.gt_u br_if $missing
        i32.const 190000 local.get $at i32.add local.get $expected local.get $length
        call $memcmp i32.eqz if i32.const 1 return end
        local.get $exact br_if $missing
        local.get $at i32.const 1 i32.add local.set $at br $scan
      end
    end i32.const 0)
  (data (i32.const 180000) "\2f\74\6d\70\2f\73\64\6b\2d\6c\69\6e\6b\2d\73\74\61\74\75\73\2e\74\78\74\00")
  (data (i32.const 180128) "\30\0a")
  (func (export "link") (result i32) i32.const 180000 i32.const 180128 i32.const 2 i32.const 1 call $matches)
  (data (i32.const 180256) "\2f\74\6d\70\2f\73\64\6b\2d\63\68\6d\6f\64\2d\73\74\61\74\75\73\2e\74\78\74\00")
  (data (i32.const 180384) "\30\0a")
  (func (export "chmod") (result i32) i32.const 180256 i32.const 180384 i32.const 2 i32.const 1 call $matches)
  (data (i32.const 180512) "\2f\74\6d\70\2f\73\64\6b\2d\72\75\6e\2d\73\74\61\74\75\73\2e\74\78\74\00")
  (data (i32.const 180640) "\33\37\0a")
  (func (export "run") (result i32) i32.const 180512 i32.const 180640 i32.const 3 i32.const 1 call $matches)
  (data (i32.const 180768) "\2f\74\6d\70\2f\73\64\6b\2d\6f\75\74\70\75\74\2e\74\78\74\00")
  (data (i32.const 180896) "\67\75\65\73\74\2d\6c\69\62\63\3a\20\68\65\6c\6c\6f\20\34\0a\6b\65\72\6e\65\6c\2d\77\72\69\74\65\0a")
  (func (export "output") (result i32) i32.const 180768 i32.const 180896 i32.const 33 i32.const 1 call $matches)
  (data (i32.const 181024) "\2f\74\6d\70\2f\73\64\6b\2e\77\61\73\6d\00")
  (data (i32.const 181152) "\04\6c\69\62\63\06\70\72\69\6e\74\66\00")
  (func (export "libc-import") (result i32) i32.const 181024 i32.const 181152 i32.const 13 i32.const 0 call $matches)
  (data (i32.const 181280) "\2f\74\6d\70\2f\73\64\6b\2e\77\61\73\6d\00")
  (data (i32.const 181408) "\03\65\6e\76\05\77\72\69\74\65\00")
  (func (export "kernel-import") (result i32) i32.const 181280 i32.const 181408 i32.const 11 i32.const 0 call $matches)
  (data (i32.const 181536) "\2f\74\6d\70\2f\62\61\64\2d\73\69\67\6e\61\74\75\72\65\2d\73\74\61\74\75\73\2e\74\78\74\00")
  (data (i32.const 181664) "\31\0a")
  (func (export "bad-status") (result i32) i32.const 181536 i32.const 181664 i32.const 2 i32.const 1 call $matches)
  (data (i32.const 181792) "\2f\74\6d\70\2f\62\61\64\2d\73\69\67\6e\61\74\75\72\65\2d\65\72\72\6f\72\2e\74\78\74\00")
  (data (i32.const 181920) "\66\75\6e\63\74\69\6f\6e\20\73\69\67\6e\61\74\75\72\65\20\6d\69\73\6d\61\74\63\68")
  (func (export "bad-diagnostic") (result i32) i32.const 181792 i32.const 181920 i32.const 27 i32.const 0 call $matches)
  (data (i32.const 183000) "/tmp/bad-signature.wasm\00")
  (func (export "bad-output-absent") (result i32)
    (local $fd i32)
    i32.const 183000 i32.const 0 i32.const 0 call $open local.tee $fd i32.const -1 i32.eq
    if (result i32) i32.const 1 else local.get $fd call $close drop i32.const 0 end))
(assert_return (invoke $sdk "link") (i32.const 1))
(assert_return (invoke $sdk "chmod") (i32.const 1))
(assert_return (invoke $sdk "run") (i32.const 1))
(assert_return (invoke $sdk "output") (i32.const 1))
(assert_return (invoke $sdk "libc-import") (i32.const 1))
(assert_return (invoke $sdk "kernel-import") (i32.const 1))
(assert_return (invoke $sdk "bad-status") (i32.const 1))
(assert_return (invoke $sdk "bad-diagnostic") (i32.const 1))
(assert_return (invoke $sdk "bad-output-absent") (i32.const 1))
