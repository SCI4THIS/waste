(module
  (type $nullary (func (result i32)))
  (import "host" "pause" (func $pause (result i32)))
  (import "env" "sigsetjmp" (func $setjmp (param i32) (result i32)))
  (import "env" "siglongjmp" (func $longjmp (param i32 i32)))
  ;; Non-local guest control uses a writable buffer, including its token.
  (memory 1)
  (table (export "table") 1 funcref)
  (elem (i32.const 0) $inner)

  (func $inner (result i32)
    (call $pause))

  (func (export "run") (result i32)
    (i32.add (call $inner) (i32.const 1)))

  (func (export "run_indirect") (result i32)
    (i32.add
      (call_indirect (type $nullary) (i32.const 0))
      (i32.const 2)))

  (func (export "run_jump") (result i32)
    (local $jump i32)
    (local.set $jump (call $setjmp (i32.const 64)))
    (if (result i32) (i32.eqz (local.get $jump))
      (then
        (drop (call $pause))
        (call $longjmp (i32.const 64) (i32.const 7))
        (i32.const -1))
      (else (i32.const 7))))
)
