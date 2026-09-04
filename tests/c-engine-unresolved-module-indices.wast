;; Every unresolved module index must stop preprocessing.
(module
  (export "bad" (memory $missing-memory))
  (func)
)
