;; Unnamed-module (get "name") after cross-module import.
;; linking.wast always uses (get $module "name") with an explicit module id.
(module $provider
  (global (export "constant") i32 (i32.const 42)))
(register "globals" $provider)

(module
  (global (import "globals" "constant") i32)
  (export "imported" (global 0)))
(assert_return (get "imported") (i32.const 42))
