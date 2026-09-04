(module
  (func (export "init")
    (table.init $segment $table (i32.const 0) (i32.const 0) (i32.const 0)))
  (table $table 1 funcref)
  (func $target)
  (elem $segment funcref (ref.func $target))
)
