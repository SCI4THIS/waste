;; Quoted module compilation failure must not vanish before any assertions.
(module quote "(module (func call $absent))")
