;; An interrupted start is not invalidity, malformed input, or a guest trap.
(assert_invalid (module
  (func $spin (loop $again (br $again)))
  (start $spin)) "not an invalid module")
