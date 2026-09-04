(module
  (memory (export "memory") 1 3)
  (table (export "table") 2 4 funcref)
  (global (export "global") (mut i32) (i32.const 10))
  (func (export "read-global") (param i32 i32) (result i32) global.get 0)
  (func (export "read-memory") (param i32 i32) (result i32) local.get 0 i32.load))
