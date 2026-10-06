(module
  (func $spin (loop $again (br $again)))
  (start $spin))
