;; Run from Bash with /bin/wast --verbose; imports the installed shared libc.
(module $memory_tests
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "waste_allocator_init" (func $init (param i32) (result i32)))
  (import "libc" "strlen" (func $strlen (param i32) (result i32)))
  (import "libc" "strnlen" (func $strnlen (param i32 i32) (result i32)))
  (import "libc" "strcpy" (func $strcpy (param i32 i32) (result i32)))
  (import "libc" "strncpy" (func $strncpy (param i32 i32 i32) (result i32)))
  (import "libc" "strcat" (func $strcat (param i32 i32) (result i32)))
  (import "libc" "strcmp" (func $strcmp (param i32 i32) (result i32)))
  (import "libc" "strncmp" (func $strncmp (param i32 i32 i32) (result i32)))
  (import "libc" "strcasecmp" (func $strcasecmp (param i32 i32) (result i32)))
  (import "libc" "strncasecmp" (func $strncasecmp (param i32 i32 i32) (result i32)))
  (import "libc" "strchr" (func $strchr (param i32 i32) (result i32)))
  (import "libc" "strchrnul" (func $strchrnul (param i32 i32) (result i32)))
  (import "libc" "strrchr" (func $strrchr (param i32 i32) (result i32)))
  (import "libc" "strpbrk" (func $strpbrk (param i32 i32) (result i32)))
  (import "libc" "strstr" (func $strstr (param i32 i32) (result i32)))
  (import "libc" "strcasestr" (func $strcasestr (param i32 i32) (result i32)))
  (import "libc" "strdup" (func $strdup (param i32) (result i32)))
  (import "libc" "memcpy" (func $memcpy (param i32 i32 i32) (result i32)))
  (import "libc" "memmove" (func $memmove (param i32 i32 i32) (result i32)))
  (import "libc" "memset" (func $memset (param i32 i32 i32) (result i32)))
  (import "libc" "memchr" (func $memchr (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (import "libc" "strtol" (func $strtol (param i32 i32 i32) (result i32)))
  (import "libc" "strtoimax" (func $strtoimax (param i32 i32 i32) (result i64)))
  (import "libc" "strtoumax" (func $strtoumax (param i32 i32 i32) (result i64)))
  (import "libc" "atoi" (func $atoi (param i32) (result i32)))
  (import "libc" "strtod" (func $strtod (param i32 i32) (result f64)))
  (import "libc" "strtold" (func $strtold (param i32 i32 i32)))
  (import "libc" "__floatditf" (func $floatditf (param i32 i64)))
  (import "libc" "__multi3" (func $multi3 (param i32 i64 i64 i64 i64)))
  (import "libc" "imaxdiv" (func $imaxdiv (param i32 i64 i64)))
  (import "libc" "sh_malloc" (func $sh_malloc (param i32 i32 i32) (result i32)))
  (import "libc" "sh_realloc" (func $sh_realloc (param i32 i32 i32 i32) (result i32)))
  (import "libc" "sh_free" (func $sh_free (param i32 i32 i32)))
  (data (i32.const 180000) "Alpha beta Alpha\00alpha\00xyz\00-42\0018446744073709551615\00-12.5\002\00")
  (data (i32.const 180064) "alpha beta alpha\00")

  (func (export "strings-and-memory") (result i32)
    (local $copy i32)
    i32.const 220000 call $init drop
    i32.const 181000 i32.const 180000 call $strcpy drop
    i32.const 181000 i32.const 180011 call $strcat drop
    i32.const 181000 call $strlen i32.const 21 i32.eq
    i32.const 181000 i32.const 5 call $strnlen i32.const 5 i32.eq i32.and
    i32.const 180000 i32.const 180064 call $strcasecmp i32.eqz i32.and
    i32.const 180000 i32.const 180017 i32.const 5 call $strncasecmp i32.eqz i32.and
    i32.const 180000 i32.const 180000 call $strcmp i32.eqz i32.and
    i32.const 180000 i32.const 180000 i32.const 5 call $strncmp i32.eqz i32.and
    i32.const 180000 i32.const 98 call $strchr i32.const 180006 i32.eq i32.and
    i32.const 180000 i32.const 90 call $strchrnul i32.const 180016 i32.eq i32.and
    i32.const 180000 i32.const 65 call $strrchr i32.const 180011 i32.eq i32.and
    i32.const 180000 i32.const 180017 call $strpbrk i32.const 180001 i32.eq i32.and
    i32.const 180000 i32.const 180011 call $strstr i32.const 180000 i32.eq i32.and
    i32.const 180000 i32.const 180017 call $strcasestr i32.const 180000 i32.eq i32.and
    i32.const 180000 call $strdup local.tee $copy i32.const 180000 i32.const 17 call $memcmp i32.eqz i32.and
    i32.const 181100 i32.const 180000 i32.const 17 call $memcpy drop
    i32.const 181101 i32.const 181100 i32.const 16 call $memmove drop
    i32.const 181200 i32.const 65 i32.const 8 call $memset drop
    i32.const 181200 i32.const 65 i32.const 8 call $memchr i32.const 181200 i32.eq i32.and
    i32.const 181300 i32.const 180000 i32.const 5 call $strncpy drop)

  (func (export "numbers-and-compiler-runtime") (result i32)
    (local $pointer i32)
    i32.const 220000 call $init drop
    i32.const 180027 i32.const 0 i32.const 10 call $strtol i32.const -42 i32.eq
    i32.const 180027 call $atoi i32.const -42 i32.eq i32.and
    i32.const 180027 i32.const 0 i32.const 10 call $strtoimax i64.const -42 i64.eq i32.and
    i32.const 180031 i32.const 0 i32.const 10 call $strtoumax i64.const -1 i64.eq i32.and
    i32.const 180052 i32.const 0 call $strtod f64.const -12.5 f64.eq i32.and
    i32.const 181000 i32.const 180058 i32.const 0 call $strtold
    i32.const 181008 i64.load i64.const 0x4000000000000000 i64.eq i32.and
    i32.const 181016 i64.const 2 call $floatditf
    i32.const 181024 i64.load i64.const 0x4000000000000000 i64.eq i32.and
    i32.const 181032 i64.const 3 i64.const 0 i64.const 5 i64.const 0 call $multi3
    i32.const 181032 i64.load i64.const 15 i64.eq i32.and
    i32.const 181048 i64.const 17 i64.const 5 call $imaxdiv
    i32.const 181048 i64.load i64.const 3 i64.eq i32.and
    i32.const 181056 i64.load i64.const 2 i64.eq i32.and
    i32.const 32 i32.const 0 i32.const 0 call $sh_malloc local.tee $pointer i32.eqz i32.eqz i32.and
    local.get $pointer i32.const 64 i32.const 0 i32.const 0 call $sh_realloc local.set $pointer
    local.get $pointer i32.const 0 i32.const 0 call $sh_free))
(assert_return (invoke $memory_tests "strings-and-memory") (i32.const 1))
(assert_return (invoke $memory_tests "numbers-and-compiler-runtime") (i32.const 1))
