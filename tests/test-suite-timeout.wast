(module (func (export "loop") (loop $again (br $again))))
(assert_return (invoke "loop"))
