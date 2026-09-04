;; Unknown named types must be rejected rather than silently inferred.
(module
  (func (type $missing) (export "bad"))
)
