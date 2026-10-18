(module
  (memory (export "memory") 2)
  (func (export "_start") (loop $again (br $again))))
