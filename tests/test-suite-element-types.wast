;; Bare funcidx segments declare (ref func); funcref stays nullable.
(module
  (type $t (func (result i32)))
  (func $f (type $t) (i32.const 7))
  (func $g (type $t) (i32.const 19))
  (table $tab 4 (ref func) (ref.func $f))
  (elem (i32.const 1) $g)
  (elem (i32.const 2) func $g)
  (elem (table $tab) (i32.const 3) func $g)
  (elem (i32.const 4) func)
  (elem (i32.const 4))
  (elem $passive func $g)
  (elem $declared declare func $g)
  (elem $empty func)
  (elem $empty_declared declare func)
  (func (export "call") (param i32) (result i32)
    (call_indirect (type $t) (local.get 0)))
  (func (export "init") (table.init $tab $passive (i32.const 0) (i32.const 0) (i32.const 1)))
  (func (export "active") (table.init 0 (i32.const 0) (i32.const 0) (i32.const 1)))
  (func (export "declared") (table.init $tab $declared (i32.const 0) (i32.const 0) (i32.const 1)))
  (func (export "drop") (elem.drop $passive))
  (func (export "empty")
    (table.init $tab $empty (i32.const 0) (i32.const 0) (i32.const 0))
    (table.init $tab $empty_declared (i32.const 0) (i32.const 0) (i32.const 0)))
)
(assert_return (invoke "empty"))
(assert_return (invoke "call" (i32.const 0)) (i32.const 7))
(assert_return (invoke "call" (i32.const 1)) (i32.const 19))
(assert_return (invoke "call" (i32.const 2)) (i32.const 19))
(assert_return (invoke "call" (i32.const 3)) (i32.const 19))
(assert_return (invoke "init"))
(assert_return (invoke "call" (i32.const 0)) (i32.const 19))
(assert_trap (invoke "active") "out of bounds table access")
(assert_trap (invoke "declared") "out of bounds table access")
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")

;; Shorthand inherits the declared nullable table type, including null entries.
(module
  (func $f (result i32) (i32.const 7))
  (func $g (result i32) (i32.const 19))
  (table $expressions funcref (elem (ref.func $f) (ref.null func) (ref.func $g)))
  (table $indices funcref (elem $g $f))
  (func (export "expression") (param i32) (result i32)
    (call_indirect $expressions (result i32) (local.get 0)))
  (func (export "index") (param i32) (result i32)
    (call_indirect $indices (result i32) (local.get 0)))
)
(assert_return (invoke "expression" (i32.const 0)) (i32.const 7))
(assert_trap (invoke "expression" (i32.const 1)) "uninitialized element")
(assert_return (invoke "expression" (i32.const 2)) (i32.const 19))
(assert_return (invoke "index" (i32.const 0)) (i32.const 19))
(assert_return (invoke "index" (i32.const 1)) (i32.const 7))

;; Explicit nullable types must not be narrowed by inspecting their items.
(assert_invalid (module
  (func $f) (table 1 (ref func) (ref.func $f))
  (elem (i32.const 0) funcref (ref.func $f))) "type mismatch")
(assert_invalid (module
  (func $f) (table 1 (ref func) (ref.func $f))
  (elem (i32.const 0) funcref)) "type mismatch")
(assert_invalid (module
  (func $f) (table 1 (ref func) (ref.func $f))
  (elem (i32.const 0) funcref (ref.null func))) "type mismatch")
(assert_invalid (module
  (func $f) (table 1 (ref func) (ref.func $f)) (elem $e funcref (ref.func $f))
  (func (table.init $e (i32.const 0) (i32.const 0) (i32.const 1)))) "type mismatch")
(assert_invalid (module
  (func $f) (table 1 (ref func) (ref.func $f)) (elem $e declare funcref (ref.func $f))
  (func (table.init $e (i32.const 0) (i32.const 0) (i32.const 0)))) "type mismatch")
(assert_invalid (module
  (func $f) (table 1 (ref func) (ref.func $f))
  (elem (i32.const 0) (ref func) (ref.null func))) "type mismatch")

;; Address width is independent of element nullability.
(module
  (type $t (func (result i32)))
  (func $f (type $t) (i32.const 7))
  (func $g (type $t) (i32.const 19))
  (table $tab i64 2 (ref func) (ref.func $f))
  (elem (table $tab) (i64.const 1) func $g)
  (elem $e func $g)
  (table $short i64 funcref (elem $f $g))
  (func (export "call") (param i64) (result i32)
    (call_indirect $tab (type $t) (local.get 0)))
  (func (export "short") (param i64) (result i32)
    (call_indirect $short (type $t) (local.get 0)))
  (func (export "init") (table.init $tab $e (i64.const 0) (i32.const 0) (i32.const 1)))
)
(assert_return (invoke "call" (i64.const 0)) (i32.const 7))
(assert_return (invoke "call" (i64.const 1)) (i32.const 19))
(assert_return (invoke "init"))
(assert_return (invoke "call" (i64.const 0)) (i32.const 19))
(assert_return (invoke "short" (i64.const 0)) (i32.const 7))
(assert_return (invoke "short" (i64.const 1)) (i32.const 19))

;; Binary modes 0-3 imply (ref func); mode 4 implies nullable funcref.
;; The table initially returns 7; segment contents return 19.
;; Element mode 0: funcidx vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\07"
  "\01\00\41\00\0b\01\01\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41\00"
  "\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b\0c"
  "\00\41\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 19))
(assert_trap (invoke "init") "out of bounds table access")
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 1: funcidx vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\05"
  "\01\01\00\01\01\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41\00\11\00"
  "\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b\0c\00\41"
  "\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 7))
(assert_return (invoke "init"))
(assert_return (invoke "call") (i32.const 19))
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 2: funcidx vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\09"
  "\01\02\00\41\00\0b\00\01\01\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00"
  "\41\00\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00"
  "\0b\0c\00\41\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 19))
(assert_trap (invoke "init") "out of bounds table access")
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 3: funcidx vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\05"
  "\01\03\00\01\01\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41\00\11\00"
  "\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b\0c\00\41"
  "\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 7))
(assert_trap (invoke "init") "out of bounds table access")
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 4: expression vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\09\01\40\00\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00\02"
  "\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\09\01"
  "\04\41\00\0b\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41"
  "\00\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b"
  "\0c\00\41\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 19))
(assert_trap (invoke "init") "out of bounds table access")
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 5: expression vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\08"
  "\01\05\64\70\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41"
  "\00\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b"
  "\0c\00\41\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 7))
(assert_return (invoke "init"))
(assert_return (invoke "call") (i32.const 19))
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 6: expression vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\0c"
  "\01\06\00\41\00\0b\64\70\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13"
  "\0b\07\00\41\00\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00"
  "\fc\0d\00\0b\0c\00\41\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 19))
(assert_trap (invoke "init") "out of bounds table access")
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Element mode 7: expression vector.
(module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\08"
  "\01\07\64\70\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41"
  "\00\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b"
  "\0c\00\41\00\41\00\41\00\fc\0c\00\00\0b"
)
(assert_return (invoke "call") (i32.const 7))
(assert_trap (invoke "init") "out of bounds table access")
(assert_return (invoke "zero"))
(assert_return (invoke "drop"))
(assert_return (invoke "drop"))
(assert_trap (invoke "init") "out of bounds table access")
;; Explicit nullable binary mode 5 cannot target a non-null table.
(assert_invalid (module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\07"
  "\01\05\70\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41\00"
  "\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b\0c"
  "\00\41\00\41\00\41\00\fc\0c\00\00\0b"
) "type mismatch")
;; Explicit nullable binary mode 6 cannot target a non-null table.
(assert_invalid (module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\0b"
  "\01\06\00\41\00\0b\70\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13\0b"
  "\07\00\41\00\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc"
  "\0d\00\0b\0c\00\41\00\41\00\41\00\fc\0c\00\00\0b"
) "type mismatch")
;; Explicit nullable binary mode 7 cannot target a non-null table.
(assert_invalid (module binary
  "\00\61\73\6d\01\00\00\00\01\08\02\60\00\01\7f\60\00\00\03\07\06\00\00\00"
  "\01\01\01\04\0a\01\40\00\64\70\00\01\d2\00\0b\07\1d\04\04\63\61\6c\6c\00"
  "\02\04\69\6e\69\74\00\03\04\64\72\6f\70\00\04\04\7a\65\72\6f\00\05\09\07"
  "\01\07\70\01\d2\01\0b\0a\33\06\04\00\41\07\0b\04\00\41\13\0b\07\00\41\00"
  "\11\00\00\0b\0c\00\41\00\41\00\41\01\fc\0c\00\00\0b\05\00\fc\0d\00\0b\0c"
  "\00\41\00\41\00\41\00\fc\0c\00\00\0b"
) "type mismatch")
