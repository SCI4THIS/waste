;; Function envelopes must be complete before measuring or executing them.
;; Trailing instruction after function end
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\04\01\60\00\00\03\02\01\00\0a\05\01\03\00\0b\01") "function envelope")
;; Trailing second end
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\04\01\60\00\00\03\02\01\00\0a\05\01\03\00\0b\0b") "function envelope")
;; Missing function end
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\04\01\60\00\00\03\02\01\00\0a\04\01\02\00\01") "function envelope")
;; Nested block closes without function end
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\04\01\60\00\00\03\02\01\00\0a\06\01\04\00\02\40\0b") "function envelope")
;; Open block at end of body
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\04\01\60\00\00\03\02\01\00\0a\06\01\04\00\02\40\01") "function envelope")
(module (func (export "ok") (result i32) i32.const 42))
(assert_return (invoke "ok") (i32.const 42))

;; Inspection must not change normal validation or start-function execution.
(assert_invalid (module (func drop)) "type mismatch")
(assert_trap (module (func $start (unreachable)) (start $start)) "unreachable")
