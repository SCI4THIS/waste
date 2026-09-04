(module
  (type $signature (func))
  (func $f)
  (global (export "func-null") (ref null func) (ref.null func))
  (global (export "func") (ref func) (ref.func $f))
  (global (export "typed-null") (ref null $signature) (ref.null $signature))
  (global (export "typed") (ref $signature) (ref.func $f))
  (global (export "extern-null") (ref null extern) (ref.null extern)))
