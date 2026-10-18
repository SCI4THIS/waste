;; Fail after the production shared library has already been instantiated.
(module (import "libc" "strlen" (func (param i32) (result i32)))
  (import "env" "memory" (memory 2)))
