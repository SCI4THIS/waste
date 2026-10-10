(module $search_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "waste-runtime" "__indirect_function_table" (table 1 funcref))
  (import "libc" "bsearch" (func $search (param i32 i32 i32 i32 i32) (result i32)))
  (import "libc" "atol" (func $atol (param i32) (result i32)))
  (import "libc" "labs" (func $labs (param i32) (result i32)))
  (import "libc" "strtok" (func $token (param i32 i32) (result i32)))
  (import "libc" "strcmp" (func $equal (param i32 i32) (result i32)))
  (data (i32.const 180000) "\01\00\00\00\03\00\00\00\05\00\00\00\09\00\00\00")
  (data (i32.const 180032) "  -42\00,,one::two,three,,\00")
  (data (i32.const 180064) ",:\00one\00two\00three\00")
  (func $compare (param i32 i32) (result i32)
    local.get 0 i32.load local.get 1 i32.load i32.sub)
  (elem (i32.const 0) $compare)
  (func (export "binary-search") (result i32)
    i32.const 181000 i32.const 5 i32.store
    i32.const 181000 i32.const 180000 i32.const 4 i32.const 4 i32.const 0 call $search
    i32.const 180008 i32.eq
    i32.const 181000 i32.const 6 i32.store
    i32.const 181000 i32.const 180000 i32.const 4 i32.const 4 i32.const 0 call $search i32.eqz i32.and
    i32.const 181000 i32.const 180000 i32.const 0 i32.const 4 i32.const 0 call $search i32.eqz i32.and)
  (func (export "numbers") (result i32)
    i32.const 180032 call $atol i32.const -42 i32.eq
    i32.const -42 call $labs i32.const 42 i32.eq i32.and)
  (func (export "tokens") (result i32)
    i32.const 180038 i32.const 180064 call $token i32.const 180067 call $equal i32.eqz
    i32.const 0 i32.const 180064 call $token i32.const 180071 call $equal i32.eqz i32.and
    i32.const 0 i32.const 180064 call $token i32.const 180075 call $equal i32.eqz i32.and
    i32.const 0 i32.const 180064 call $token i32.eqz i32.and
    i32.const 0 i32.const 180064 call $token i32.eqz i32.and))
(assert_return (invoke $search_tests "binary-search") (i32.const 1))
(assert_return (invoke $search_tests "numbers") (i32.const 1))
(assert_return (invoke $search_tests "tokens") (i32.const 1))
