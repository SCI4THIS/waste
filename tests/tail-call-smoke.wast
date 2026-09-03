(module
  (func $count (export "count") (param i64) (result i64)
    (if (result i64) (i64.eqz (local.get 0))
      (then (local.get 0))
      (else
        (return_call $count (i64.sub (local.get 0) (i64.const 1)))
      )
    )
  )

  (func $even (export "even") (param i64) (result i32)
    (if (result i32) (i64.eqz (local.get 0))
      (then (i32.const 1))
      (else
        (return_call $odd (i64.sub (local.get 0) (i64.const 1)))
      )
    )
  )

  (func $odd (param i64) (result i32)
    (if (result i32) (i64.eqz (local.get 0))
      (then (i32.const 0))
      (else
        (return_call $even (i64.sub (local.get 0) (i64.const 1)))
      )
    )
  )
)

(assert_return (invoke "count" (i64.const 10_000)) (i64.const 0))
(assert_return (invoke "even" (i64.const 10_000)) (i32.const 1))
