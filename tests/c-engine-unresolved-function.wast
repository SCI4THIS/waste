;; This must fail preprocessing instead of encoding function index UINT32_MAX.
(module
  (func (export "bad")
    (call $missing))
)
