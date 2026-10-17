;; Demand-allocated declared-local types: boundaries, groups, cleanup and calls.
(module $calls
  (func $empty (result i32) i32.const 7)
  (func $child (param i32) (result i32) (local i32)
    local.get 0 local.set 1 local.get 1 i32.const 1 i32.add)
  (func (export "nested") (result i32) (local i32 i64 f32 f64 funcref)
    call $empty call $child local.set 0 local.get 0)
  (func (export "zero") (result i64) (local i64) local.get 0))
(assert_return (invoke $calls "nested") (i32.const 8))
(assert_return (invoke $calls "zero") (i64.const 0))

;; no locals
(module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\06\01\04\00\41\2a\0b")
(assert_return (invoke "f") (i32.const 42))

;; empty groups around mixed types
(module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\18\01\16\05\00\7f\01\7e\00\7c\01\7f\00\70\42\09\21\00\41\2a\21\01\20\01\0b")
(assert_return (invoke "f") (i32.const 42))

;; maximum declared locals
(module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\0f\01\0d\01\80\10\7f\41\2a\21\ff\0f\20\ff\0f\0b")
(assert_return (invoke "f") (i32.const 42))

;; parameters share the frame limit
(module binary "\00\61\73\6d\01\00\00\00\01\06\01\60\01\7f\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\0f\01\0d\01\ff\0f\7f\20\00\21\ff\0f\20\ff\0f\0b")
(assert_return (invoke "f" (i32.const 17)) (i32.const 17))

;; oversized single group
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\09\01\07\01\81\10\7f\41\00\0b") "local declaration or body")

;; cumulative groups
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\0b\01\09\02\80\10\7f\01\7e\41\00\0b") "local declaration or body")

;; u32 group overflow
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\0c\01\0a\01\ff\ff\ff\ff\0f\7f\41\00\0b") "local declaration or body")

;; parameters plus locals exceed frame
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\06\01\60\01\7f\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\09\01\07\01\80\10\7f\41\00\0b") "local declaration or body")

;; invalid zero-count type
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\08\01\06\01\00\ff\41\00\0b") "local declaration or body")

;; missing group count
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\03\01\01\80") "local declaration or body")

;; truncated group
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\04\01\02\01\01") "local declaration or body")

;; truncated body after allocated locals
(assert_malformed (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\06\01\04\01\01\7f\41") "local declaration or body")

;; unknown local after allocation
(assert_invalid (module binary "\00\61\73\6d\01\00\00\00\01\05\01\60\00\01\7f\03\02\01\00\07\05\01\01\66\00\00\0a\08\01\06\01\01\7f\20\01\0b") "local declaration or body")

;; A failed load must not poison the next instance.
(module (func (export "f") (result i32) (local i32) i32.const 42 local.set 0 local.get 0))
(assert_return (invoke "f") (i32.const 42))
