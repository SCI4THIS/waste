;; Actual compiled C/C++ observations. Five i32 metadata fields and two
;; wchar_t[128] buffers: 1044 bytes per record, nine records per executable.
(module $wide-format
  (import "waste-runtime" "memory" (memory 8))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/wide-format-c.bin\00")
  (data (i32.const 180064) "/tmp/wide-format-cxx.bin\00")
  (data (i32.const 180128) "/tmp/wide-format-c-status.txt\00")
  (data (i32.const 180192) "/tmp/wide-format-cxx-status.txt\00")
  (func $capture (param $path i32) (param $output i32) (param $count i32) (result i32)
    (local $fd i32) (local $bytes i32)
    local.get $path i32.const 0 i32.const 0 call $open local.set $fd
    local.get $fd local.get $output local.get $count call $read local.set $bytes
    local.get $fd call $close drop local.get $bytes)
  (func (export "capture-c-and-cxx") (result i32)
    i32.const 180000 i32.const 182000 i32.const 10000 call $capture i32.const 9396 i32.eq
    i32.const 180064 i32.const 196000 i32.const 10000 call $capture i32.const 9396 i32.eq i32.and)
  (func $status (param $path i32) (result i32)
    local.get $path i32.const 211000 i32.const 8 call $capture i32.const 2 i32.eq
    i32.const 211000 i32.load16_u i32.const 0x0a30 i32.eq i32.and)
  (func (export "c-exit") (result i32) i32.const 180128 call $status)
  (func (export "cxx-exit") (result i32) i32.const 180192 call $status)
  (func $buffer (param $output i32) (param $expected i32) (param $written i32) (result i32)
    (local $at i32)
    local.get $output i32.const 8 i32.add local.get $expected local.get $written i32.const 4 i32.mul call $compare
    if i32.const 0 return end
    loop $guards
      local.get $at i32.const 2 i32.lt_u
      local.get $at local.get $written i32.const 2 i32.add i32.ge_u i32.or
      if
        local.get $output local.get $at i32.const 4 i32.mul i32.add i32.load i32.const 0x1234abcd i32.ne
        if i32.const 0 return end
      end
      local.get $at i32.const 1 i32.add local.tee $at i32.const 128 i32.lt_u br_if $guards
    end i32.const 1)
  (func $record (param $base i32) (param $index i32) (param $count i32) (param $error i32)
                (param $expected i32) (param $written i32) (result i32)
    (local $record i32)
    local.get $base local.get $index i32.const 1044 i32.mul i32.add local.set $record
    local.get $record i32.load local.get $count i32.eq
    local.get $record i32.load offset=4 local.get $count i32.eq i32.and
    local.get $record i32.load offset=8 local.get $error i32.eq i32.and
    local.get $record i32.load offset=12 local.get $error i32.eq i32.and
    local.get $record i32.load offset=16 i32.const 42 i32.eq i32.and
    local.get $record i32.const 20 i32.add local.get $expected local.get $written call $buffer i32.and
    local.get $record i32.const 532 i32.add local.get $expected local.get $written call $buffer i32.and)
  (data (i32.const 212000) "\34\00\00\00\32\00\00\00\7c\00\00\00\31\00\00\00\2e\00\00\00\32\00\00\00\35\00\00\00\7c\00\00\00\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\39\00\00\00\7c\00\00\00\2d\00\00\00\34\00\00\00\32\00\00\00\39\00\00\00\34\00\00\00\39\00\00\00\36\00\00\00\37\00\00\00\32\00\00\00\39\00\00\00\37\00\00\00\00\00\00\00")
  (func (export "c-mixed-alignments") (result i32) i32.const 182000 i32.const 0 i32.const 57 i32.const 17 i32.const 212000 i32.const 58 call $record)
  (func (export "cxx-mixed-alignments") (result i32) i32.const 196000 i32.const 0 i32.const 57 i32.const 17 i32.const 212000 i32.const 58 call $record)
  (data (i32.const 212512) "\34\00\00\00\32\00\00\00\7c\00\00\00\e9\00\00\00\00\f6\01\00\7c\00\00\00\00\f6\01\00\7c\00\00\00\e9\00\00\00\00\f6\01\00\00\00\00\00")
  (func (export "c-wide-utf8") (result i32) i32.const 182000 i32.const 1 i32.const 10 i32.const 17 i32.const 212512 i32.const 11 call $record)
  (func (export "cxx-wide-utf8") (result i32) i32.const 196000 i32.const 1 i32.const 10 i32.const 17 i32.const 212512 i32.const 11 call $record)
  (data (i32.const 213024) "\34\00\00\00\32\00\00\00\7c\00\00\00\36\00\00\00\2e\00\00\00\34\00\00\00\37\00\00\00\35\00\00\00\31\00\00\00\37\00\00\00\35\00\00\00\65\00\00\00\2d\00\00\00\34\00\00\00\39\00\00\00\36\00\00\00\36\00\00\00\7c\00\00\00\30\00\00\00\78\00\00\00\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\70\00\00\00\2b\00\00\00\30\00\00\00\00\00\00\00")
  (func (export "c-full-quad-range") (result i32) i32.const 182000 i32.const 2 i32.const 53 i32.const 17 i32.const 213024 i32.const 54 call $record)
  (func (export "cxx-full-quad-range") (result i32) i32.const 196000 i32.const 2 i32.const 53 i32.const 17 i32.const 213024 i32.const 54 call $record)
  (data (i32.const 213536) "\34\00\00\00\32\00\00\00\2d\00\00\00\61\00\00\00\62\00\00\00\63\00\00\00\00\00\00\00")
  (func (export "c-truncation") (result i32) i32.const 182000 i32.const 3 i32.const -1 i32.const 75 i32.const 213536 i32.const 7 call $record)
  (func (export "cxx-truncation") (result i32) i32.const 196000 i32.const 3 i32.const -1 i32.const 75 i32.const 213536 i32.const 7 call $record)
  (data (i32.const 214048) "\34\00\00\00\32\00\00\00\2d\00\00\00\61\00\00\00\62\00\00\00\63\00\00\00\64\00\00\00\65\00\00\00\66\00\00\00\00\00\00\00")
  (func (export "c-exact-fit") (result i32) i32.const 182000 i32.const 4 i32.const 9 i32.const 17 i32.const 214048 i32.const 10 call $record)
  (func (export "cxx-exact-fit") (result i32) i32.const 196000 i32.const 4 i32.const 9 i32.const 17 i32.const 214048 i32.const 10 call $record)
  (data (i32.const 214560) "\00\00\00\00")
  (func (export "c-one-element") (result i32) i32.const 182000 i32.const 5 i32.const -1 i32.const 75 i32.const 214560 i32.const 1 call $record)
  (func (export "cxx-one-element") (result i32) i32.const 196000 i32.const 5 i32.const -1 i32.const 75 i32.const 214560 i32.const 1 call $record)
  (data (i32.const 215072) "\00\00\00\00")
  (func (export "c-zero-capacity") (result i32) i32.const 182000 i32.const 6 i32.const -1 i32.const 75 i32.const 215072 i32.const 0 call $record)
  (func (export "cxx-zero-capacity") (result i32) i32.const 196000 i32.const 6 i32.const -1 i32.const 75 i32.const 215072 i32.const 0 call $record)
  (data (i32.const 215584) "\34\00\00\00\32\00\00\00\7c\00\00\00\30\00\00\00\2e\00\00\00\31\00\00\00\32\00\00\00\35\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\20\00\00\00\7c\00\00\00\31\00\00\00\38\00\00\00\34\00\00\00\34\00\00\00\36\00\00\00\37\00\00\00\34\00\00\00\34\00\00\00\30\00\00\00\37\00\00\00\33\00\00\00\37\00\00\00\30\00\00\00\39\00\00\00\35\00\00\00\35\00\00\00\31\00\00\00\36\00\00\00\31\00\00\00\35\00\00\00\7c\00\00\00\2d\00\00\00\37\00\00\00\00\00\00\00")
  (func (export "c-star-and-widths") (result i32) i32.const 182000 i32.const 7 i32.const 37 i32.const 17 i32.const 215584 i32.const 38 call $record)
  (func (export "cxx-star-and-widths") (result i32) i32.const 196000 i32.const 7 i32.const 37 i32.const 17 i32.const 215584 i32.const 38 call $record)
  (data (i32.const 216096) "\31\00\00\00\2e\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\30\00\00\00\31\00\00\00\39\00\00\00\7c\00\00\00\00\f6\01\00\00\00\00\00")
  (func (export "c-direct-swprintf") (result i32) i32.const 182000 i32.const 8 i32.const 39 i32.const 17 i32.const 216096 i32.const 40 call $record)
  (func (export "cxx-direct-swprintf") (result i32) i32.const 196000 i32.const 8 i32.const 39 i32.const 17 i32.const 216096 i32.const 40 call $record)
)
(assert_return (invoke $wide-format "capture-c-and-cxx") (i32.const 1))
(assert_return (invoke $wide-format "c-exit") (i32.const 1))
(assert_return (invoke $wide-format "cxx-exit") (i32.const 1))
(assert_return (invoke $wide-format "c-mixed-alignments") (i32.const 1))
(assert_return (invoke $wide-format "cxx-mixed-alignments") (i32.const 1))
(assert_return (invoke $wide-format "c-wide-utf8") (i32.const 1))
(assert_return (invoke $wide-format "cxx-wide-utf8") (i32.const 1))
(assert_return (invoke $wide-format "c-full-quad-range") (i32.const 1))
(assert_return (invoke $wide-format "cxx-full-quad-range") (i32.const 1))
(assert_return (invoke $wide-format "c-truncation") (i32.const 1))
(assert_return (invoke $wide-format "cxx-truncation") (i32.const 1))
(assert_return (invoke $wide-format "c-exact-fit") (i32.const 1))
(assert_return (invoke $wide-format "cxx-exact-fit") (i32.const 1))
(assert_return (invoke $wide-format "c-one-element") (i32.const 1))
(assert_return (invoke $wide-format "cxx-one-element") (i32.const 1))
(assert_return (invoke $wide-format "c-zero-capacity") (i32.const 1))
(assert_return (invoke $wide-format "cxx-zero-capacity") (i32.const 1))
(assert_return (invoke $wide-format "c-star-and-widths") (i32.const 1))
(assert_return (invoke $wide-format "cxx-star-and-widths") (i32.const 1))
(assert_return (invoke $wide-format "c-direct-swprintf") (i32.const 1))
(assert_return (invoke $wide-format "cxx-direct-swprintf") (i32.const 1))
