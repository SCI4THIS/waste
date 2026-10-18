;; Result identities must retain long export names and a leading UTF-8 BOM.
(module
  (func (export "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqr")
    (result i32) i32.const 42)
  (func (export "﻿") (result i32) i32.const 7))
(assert_return (invoke "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqr")
  (i32.const 42))
(assert_return (invoke "﻿") (i32.const 7))
