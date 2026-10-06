(register "waste-libc" $waste_libc)
(module $matching_tests
  (import "waste-libc" "memory" (memory 4))
  (import "waste-libc" "__indirect_function_table" (table 1 1 funcref))
  (import "waste-libc" "qsort" (func $qsort (param i32 i32 i32 i32)))
  (import "waste-libc" "fnmatch" (func $fnmatch (param i32 i32 i32) (result i32)))
  (import "waste-libc" "regcomp" (func $regcomp (param i32 i32 i32) (result i32)))
  (import "waste-libc" "regexec" (func $regexec (param i32 i32 i32 i32 i32) (result i32)))
  (import "waste-libc" "regfree" (func $regfree (param i32)))
  (data (i32.const 180000) "a*Z\00AlphaZ\00^a.*z$\00not-a-match\00")
  (data (i32.const 181000) "\09\00\00\00\01\00\00\00\05\00\00\00\03\00\00\00")
  (func $compare (param $left i32) (param $right i32) (result i32)
    local.get $left i32.load local.get $right i32.load i32.sub)
  (elem (i32.const 0) $compare)

  (func (export "matching-and-sort") (result i32)
    i32.const 180000 i32.const 180004 i32.const 16 call $fnmatch i32.eqz
    i32.const 182000 i32.const 180011 i32.const 2 call $regcomp i32.eqz i32.and
    i32.const 182000 i32.const 180004 i32.const 0 i32.const 0 i32.const 0 call $regexec i32.eqz i32.and
    i32.const 182000 i32.const 180019 i32.const 0 i32.const 0 i32.const 0 call $regexec i32.const 1 i32.eq i32.and
    i32.const 182000 call $regfree
    i32.const 181000 i32.const 4 i32.const 4 i32.const 0 call $qsort
    i32.const 181000 i32.load i32.const 1 i32.eq i32.and
    i32.const 181004 i32.load i32.const 3 i32.eq i32.and
    i32.const 181008 i32.load i32.const 5 i32.eq i32.and
    i32.const 181012 i32.load i32.const 9 i32.eq i32.and))
(assert_return (invoke $matching_tests "matching-and-sort") (i32.const 1))
