;; Real installed swprintf/vswprintf providers. Argument areas follow the
;; wasm32 compiler ABI (4/8/16-byte alignment); compiler observers test lowering.
(module $wide-format
  (import "waste-runtime" "memory" (memory 8))
  (import "libc" "vswprintf" (func $format (param i32 i32 i32 i32) (result i32)))
  (import "libc" "swprintf" (func $direct (param i32 i32 i32 i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (import "libc" "memcpy" (func $copy (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (import "libc" "wcslen" (func $length (param i32) (result i32)))
  (func $check (param $format i32) (param $args i32) (param $expected i32)
               (param $written i32) (param $capacity i32) (param $result i32)
               (param $error i32) (param $null i32) (param $direct i32) (result i32)
    (local $at i32) (local $bytes i32) (local $returned i32)
    i32.const 301008 local.get $args i32.const 128 call $copy drop
    i32.const 420000 local.get $format local.get $format call $length i32.const 1 i32.add i32.const 4 i32.mul local.tee $bytes call $copy drop
    i32.const 300800 i32.const 0xa5 i32.const 96 memory.fill
    i32.const 319996 i32.const 0xa5 local.get $capacity i32.const 4 i32.mul i32.const 12 i32.add memory.fill
    call $errno i32.const 17 i32.store
    local.get $null if (result i32) i32.const 0 else i32.const 320000 end
    local.get $capacity local.get $format i32.const 301008
    local.get $direct if (param i32 i32 i32 i32) (result i32) call $direct else call $format end local.set $returned
    local.get $returned local.get $result i32.ne if i32.const 0 return end
    call $errno i32.load local.get $error i32.ne if i32.const 0 return end
    i32.const 301008 local.get $args i32.const 128 call $compare if i32.const 0 return end
    i32.const 420000 local.get $format local.get $bytes call $compare if i32.const 0 return end
    local.get $capacity
    if
      i32.const 320000 local.get $expected local.get $written i32.const 1 i32.add i32.const 4 i32.mul call $compare
      if i32.const 0 return end
    end
    i32.const 319996 i32.load i32.const 0xa5a5a5a5 i32.ne if i32.const 0 return end
    local.get $capacity if (result i32) local.get $written i32.const 1 i32.add else i32.const 0 end local.set $at
    block $done loop $canaries
      local.get $at local.get $capacity i32.const 2 i32.add i32.ge_u br_if $done
      i32.const 320000 local.get $at i32.const 4 i32.mul i32.add i32.load i32.const 0xa5a5a5a5 i32.ne
      if i32.const 0 return end
      local.get $at i32.const 1 i32.add local.set $at br $canaries
    end end
    i32.const 1)

  (data (i32.const 280000) "\e9\00\00\00\00\f6\01\00\58\00\00\00\59\00\00\00\5a\00\00\00\00\00\00\00")
  (data (i32.const 280064) "\00\00\00\80\cd\ab\34\12\00\00\00\00")
  (data (i32.const 280256) "\c3\a9\f0\9f\98\80\58\59\5a\00")
  (data (i32.const 280320) "\80\00")
  (data (i32.const 280352) "\e2\82\00")
  (data (i32.const 280384) "\c0\80\00")
  (data (i32.const 280416) "\ed\a0\80\00")
  (data (i32.const 280448) "\f4\90\80\80\00")
  (data (i32.const 185000) "\41\00\00\00\e9\00\00\00\00\f6\01\00\7c\00\00\00\25\00\00\00\25\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\41\00\00\00\e9\00\00\00\00\f6\01\00\7c\00\00\00\25\00\00\00\00\00\00\00")
  (func (export "literal-wide") (result i32)
    i32.const 185000 i32.const 185040 i32.const 185168 i32.const 5
    i32.const 256 i32.const 5 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 185216) "\25\00\00\00\68\00\00\00\68\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\68\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\6c\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\6c\00\00\00\6c\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\6a\00\00\00\75\00\00\00\7c\00\00\00\25\00\00\00\7a\00\00\00\75\00\00\00\7c\00\00\00\25\00\00\00\74\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\58\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\00\00\00\ff\ff\00\00\00\00\00\80\5a\5a\5a\5a\00\00\00\00\00\00\00\80\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\fb\ff\ff\ff\cd\ab\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\31\00\00\00\7c\00\00\00\2d\00\00\00\31\00\00\00\7c\00\00\00\2d\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\38\00\00\00\7c\00\00\00\2d\00\00\00\39\00\00\00\32\00\00\00\32\00\00\00\33\00\00\00\33\00\00\00\37\00\00\00\32\00\00\00\30\00\00\00\33\00\00\00\36\00\00\00\38\00\00\00\35\00\00\00\34\00\00\00\37\00\00\00\37\00\00\00\35\00\00\00\38\00\00\00\30\00\00\00\38\00\00\00\7c\00\00\00\31\00\00\00\38\00\00\00\34\00\00\00\34\00\00\00\36\00\00\00\37\00\00\00\34\00\00\00\34\00\00\00\30\00\00\00\37\00\00\00\33\00\00\00\37\00\00\00\30\00\00\00\39\00\00\00\35\00\00\00\35\00\00\00\31\00\00\00\36\00\00\00\31\00\00\00\35\00\00\00\7c\00\00\00\34\00\00\00\32\00\00\00\39\00\00\00\34\00\00\00\39\00\00\00\36\00\00\00\37\00\00\00\32\00\00\00\39\00\00\00\35\00\00\00\7c\00\00\00\2d\00\00\00\35\00\00\00\7c\00\00\00\41\00\00\00\42\00\00\00\43\00\00\00\44\00\00\00\00\00\00\00")
  (func (export "mixed-promotions") (result i32)
    i32.const 185216 i32.const 185360 i32.const 185488 i32.const 78
    i32.const 256 i32.const 78 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 185824) "\25\00\00\00\2b\00\00\00\30\00\00\00\38\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\20\00\00\00\36\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\2d\00\00\00\36\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\2e\00\00\00\35\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2a\00\00\00\2a\00\00\00\d6\ff\ff\ff\d6\ff\ff\ff\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2b\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\34\00\00\00\32\00\00\00\7c\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\34\00\00\00\32\00\00\00\7c\00\00\00\2d\00\00\00\34\00\00\00\32\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\7c\00\00\00\2d\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\34\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "signed-flags") (result i32)
    i32.const 185824 i32.const 185920 i32.const 186048 i32.const 29
    i32.const 256 i32.const 29 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 186176) "\25\00\00\00\23\00\00\00\30\00\00\00\38\00\00\00\78\00\00\00\7c\00\00\00\25\00\00\00\23\00\00\00\58\00\00\00\7c\00\00\00\25\00\00\00\23\00\00\00\2e\00\00\00\30\00\00\00\6f\00\00\00\7c\00\00\00\25\00\00\00\2e\00\00\00\30\00\00\00\75\00\00\00\7c\00\00\00\25\00\00\00\23\00\00\00\6f\00\00\00\7c\00\00\00\25\00\00\00\70\00\00\00\00\00\00\00\2a\00\00\00\2a\00\00\00\00\00\00\00\00\00\00\00\08\00\00\00\34\12\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\32\00\00\00\61\00\00\00\7c\00\00\00\30\00\00\00\58\00\00\00\32\00\00\00\41\00\00\00\7c\00\00\00\30\00\00\00\7c\00\00\00\7c\00\00\00\30\00\00\00\31\00\00\00\30\00\00\00\7c\00\00\00\30\00\00\00\78\00\00\00\31\00\00\00\32\00\00\00\33\00\00\00\34\00\00\00\00\00\00\00")
  (func (export "integer-alternate") (result i32)
    i32.const 186176 i32.const 186288 i32.const 186416 i32.const 27
    i32.const 256 i32.const 27 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 186528) "\25\00\00\00\2a\00\00\00\2e\00\00\00\2a\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f8\ff\ff\ff\05\00\00\00\2a\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\30\00\00\00\30\00\00\00\34\00\00\00\32\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\00\00\00\00")
  (func (export "negative-star-width") (result i32)
    i32.const 186528 i32.const 186560 i32.const 186688 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 186752) "\25\00\00\00\2e\00\00\00\2a\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\ff\2a\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\34\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "negative-star-precision") (result i32)
    i32.const 186752 i32.const 186784 i32.const 186912 i32.const 2
    i32.const 256 i32.const 2 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 186944) "\5b\00\00\00\25\00\00\00\36\00\00\00\2e\00\00\00\32\00\00\00\6c\00\00\00\73\00\00\00\5d\00\00\00\7c\00\00\00\5b\00\00\00\25\00\00\00\6c\00\00\00\63\00\00\00\5d\00\00\00\00\00\00\00\00\00\00\00\c0\45\04\00\00\f6\01\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5b\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\e9\00\00\00\00\f6\01\00\5d\00\00\00\7c\00\00\00\5b\00\00\00\00\f6\01\00\5d\00\00\00\00\00\00\00")
  (func (export "wide-strings") (result i32)
    i32.const 186944 i32.const 187008 i32.const 187136 i32.const 12
    i32.const 256 i32.const 12 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 187200) "\25\00\00\00\6c\00\00\00\73\00\00\00\00\00\00\00\00\46\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\80\cd\ab\34\12\00\00\00\00")
  (func (export "wide-signed-elements") (result i32)
    i32.const 187200 i32.const 187216 i32.const 187344 i32.const 2
    i32.const 256 i32.const 2 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 187360) "\5b\00\00\00\25\00\00\00\35\00\00\00\2e\00\00\00\32\00\00\00\73\00\00\00\5d\00\00\00\7c\00\00\00\25\00\00\00\63\00\00\00\00\00\00\00\00\00\00\00\c0\46\04\00\41\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5b\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\e9\00\00\00\00\f6\01\00\5d\00\00\00\7c\00\00\00\41\00\00\00\00\00\00\00")
  (func (export "utf8-strings") (result i32)
    i32.const 187360 i32.const 187408 i32.const 187536 i32.const 9
    i32.const 256 i32.const 9 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 187584) "\25\00\00\00\2e\00\00\00\30\00\00\00\73\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\47\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "utf8-zero-precision") (result i32)
    i32.const 187584 i32.const 187616 i32.const 187744 i32.const 0
    i32.const 256 i32.const 0 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 187776) "\25\00\00\00\73\00\00\00\00\00\00\00\00\00\00\00\00\47\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "invalid-utf8") (result i32)
    i32.const 187776 i32.const 187792 i32.const 187920 i32.const 0
    i32.const 256 i32.const -1 i32.const 84 i32.const 0 i32.const 0 call $check)
  (data (i32.const 187936) "\25\00\00\00\73\00\00\00\00\00\00\00\00\00\00\00\20\47\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "truncated-utf8") (result i32)
    i32.const 187936 i32.const 187952 i32.const 188080 i32.const 0
    i32.const 256 i32.const -1 i32.const 84 i32.const 0 i32.const 0 call $check)
  (data (i32.const 188096) "\25\00\00\00\73\00\00\00\00\00\00\00\00\00\00\00\40\47\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "utf8-overlong") (result i32)
    i32.const 188096 i32.const 188112 i32.const 188240 i32.const 0
    i32.const 256 i32.const -1 i32.const 84 i32.const 0 i32.const 0 call $check)
  (data (i32.const 188256) "\25\00\00\00\73\00\00\00\00\00\00\00\00\00\00\00\60\47\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "utf8-surrogate") (result i32)
    i32.const 188256 i32.const 188272 i32.const 188400 i32.const 0
    i32.const 256 i32.const -1 i32.const 84 i32.const 0 i32.const 0 call $check)
  (data (i32.const 188416) "\25\00\00\00\73\00\00\00\00\00\00\00\00\00\00\00\80\47\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "utf8-out-of-range") (result i32)
    i32.const 188416 i32.const 188432 i32.const 188560 i32.const 0
    i32.const 256 i32.const -1 i32.const 84 i32.const 0 i32.const 0 call $check)
  (data (i32.const 188576) "\61\00\00\00\62\00\00\00\63\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\61\00\00\00\62\00\00\00\63\00\00\00\64\00\00\00\00\00\00\00")
  (func (export "capacity-5") (result i32)
    i32.const 188576 i32.const 188608 i32.const 188736 i32.const 4
    i32.const 5 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 188768) "\61\00\00\00\62\00\00\00\63\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\61\00\00\00\62\00\00\00\63\00\00\00\00\00\00\00")
  (func (export "capacity-4") (result i32)
    i32.const 188768 i32.const 188800 i32.const 188928 i32.const 3
    i32.const 4 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 188960) "\61\00\00\00\62\00\00\00\63\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "capacity-1") (result i32)
    i32.const 188960 i32.const 188992 i32.const 189120 i32.const 0
    i32.const 1 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 189152) "\61\00\00\00\62\00\00\00\63\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "capacity-0") (result i32)
    i32.const 189152 i32.const 189184 i32.const 189312 i32.const 0
    i32.const 0 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 189344) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "empty") (result i32)
    i32.const 189344 i32.const 189360 i32.const 189488 i32.const 0
    i32.const 1 i32.const 0 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 189504) "\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "empty-zero-capacity") (result i32)
    i32.const 189504 i32.const 189520 i32.const 189648 i32.const 0
    i32.const 0 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 189664) "\78\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "null-zero-capacity") (result i32)
    i32.const 189664 i32.const 189680 i32.const 189808 i32.const 0
    i32.const 0 i32.const -1 i32.const 75 i32.const 1 i32.const 0 call $check)
  (data (i32.const 189824) "\61\00\00\00\62\00\00\00\63\00\00\00\25\00\00\00\51\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\61\00\00\00\62\00\00\00\63\00\00\00\00\00\00\00")
  (func (export "bad-format") (result i32)
    i32.const 189824 i32.const 189856 i32.const 189984 i32.const 3
    i32.const 256 i32.const -1 i32.const 22 i32.const 0 i32.const 0 call $check)
  (data (i32.const 190016) "\61\00\00\00\62\00\00\00\63\00\00\00\25\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\61\00\00\00\62\00\00\00\63\00\00\00\00\00\00\00")
  (func (export "dangling-percent") (result i32)
    i32.const 190016 i32.const 190048 i32.const 190176 i32.const 3
    i32.const 256 i32.const -1 i32.const 22 i32.const 0 i32.const 0 call $check)
  (data (i32.const 190208) "\25\00\00\00\31\00\00\00\24\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2a\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "positional-rejected") (result i32)
    i32.const 190208 i32.const 190240 i32.const 190368 i32.const 0
    i32.const 256 i32.const -1 i32.const 22 i32.const 0 i32.const 0 call $check)
  (data (i32.const 190400) "\25\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\38\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2a\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "field-overflow") (result i32)
    i32.const 190400 i32.const 190464 i32.const 190592 i32.const 0
    i32.const 256 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 190624) "\25\00\00\00\2e\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\38\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "precision-overflow") (result i32)
    i32.const 190624 i32.const 190688 i32.const 190816 i32.const 0
    i32.const 256 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 190848) "\25\00\00\00\2a\00\00\00\64\00\00\00\00\00\00\00\00\00\00\80\2a\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\00\00\00\00")
  (func (export "star-width-overflow") (result i32)
    i32.const 190848 i32.const 190864 i32.const 190992 i32.const 0
    i32.const 256 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 191008) "\25\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\37\00\00\00\64\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2a\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\00\00\00\00")
  (func (export "huge-width-bounded") (result i32)
    i32.const 191008 i32.const 191072 i32.const 191200 i32.const 15
    i32.const 16 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 191264) "\25\00\00\00\2e\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\37\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "huge-precision-bounded") (result i32)
    i32.const 191264 i32.const 191328 i32.const 191456 i32.const 15
    i32.const 16 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (data (i32.const 191520) "\25\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f4\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "default-fixed") (result i32)
    i32.const 191520 i32.const 191536 i32.const 191664 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (func (export "direct-swprintf") (result i32) i32.const 191520 i32.const 191536 i32.const 191664 i32.const 8 i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 1 call $check)
  (data (i32.const 191712) "\25\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\80\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\30\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "default-negative-zero") (result i32)
    i32.const 191712 i32.const 191728 i32.const 191856 i32.const 9
    i32.const 256 i32.const 9 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 191904) "\25\00\00\00\2e\00\00\00\30\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\32\00\00\00\00\00\00\00")
  (func (export "half-even") (result i32)
    i32.const 191904 i32.const 191936 i32.const 192064 i32.const 1
    i32.const 256 i32.const 1 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 192096) "\25\00\00\00\2e\00\00\00\30\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\0c\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\34\00\00\00\00\00\00\00")
  (func (export "half-odd") (result i32)
    i32.const 192096 i32.const 192128 i32.const 192256 i32.const 1
    i32.const 256 i32.const 1 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 192288) "\25\00\00\00\2e\00\00\00\30\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\c0\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "half-negative") (result i32)
    i32.const 192288 i32.const 192320 i32.const 192448 i32.const 2
    i32.const 256 i32.const 2 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 192480) "\25\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\c0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\31\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "fraction-tie-even") (result i32)
    i32.const 192480 i32.const 192512 i32.const 192640 i32.const 4
    i32.const 256 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 192672) "\25\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\d8\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\33\00\00\00\38\00\00\00\00\00\00\00")
  (func (export "fraction-tie-odd") (result i32)
    i32.const 192672 i32.const 192704 i32.const 192832 i32.const 4
    i32.const 256 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 192864) "\25\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\ff\ff\ff\bf\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\31\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "fraction-below") (result i32)
    i32.const 192864 i32.const 192896 i32.const 193024 i32.const 4
    i32.const 256 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 193056) "\25\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\c0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\31\00\00\00\33\00\00\00\00\00\00\00")
  (func (export "fraction-above") (result i32)
    i32.const 193056 i32.const 193088 i32.const 193216 i32.const 4
    i32.const 256 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 193248) "\25\00\00\00\2e\00\00\00\32\00\00\00\30\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\9a\99\99\99\99\99\b9\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\31\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\35\00\00\00\35\00\00\00\35\00\00\00\00\00\00\00")
  (func (export "decimal-exact") (result i32)
    i32.const 193248 i32.const 193280 i32.const 193408 i32.const 22
    i32.const 256 i32.const 22 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 193504) "\25\00\00\00\2e\00\00\00\31\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\23\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\30\00\00\00\2e\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "fixed-carry") (result i32)
    i32.const 193504 i32.const 193536 i32.const 193664 i32.const 4
    i32.const 256 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 193696) "\25\00\00\00\2e\00\00\00\33\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\60\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "fixed-zero-leading") (result i32)
    i32.const 193696 i32.const 193728 i32.const 193856 i32.const 5
    i32.const 256 i32.const 5 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 193888) "\25\00\00\00\2b\00\00\00\30\00\00\00\31\00\00\00\32\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f4\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2b\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\00\00\00\00")
  (func (export "decimal-flags") (result i32)
    i32.const 193888 i32.const 193936 i32.const 194064 i32.const 12
    i32.const 256 i32.const 12 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 194144) "\25\00\00\00\2d\00\00\00\31\00\00\00\32\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\f4\bf\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\00\00\00\00")
  (func (export "decimal-left") (result i32)
    i32.const 194144 i32.const 194176 i32.const 194304 i32.const 12
    i32.const 256 i32.const 12 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 194368) "\25\00\00\00\23\00\00\00\2e\00\00\00\30\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\32\00\00\00\2e\00\00\00\00\00\00\00")
  (func (export "decimal-alternate") (result i32)
    i32.const 194368 i32.const 194400 i32.const 194528 i32.const 2
    i32.const 256 i32.const 2 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 194560) "\25\00\00\00\2e\00\00\00\32\00\00\00\65\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\40\5f\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\65\00\00\00\2b\00\00\00\30\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "scientific") (result i32)
    i32.const 194560 i32.const 194592 i32.const 194720 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 194784) "\25\00\00\00\2e\00\00\00\31\00\00\00\45\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\23\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\30\00\00\00\45\00\00\00\2b\00\00\00\30\00\00\00\31\00\00\00\00\00\00\00")
  (func (export "scientific-carry") (result i32)
    i32.const 194784 i32.const 194816 i32.const 194944 i32.const 7
    i32.const 256 i32.const 7 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 194976) "\25\00\00\00\65\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\65\00\00\00\2b\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "scientific-zero") (result i32)
    i32.const 194976 i32.const 194992 i32.const 195120 i32.const 12
    i32.const 256 i32.const 12 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 195200) "\25\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\40\5f\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\32\00\00\00\35\00\00\00\00\00\00\00")
  (func (export "general") (result i32)
    i32.const 195200 i32.const 195216 i32.const 195344 i32.const 3
    i32.const 256 i32.const 3 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 195360) "\25\00\00\00\2e\00\00\00\33\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\88\93\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\65\00\00\00\2b\00\00\00\30\00\00\00\33\00\00\00\00\00\00\00")
  (func (export "general-exponent") (result i32)
    i32.const 195360 i32.const 195392 i32.const 195520 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 195584) "\25\00\00\00\2e\00\00\00\33\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\3c\8f\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\65\00\00\00\2b\00\00\00\30\00\00\00\33\00\00\00\00\00\00\00")
  (func (export "general-transition") (result i32)
    i32.const 195584 i32.const 195616 i32.const 195744 i32.const 5
    i32.const 256 i32.const 5 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 195776) "\25\00\00\00\2e\00\00\00\33\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\6f\2c\28\0c\ca\34\1a\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\00\00\00\00")
  (func (export "general-fixed-transition") (result i32)
    i32.const 195776 i32.const 195808 i32.const 195936 i32.const 6
    i32.const 256 i32.const 6 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 195968) "\25\00\00\00\2e\00\00\00\33\00\00\00\47\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\2d\43\1c\eb\e2\36\ea\3e\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\45\00\00\00\2d\00\00\00\30\00\00\00\35\00\00\00\00\00\00\00")
  (func (export "general-small") (result i32)
    i32.const 195968 i32.const 196000 i32.const 196128 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 196192) "\25\00\00\00\23\00\00\00\2e\00\00\00\35\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\29\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\32\00\00\00\2e\00\00\00\35\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "general-alternate") (result i32)
    i32.const 196192 i32.const 196224 i32.const 196352 i32.const 6
    i32.const 256 i32.const 6 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 196384) "\25\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\00\00\00\00")
  (func (export "general-zero") (result i32)
    i32.const 196384 i32.const 196400 i32.const 196528 i32.const 1
    i32.const 256 i32.const 1 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 196544) "\25\00\00\00\2e\00\00\00\30\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\29\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\65\00\00\00\2b\00\00\00\30\00\00\00\31\00\00\00\00\00\00\00")
  (func (export "general-zero-precision") (result i32)
    i32.const 196544 i32.const 196576 i32.const 196704 i32.const 5
    i32.const 256 i32.const 5 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 196736) "\25\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f8\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\31\00\00\00\2e\00\00\00\38\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-exact") (result i32)
    i32.const 196736 i32.const 196752 i32.const 196880 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 196928) "\25\00\00\00\41\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\bc\3a\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\58\00\00\00\31\00\00\00\2e\00\00\00\41\00\00\00\42\00\00\00\43\00\00\00\50\00\00\00\2d\00\00\00\31\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "hex-upper") (result i32)
    i32.const 196928 i32.const 196944 i32.const 197072 i32.const 11
    i32.const 256 i32.const 11 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 197120) "\25\00\00\00\2e\00\00\00\31\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\80\f8\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\31\00\00\00\2e\00\00\00\38\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-tie-even") (result i32)
    i32.const 197120 i32.const 197152 i32.const 197280 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 197344) "\25\00\00\00\2e\00\00\00\31\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\80\f9\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\31\00\00\00\2e\00\00\00\61\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-tie-odd") (result i32)
    i32.const 197344 i32.const 197376 i32.const 197504 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 197568) "\25\00\00\00\2e\00\00\00\30\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\fc\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\32\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-carry") (result i32)
    i32.const 197568 i32.const 197600 i32.const 197728 i32.const 6
    i32.const 256 i32.const 6 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 197760) "\25\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\80\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\30\00\00\00\78\00\00\00\30\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-zero") (result i32)
    i32.const 197760 i32.const 197776 i32.const 197904 i32.const 7
    i32.const 256 i32.const 7 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 197952) "\25\00\00\00\23\00\00\00\2e\00\00\00\30\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\31\00\00\00\2e\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-alternate") (result i32)
    i32.const 197952 i32.const 197984 i32.const 198112 i32.const 7
    i32.const 256 i32.const 7 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 198144) "\25\00\00\00\30\00\00\00\31\00\00\00\32\00\00\00\2e\00\00\00\31\00\00\00\61\00\00\00\00\00\00\00\00\00\00\00\00\00\f8\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\2e\00\00\00\38\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "hex-pad") (result i32)
    i32.const 198144 i32.const 198176 i32.const 198304 i32.const 12
    i32.const 256 i32.const 12 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 198368) "\25\00\00\00\2e\00\00\00\36\00\00\00\65\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\34\00\00\00\2e\00\00\00\39\00\00\00\34\00\00\00\30\00\00\00\36\00\00\00\35\00\00\00\36\00\00\00\65\00\00\00\2d\00\00\00\33\00\00\00\32\00\00\00\34\00\00\00\00\00\00\00")
  (func (export "double-smallest") (result i32)
    i32.const 198368 i32.const 198400 i32.const 198528 i32.const 13
    i32.const 256 i32.const 13 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 198592) "\25\00\00\00\2e\00\00\00\36\00\00\00\65\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\ff\ff\ff\ef\7f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\37\00\00\00\39\00\00\00\37\00\00\00\36\00\00\00\39\00\00\00\33\00\00\00\65\00\00\00\2b\00\00\00\33\00\00\00\30\00\00\00\38\00\00\00\00\00\00\00")
  (func (export "double-largest") (result i32)
    i32.const 198592 i32.const 198624 i32.const 198752 i32.const 13
    i32.const 256 i32.const 13 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 198816) "\25\00\00\00\2b\00\00\00\30\00\00\00\38\00\00\00\46\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\7f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\2b\00\00\00\49\00\00\00\4e\00\00\00\46\00\00\00\00\00\00\00")
  (func (export "infinity") (result i32)
    i32.const 198816 i32.const 198848 i32.const 198976 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 199040) "\25\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f0\ff\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\69\00\00\00\6e\00\00\00\66\00\00\00\00\00\00\00")
  (func (export "negative-infinity") (result i32)
    i32.const 199040 i32.const 199056 i32.const 199184 i32.const 4
    i32.const 256 i32.const 4 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 199232) "\25\00\00\00\47\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\f8\7f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\4e\00\00\00\41\00\00\00\4e\00\00\00\00\00\00\00")
  (func (export "nan") (result i32)
    i32.const 199232 i32.const 199248 i32.const 199376 i32.const 3
    i32.const 256 i32.const 3 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 199392) "\25\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\40\ff\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "quad-default") (result i32)
    i32.const 199392 i32.const 199408 i32.const 199536 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 199584) "\25\00\00\00\2e\00\00\00\33\00\00\00\35\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\39\00\00\00\00\00\00\00")
  (func (export "quad-low-bit") (result i32)
    i32.const 199584 i32.const 199616 i32.const 199744 i32.const 37
    i32.const 256 i32.const 37 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 199904) "\25\00\00\00\4c\00\00\00\61\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\78\00\00\00\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "quad-hex-low-bit") (result i32)
    i32.const 199904 i32.const 199920 i32.const 200048 i32.const 35
    i32.const 256 i32.const 35 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 200192) "\25\00\00\00\2e\00\00\00\36\00\00\00\4c\00\00\00\65\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\36\00\00\00\2e\00\00\00\34\00\00\00\37\00\00\00\35\00\00\00\31\00\00\00\37\00\00\00\35\00\00\00\65\00\00\00\2d\00\00\00\34\00\00\00\39\00\00\00\36\00\00\00\36\00\00\00\00\00\00\00")
  (func (export "quad-smallest") (result i32)
    i32.const 200192 i32.const 200224 i32.const 200352 i32.const 14
    i32.const 256 i32.const 14 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 200416) "\25\00\00\00\2e\00\00\00\36\00\00\00\4c\00\00\00\65\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\ff\fe\7f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\31\00\00\00\2e\00\00\00\31\00\00\00\38\00\00\00\39\00\00\00\37\00\00\00\33\00\00\00\31\00\00\00\65\00\00\00\2b\00\00\00\34\00\00\00\39\00\00\00\33\00\00\00\32\00\00\00\00\00\00\00")
  (func (export "quad-largest") (result i32)
    i32.const 200416 i32.const 200448 i32.const 200576 i32.const 14
    i32.const 256 i32.const 14 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 200640) "\25\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\80\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\2d\00\00\00\30\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "quad-negative-zero") (result i32)
    i32.const 200640 i32.const 200656 i32.const 200784 i32.const 9
    i32.const 256 i32.const 9 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 200832) "\25\00\00\00\2e\00\00\00\30\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\40\00\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\32\00\00\00\00\00\00\00")
  (func (export "quad-tie-even") (result i32)
    i32.const 200832 i32.const 200864 i32.const 200992 i32.const 1
    i32.const 256 i32.const 1 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 201024) "\25\00\00\00\2e\00\00\00\30\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\c0\00\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\34\00\00\00\00\00\00\00")
  (func (export "quad-tie-odd") (result i32)
    i32.const 201024 i32.const 201056 i32.const 201184 i32.const 1
    i32.const 256 i32.const 1 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 201216) "\25\00\00\00\2e\00\00\00\30\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\40\00\40\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\33\00\00\00\00\00\00\00")
  (func (export "quad-round-above") (result i32)
    i32.const 201216 i32.const 201248 i32.const 201376 i32.const 1
    i32.const 256 i32.const 1 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 201408) "\25\00\00\00\2e\00\00\00\36\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "quad-tiny-fixed") (result i32)
    i32.const 201408 i32.const 201440 i32.const 201568 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 201632) "\25\00\00\00\4c\00\00\00\41\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\7f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\49\00\00\00\4e\00\00\00\46\00\00\00\00\00\00\00")
  (func (export "quad-infinity") (result i32)
    i32.const 201632 i32.const 201648 i32.const 201776 i32.const 3
    i32.const 256 i32.const 3 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 201792) "\25\00\00\00\4c\00\00\00\66\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\7f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\6e\00\00\00\61\00\00\00\6e\00\00\00\00\00\00\00")
  (func (export "quad-nan") (result i32)
    i32.const 201792 i32.const 201808 i32.const 201936 i32.const 3
    i32.const 256 i32.const 3 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 201952) "\25\00\00\00\64\00\00\00\7c\00\00\00\25\00\00\00\2e\00\00\00\32\00\00\00\66\00\00\00\7c\00\00\00\25\00\00\00\2e\00\00\00\33\00\00\00\35\00\00\00\4c\00\00\00\66\00\00\00\7c\00\00\00\25\00\00\00\6c\00\00\00\6c\00\00\00\64\00\00\00\00\00\00\00\2a\00\00\00\5a\5a\5a\5a\00\00\00\00\00\00\f4\3f\01\00\00\00\00\00\00\00\00\00\00\00\00\00\ff\3f\ff\ff\ff\ff\fe\ff\ff\ff\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\34\00\00\00\32\00\00\00\7c\00\00\00\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\7c\00\00\00\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\39\00\00\00\7c\00\00\00\2d\00\00\00\34\00\00\00\32\00\00\00\39\00\00\00\34\00\00\00\39\00\00\00\36\00\00\00\37\00\00\00\32\00\00\00\39\00\00\00\37\00\00\00\00\00\00\00")
  (func (export "mixed-float-alignment") (result i32)
    i32.const 201952 i32.const 202032 i32.const 202160 i32.const 57
    i32.const 256 i32.const 57 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 202400) "\41\00\00\00\25\00\00\00\68\00\00\00\68\00\00\00\6e\00\00\00\42\00\00\00\25\00\00\00\68\00\00\00\6e\00\00\00\43\00\00\00\25\00\00\00\6e\00\00\00\44\00\00\00\25\00\00\00\6c\00\00\00\6e\00\00\00\45\00\00\00\25\00\00\00\6c\00\00\00\6c\00\00\00\6e\00\00\00\46\00\00\00\25\00\00\00\6a\00\00\00\6e\00\00\00\47\00\00\00\25\00\00\00\7a\00\00\00\6e\00\00\00\48\00\00\00\25\00\00\00\74\00\00\00\6e\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\97\04\00\0c\97\04\00\14\97\04\00\1c\97\04\00\28\97\04\00\38\97\04\00\44\97\04\00\4c\97\04\00\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\41\00\00\00\42\00\00\00\43\00\00\00\44\00\00\00\45\00\00\00\46\00\00\00\47\00\00\00\48\00\00\00\00\00\00\00")
  (func (export "count-modifiers") (result i32)
    i32.const 202400 i32.const 202544 i32.const 202672 i32.const 8
    i32.const 256 i32.const 8 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 204000) "\25\00\00\00\2e\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\37\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\c0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\31\00\00\00\32\00\00\00\35\00\00\00\00\00\00\00")
  (func (export "general-huge-precision") (result i32) i32.const 204000 i32.const 204064 i32.const 204192 i32.const 5 i32.const 16 i32.const 5 i32.const 17 i32.const 0 i32.const 0 call $check)
  (data (i32.const 204512) "\25\00\00\00\23\00\00\00\2e\00\00\00\32\00\00\00\31\00\00\00\34\00\00\00\37\00\00\00\34\00\00\00\38\00\00\00\33\00\00\00\36\00\00\00\34\00\00\00\37\00\00\00\67\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\c0\3f\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\5a\30\00\00\00\2e\00\00\00\31\00\00\00\32\00\00\00\35\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "general-huge-alternate") (result i32) i32.const 204512 i32.const 204576 i32.const 204704 i32.const 15 i32.const 16 i32.const -1 i32.const 75 i32.const 0 i32.const 0 call $check)
  (func (export "count-values-and-canaries") (result i32)
    (local $at i32)
    i32.const 300804 i32.load8_u i32.const 1 i32.ne if i32.const 0 return end
    i32.const 300812 i32.load16_u i32.const 2 i32.ne if i32.const 0 return end
    i32.const 300820 i32.load i32.const 3 i32.ne if i32.const 0 return end
    i32.const 300828 i32.load i32.const 4 i32.ne if i32.const 0 return end
    i32.const 300840 i64.load i64.const 5 i64.ne if i32.const 0 return end
    i32.const 300856 i64.load i64.const 6 i64.ne if i32.const 0 return end
    i32.const 300868 i32.load i32.const 7 i32.ne if i32.const 0 return end
    i32.const 300876 i32.load i32.const 8 i32.ne if i32.const 0 return end
    loop $guards
      local.get $at i32.const 4 i32.lt_u
      local.get $at i32.const 5 i32.ge_u local.get $at i32.const 12 i32.lt_u i32.and i32.or
      local.get $at i32.const 14 i32.ge_u local.get $at i32.const 20 i32.lt_u i32.and i32.or
      local.get $at i32.const 24 i32.ge_u local.get $at i32.const 28 i32.lt_u i32.and i32.or
      local.get $at i32.const 32 i32.ge_u local.get $at i32.const 40 i32.lt_u i32.and i32.or
      local.get $at i32.const 48 i32.ge_u local.get $at i32.const 56 i32.lt_u i32.and i32.or
      local.get $at i32.const 64 i32.ge_u local.get $at i32.const 68 i32.lt_u i32.and i32.or
      local.get $at i32.const 72 i32.ge_u local.get $at i32.const 76 i32.lt_u i32.and i32.or
      local.get $at i32.const 80 i32.ge_u i32.or
      if i32.const 300800 local.get $at i32.add i32.load8_u i32.const 0xa5 i32.ne if i32.const 0 return end end
      local.get $at i32.const 1 i32.add local.tee $at i32.const 96 i32.lt_u br_if $guards
    end i32.const 1)
)
(assert_return (invoke $wide-format "literal-wide") (i32.const 1))
(assert_return (invoke $wide-format "mixed-promotions") (i32.const 1))
(assert_return (invoke $wide-format "signed-flags") (i32.const 1))
(assert_return (invoke $wide-format "integer-alternate") (i32.const 1))
(assert_return (invoke $wide-format "negative-star-width") (i32.const 1))
(assert_return (invoke $wide-format "negative-star-precision") (i32.const 1))
(assert_return (invoke $wide-format "wide-strings") (i32.const 1))
(assert_return (invoke $wide-format "wide-signed-elements") (i32.const 1))
(assert_return (invoke $wide-format "utf8-strings") (i32.const 1))
(assert_return (invoke $wide-format "utf8-zero-precision") (i32.const 1))
(assert_return (invoke $wide-format "invalid-utf8") (i32.const 1))
(assert_return (invoke $wide-format "truncated-utf8") (i32.const 1))
(assert_return (invoke $wide-format "utf8-overlong") (i32.const 1))
(assert_return (invoke $wide-format "utf8-surrogate") (i32.const 1))
(assert_return (invoke $wide-format "utf8-out-of-range") (i32.const 1))
(assert_return (invoke $wide-format "capacity-5") (i32.const 1))
(assert_return (invoke $wide-format "capacity-4") (i32.const 1))
(assert_return (invoke $wide-format "capacity-1") (i32.const 1))
(assert_return (invoke $wide-format "capacity-0") (i32.const 1))
(assert_return (invoke $wide-format "empty") (i32.const 1))
(assert_return (invoke $wide-format "empty-zero-capacity") (i32.const 1))
(assert_return (invoke $wide-format "null-zero-capacity") (i32.const 1))
(assert_return (invoke $wide-format "bad-format") (i32.const 1))
(assert_return (invoke $wide-format "dangling-percent") (i32.const 1))
(assert_return (invoke $wide-format "positional-rejected") (i32.const 1))
(assert_return (invoke $wide-format "field-overflow") (i32.const 1))
(assert_return (invoke $wide-format "precision-overflow") (i32.const 1))
(assert_return (invoke $wide-format "star-width-overflow") (i32.const 1))
(assert_return (invoke $wide-format "huge-width-bounded") (i32.const 1))
(assert_return (invoke $wide-format "huge-precision-bounded") (i32.const 1))
(assert_return (invoke $wide-format "default-fixed") (i32.const 1))
(assert_return (invoke $wide-format "default-negative-zero") (i32.const 1))
(assert_return (invoke $wide-format "half-even") (i32.const 1))
(assert_return (invoke $wide-format "half-odd") (i32.const 1))
(assert_return (invoke $wide-format "half-negative") (i32.const 1))
(assert_return (invoke $wide-format "fraction-tie-even") (i32.const 1))
(assert_return (invoke $wide-format "fraction-tie-odd") (i32.const 1))
(assert_return (invoke $wide-format "fraction-below") (i32.const 1))
(assert_return (invoke $wide-format "fraction-above") (i32.const 1))
(assert_return (invoke $wide-format "decimal-exact") (i32.const 1))
(assert_return (invoke $wide-format "fixed-carry") (i32.const 1))
(assert_return (invoke $wide-format "fixed-zero-leading") (i32.const 1))
(assert_return (invoke $wide-format "decimal-flags") (i32.const 1))
(assert_return (invoke $wide-format "decimal-left") (i32.const 1))
(assert_return (invoke $wide-format "decimal-alternate") (i32.const 1))
(assert_return (invoke $wide-format "scientific") (i32.const 1))
(assert_return (invoke $wide-format "scientific-carry") (i32.const 1))
(assert_return (invoke $wide-format "scientific-zero") (i32.const 1))
(assert_return (invoke $wide-format "general") (i32.const 1))
(assert_return (invoke $wide-format "general-exponent") (i32.const 1))
(assert_return (invoke $wide-format "general-transition") (i32.const 1))
(assert_return (invoke $wide-format "general-fixed-transition") (i32.const 1))
(assert_return (invoke $wide-format "general-small") (i32.const 1))
(assert_return (invoke $wide-format "general-alternate") (i32.const 1))
(assert_return (invoke $wide-format "general-zero") (i32.const 1))
(assert_return (invoke $wide-format "general-zero-precision") (i32.const 1))
(assert_return (invoke $wide-format "hex-exact") (i32.const 1))
(assert_return (invoke $wide-format "hex-upper") (i32.const 1))
(assert_return (invoke $wide-format "hex-tie-even") (i32.const 1))
(assert_return (invoke $wide-format "hex-tie-odd") (i32.const 1))
(assert_return (invoke $wide-format "hex-carry") (i32.const 1))
(assert_return (invoke $wide-format "hex-zero") (i32.const 1))
(assert_return (invoke $wide-format "hex-alternate") (i32.const 1))
(assert_return (invoke $wide-format "hex-pad") (i32.const 1))
(assert_return (invoke $wide-format "double-smallest") (i32.const 1))
(assert_return (invoke $wide-format "double-largest") (i32.const 1))
(assert_return (invoke $wide-format "infinity") (i32.const 1))
(assert_return (invoke $wide-format "negative-infinity") (i32.const 1))
(assert_return (invoke $wide-format "nan") (i32.const 1))
(assert_return (invoke $wide-format "quad-default") (i32.const 1))
(assert_return (invoke $wide-format "quad-low-bit") (i32.const 1))
(assert_return (invoke $wide-format "quad-hex-low-bit") (i32.const 1))
(assert_return (invoke $wide-format "quad-smallest") (i32.const 1))
(assert_return (invoke $wide-format "quad-largest") (i32.const 1))
(assert_return (invoke $wide-format "quad-negative-zero") (i32.const 1))
(assert_return (invoke $wide-format "quad-tie-even") (i32.const 1))
(assert_return (invoke $wide-format "quad-tie-odd") (i32.const 1))
(assert_return (invoke $wide-format "quad-round-above") (i32.const 1))
(assert_return (invoke $wide-format "quad-tiny-fixed") (i32.const 1))
(assert_return (invoke $wide-format "quad-infinity") (i32.const 1))
(assert_return (invoke $wide-format "quad-nan") (i32.const 1))
(assert_return (invoke $wide-format "mixed-float-alignment") (i32.const 1))
(assert_return (invoke $wide-format "general-huge-precision") (i32.const 1))
(assert_return (invoke $wide-format "general-huge-alternate") (i32.const 1))
(assert_return (invoke $wide-format "direct-swprintf") (i32.const 1))
(assert_return (invoke $wide-format "count-modifiers") (i32.const 1))
(assert_return (invoke $wide-format "count-values-and-canaries") (i32.const 1))
