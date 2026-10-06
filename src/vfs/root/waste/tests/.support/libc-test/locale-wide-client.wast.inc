(register "waste-libc" $waste_libc)
(module $locale_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "waste_allocator_init" (func $init (param i32) (result i32)))
  (import "waste-libc" "mbstowcs" (func $mbstowcs (param i32 i32 i32) (result i32)))
  (import "waste-libc" "wcrtomb" (func $wcrtomb (param i32 i32 i32) (result i32)))
  (import "waste-libc" "wcslen" (func $wcslen (param i32) (result i32)))
  (import "waste-libc" "wcwidth" (func $wcwidth (param i32) (result i32)))
  (import "waste-libc" "wcswidth" (func $wcswidth (param i32 i32) (result i32)))
  (import "waste-libc" "setlocale" (func $setlocale (param i32 i32) (result i32)))
  (import "waste-libc" "gettext" (func $gettext (param i32) (result i32)))
  (import "waste-libc" "ngettext" (func $ngettext (param i32 i32 i32) (result i32)))
  (import "waste-libc" "__ctype_get_mb_cur_max" (func $mbmax (result i32)))
  (import "waste-libc" "locale_charset" (func $locale_charset (result i32)))
  (import "waste-libc" "mbsinit" (func $mbsinit (param i32) (result i32)))
  (import "waste-libc" "iconv_open" (func $iconv_open (param i32 i32) (result i32)))
  (import "waste-libc" "iconv" (func $iconv (param i32 i32 i32 i32 i32) (result i32)))
  (data (i32.const 180000) "A\c3\a9\f0\9f\98\80\00")
  (data (i32.const 180032) "one\00many\00")
  (func (export "utf8-roundtrip") (result i32)
    i32.const 220000 call $init drop
    i32.const 181000 i32.const 180000 i32.const 8 call $mbstowcs
    i32.const 3 i32.eq
    i32.const 181000 i32.load i32.const 65 i32.eq i32.and
    i32.const 181004 i32.load i32.const 233 i32.eq i32.and
    i32.const 181008 i32.load i32.const 0x1f600 i32.eq i32.and
    i32.const 181000 call $wcslen i32.const 3 i32.eq i32.and
    i32.const 181100 i32.const 0x1f600 i32.const 0 call $wcrtomb i32.const 4 i32.eq i32.and
    i32.const 181100 i32.load i32.const 0x80989ff0 i32.eq i32.and)
  (func (export "width-and-locale") (result i32)
    i32.const 220000 call $init drop
    i32.const 0x0301 call $wcwidth i32.eqz
    i32.const 0x1f600 call $wcwidth i32.const 2 i32.eq i32.and
    i32.const 0 i32.const 0 call $setlocale i32.eqz i32.eqz i32.and
    i32.const 180032 call $gettext i32.const 180032 i32.eq i32.and
    i32.const 180032 i32.const 180036 i32.const 2 call $ngettext i32.const 180036 i32.eq i32.and
    call $mbmax i32.const 4 i32.eq i32.and
    i32.const 0 call $mbsinit i32.const 1 i32.eq i32.and
    call $locale_charset i32.load i32.const 0x2d465455 i32.eq i32.and)
  (func (export "iconv-utf8") (result i32)
    i32.const 220000 call $init drop
    i32.const 181000 i32.const 180000 i32.store
    i32.const 181004 i32.const 7 i32.store
    i32.const 181008 i32.const 181100 i32.store
    i32.const 181012 i32.const 16 i32.store
    i32.const 0 i32.const 0 call $iconv_open
    i32.const 181000 i32.const 181004 i32.const 181008 i32.const 181012 call $iconv i32.eqz
    i32.const 181004 i32.load i32.eqz i32.and
    i32.const 181100 i32.load i32.const 0xffffff i32.and i32.const 0xa9c341 i32.eq i32.and))
(assert_return (invoke $locale_tests "utf8-roundtrip") (i32.const 1))
(assert_return (invoke $locale_tests "width-and-locale") (i32.const 1))
(assert_return (invoke $locale_tests "iconv-utf8") (i32.const 1))
