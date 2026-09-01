(register "waste-libc" $waste_libc)
(module $terminal_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "__indirect_function_table" (table 1 1 funcref))
  (import "waste-libc" "ttyname" (func $ttyname (param i32) (result i32)))
  (import "waste-libc" "tcflow" (func $tcflow (param i32 i32) (result i32)))
  (import "waste-libc" "tgetent" (func $tgetent (param i32 i32) (result i32)))
  (import "waste-libc" "tgetflag" (func $tgetflag (param i32) (result i32)))
  (import "waste-libc" "tgetnum" (func $tgetnum (param i32) (result i32)))
  (import "waste-libc" "tgetstr" (func $tgetstr (param i32 i32) (result i32)))
  (import "waste-libc" "tgoto" (func $tgoto (param i32 i32 i32) (result i32)))
  (import "waste-libc" "tputs" (func $tputs (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "abc\00xterm\00")
  (func $put (param $character i32) (result i32)
    i32.const 181000
    i32.const 181000 i32.load local.get $character i32.add
    i32.store
    i32.const 0)
  (elem (i32.const 0) $put)
  (func (export "terminal") (result i32)
    i32.const 1 call $ttyname i32.eqz i32.eqz
    i32.const 3 call $ttyname i32.eqz i32.and
    i32.const 1 i32.const 0 call $tcflow i32.eqz i32.and
    i32.const 0 i32.const 180004 call $tgetent i32.const 1 i32.eq i32.and
    i32.const 180000 call $tgetflag i32.eqz i32.and
    i32.const 180000 call $tgetnum i32.const -1 i32.eq i32.and
    i32.const 180000 i32.const 0 call $tgetstr i32.eqz i32.and
    i32.const 180000 i32.const 2 i32.const 3 call $tgoto i32.const 180000 i32.eq i32.and
    i32.const 180000 i32.const 1 i32.const 0 call $tputs i32.eqz i32.and
    i32.const 181000 i32.load i32.const 294 i32.eq i32.and))
(assert_return (invoke $terminal_tests "terminal") (i32.const 1))
