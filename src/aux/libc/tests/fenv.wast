;; Shared libc's immutable Wasm floating-point environment, not host fenv.
(module $fenv
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "fegetround" (func $round (result i32)))
  (import "libc" "fesetround" (func $set_round (param i32) (result i32)))
  (import "libc" "feclearexcept" (func $clear (param i32) (result i32)))
  (import "libc" "feraiseexcept" (func $raise (param i32) (result i32)))
  (import "libc" "fetestexcept" (func $test (param i32) (result i32)))
  (import "libc" "fegetexceptflag" (func $get_flags (param i32 i32) (result i32)))
  (import "libc" "fesetexceptflag" (func $set_flags (param i32 i32) (result i32)))
  (import "libc" "fegetenv" (func $get_env (param i32) (result i32)))
  (import "libc" "feholdexcept" (func $hold (param i32) (result i32)))
  (import "libc" "fesetenv" (func $set_env (param i32) (result i32)))
  (import "libc" "feupdateenv" (func $update (param i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (func (export "nearest-only") (result i32)
    call $round i32.eqz
    i32.const 0 call $set_round i32.eqz i32.and
    i32.const 1 call $set_round i32.const -1 i32.eq i32.and
    i32.const 0x400 call $set_round i32.const -1 i32.eq i32.and
    i32.const 0x800 call $set_round i32.const -1 i32.eq i32.and
    i32.const 0xc00 call $set_round i32.const -1 i32.eq i32.and
    call $round i32.eqz i32.and)
  (func (export "arithmetic-ties-to-even") (result i32)
    f32.const 1 f32.const 0x1p-24 f32.add f32.const 1 f32.eq
    f32.const 0x1.000002p0 f32.const 0x1p-24 f32.add f32.const 0x1.000004p0 f32.eq i32.and
    f32.const -1 f32.const -0x1p-24 f32.add f32.const -1 f32.eq i32.and
    f64.const 1 f64.const 0x1p-53 f64.add f64.const 1 f64.eq i32.and
    f64.const 0x1.0000000000001p0 f64.const 0x1p-53 f64.add
    f64.const 0x1.0000000000002p0 f64.eq i32.and
    f32.const 2.5 f32.nearest f32.const 2 f32.eq i32.and)
  (func (export "empty-exception-set") (result i32)
    i32.const 0 call $clear i32.eqz
    i32.const 0 call $raise i32.eqz i32.and
    i32.const 0 call $test i32.eqz i32.and
    i32.const 1 call $clear i32.const -1 i32.eq i32.and
    i32.const 1 call $raise i32.const -1 i32.eq i32.and
    i32.const 1 call $test i32.const -1 i32.eq i32.and)
  (func (export "operations-do-not-create-flags") (result i32)
    (local $nan f64)
    f64.const 1 f64.const 0 f64.div f64.const inf f64.eq
    f64.const 0 f64.const 0 f64.div local.set $nan
    local.get $nan local.get $nan f64.ne i32.and
    f64.const 0x1p1023 f64.const 0x1p1023 f64.mul f64.const inf f64.eq i32.and
    f64.const 0x1p-1022 f64.const 0x1p-1022 f64.mul f64.const 0 f64.eq i32.and
    i32.const 0 call $test i32.eqz i32.and)
  (func (export "save-hold-and-restore") (result i32)
    i32.const 180000 i32.const 0x12345678 i32.store
    i32.const 180004 i32.const -1 i32.store
    i32.const 180008 i32.const 0x12345678 i32.store
    i32.const 180004 call $get_env i32.eqz
    i32.const 180004 i32.load i32.eqz i32.and
    i32.const 180004 call $set_env i32.eqz i32.and
    i32.const 180004 i32.const -1 i32.store
    i32.const 180004 call $hold i32.eqz i32.and
    i32.const 180004 i32.load i32.eqz i32.and
    i32.const 180004 call $update i32.eqz i32.and
    i32.const 180000 i32.load i32.const 0x12345678 i32.eq i32.and
    i32.const 180008 i32.load i32.const 0x12345678 i32.eq i32.and)
  (func (export "default-environment-sentinel") (result i32)
    i32.const -1 call $set_env i32.eqz
    i32.const -1 call $update i32.eqz i32.and
    call $round i32.eqz i32.and)
  (func (export "flag-snapshot-and-canaries") (result i32)
    i32.const 180016 i32.const 0x12345678 i32.store
    i32.const 180020 i32.const -1 i32.store
    i32.const 180024 i32.const 0x12345678 i32.store
    i32.const 180020 i32.const 0 call $get_flags i32.eqz
    i32.const 180020 i32.load i32.eqz i32.and
    i32.const 180020 i32.const 0 call $set_flags i32.eqz i32.and
    i32.const 180020 i32.const 0x87654321 i32.store
    i32.const 180020 i32.const 1 call $get_flags i32.const -1 i32.eq i32.and
    i32.const 180020 i32.load i32.const 0x87654321 i32.eq i32.and
    i32.const 180016 i32.load i32.const 0x12345678 i32.eq i32.and
    i32.const 180024 i32.load i32.const 0x12345678 i32.eq i32.and)
  (func (export "null-and-invalid-snapshots") (result i32)
    i32.const 0 call $get_env i32.const -1 i32.eq
    i32.const 0 call $hold i32.const -1 i32.eq i32.and
    i32.const 0 call $set_env i32.const -1 i32.eq i32.and
    i32.const 0 call $update i32.const -1 i32.eq i32.and
    i32.const 0 i32.const 0 call $get_flags i32.const -1 i32.eq i32.and
    i32.const 0 i32.const 0 call $set_flags i32.const -1 i32.eq i32.and
    i32.const 180032 i32.const 0x400 i32.store
    i32.const 180032 call $set_env i32.const -1 i32.eq i32.and
    i32.const 180032 call $update i32.const -1 i32.eq i32.and
    i32.const 180032 i32.const 0 call $set_flags i32.const -1 i32.eq i32.and
    i32.const 180032 i32.load i32.const 0x400 i32.eq i32.and
    call $round i32.eqz i32.and)
  (func (export "errno-preserved") (result i32)
    call $errno i32.const 17 i32.store
    i32.const 1 call $set_round drop
    i32.const 1 call $raise drop
    i32.const 1 call $clear drop
    i32.const 1 call $test drop
    i32.const 0 call $set_env drop
    i32.const 180004 call $get_env drop
    i32.const 180004 call $hold drop
    i32.const 180004 call $update drop
    i32.const -1 call $set_env drop
    i32.const 180020 i32.const 0 call $get_flags drop
    i32.const 180020 i32.const 0 call $set_flags drop
    call $errno i32.load i32.const 17 i32.eq))
(assert_return (invoke $fenv "nearest-only") (i32.const 1))
(assert_return (invoke $fenv "arithmetic-ties-to-even") (i32.const 1))
(assert_return (invoke $fenv "empty-exception-set") (i32.const 1))
(assert_return (invoke $fenv "operations-do-not-create-flags") (i32.const 1))
(assert_return (invoke $fenv "save-hold-and-restore") (i32.const 1))
(assert_return (invoke $fenv "default-environment-sentinel") (i32.const 1))
(assert_return (invoke $fenv "flag-snapshot-and-canaries") (i32.const 1))
(assert_return (invoke $fenv "null-and-invalid-snapshots") (i32.const 1))
(assert_return (invoke $fenv "errno-preserved") (i32.const 1))
