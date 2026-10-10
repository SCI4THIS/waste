;; Readline chooses its insert path from capability presence. A non-null,
;; empty im capability would overwrite the suffix instead of inserting.
(module $termcap_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "tgetstr" (func $get (param i32 i32) (result i32)))
  (import "libc" "strcmp" (func $compare (param i32 i32) (result i32)))
  (data (i32.const 180000) "im\00ei\00zz\00")
  (data (i32.const 180032) "\1b[4h\00\1b[4l\00")

  (func (export "insert-mode") (result i32)
    i32.const 180000 i32.const 0 call $get
    i32.const 180032 call $compare i32.eqz
    i32.const 180003 i32.const 0 call $get
    i32.const 180037 call $compare i32.eqz
    i32.and)

  (func (export "copied-capabilities") (result i32)
    i32.const 181000 i32.const 181100 i32.store
    i32.const 180000 i32.const 181000 call $get
    i32.const 181100 i32.eq
    i32.const 180003 i32.const 181000 call $get
    i32.const 181105 i32.eq i32.and
    i32.const 181000 i32.load i32.const 181110 i32.eq i32.and
    i32.const 181100 i32.const 180032 call $compare i32.eqz i32.and
    i32.const 181105 i32.const 180037 call $compare i32.eqz i32.and)

  (func (export "unsupported-capability") (result i32)
    i32.const 180006 i32.const 0 call $get i32.eqz))

(assert_return (invoke $termcap_tests "insert-mode") (i32.const 1))
(assert_return (invoke $termcap_tests "copied-capabilities") (i32.const 1))
(assert_return (invoke $termcap_tests "unsupported-capability") (i32.const 1))
