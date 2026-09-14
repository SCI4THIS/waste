;; Inline module fields retain their original source coordinates.
(; leading (; nested ;) block comment ;)
(@ignored "module annotation")
(func (export "value") (result f32)
  f32.const 16777217)
(memory 1)
(data (i32.const 0) "A\00B")
