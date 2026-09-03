(module
  (type $counter (func (param i64) (result i64)))

  (func $direct (export "direct") (type $counter) (param i64) (result i64)
    local.get 0
    i64.eqz
    if (result i64)
      local.get 0
    else
      local.get 0
      i64.const 1
      i64.sub
      return_call $direct
    end)

  (func $reference (export "reference") (type $counter) (param i64) (result i64)
    local.get 0
    i64.eqz
    if (result i64)
      local.get 0
    else
      local.get 0
      i64.const 1
      i64.sub
      ref.func $reference
      return_call_ref $counter
    end)

  (elem declare func $reference))
