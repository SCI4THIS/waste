(module
  (memory (export "memory") 1)
  (func (export "__waste_startup") (param i32)
    (i32.const 100) (local.get 0) i32.store)
  (func (export "_start"))
)
