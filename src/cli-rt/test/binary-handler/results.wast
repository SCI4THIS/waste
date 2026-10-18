;; /bin/wasm reads a non-executable binary, forwards argv, and rejects bad input.
(module $binary
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/tmp/bin-handler-output.txt\00")
  (data (i32.const 180064) "/tmp/bin-handler-status.txt\00")
  (data (i32.const 180128) "/tmp/bin-handler-format-status.txt\00")
  (data (i32.const 180192) "/tmp/bin-handler-usage-status.txt\00")
  (data (i32.const 180256) "alpha|beta\0a")
  (data (i32.const 180288) "0\0a")
  (func $capture (param $path i32) (result i32)
    (local $fd i32) (local $size i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd i32.const 184000 i32.const 64 call $read local.set $size
    local.get $fd call $close drop local.get $size)
  (func $matches (param $path i32) (param $expected i32) (param $size i32) (result i32)
    local.get $path call $capture local.get $size i32.eq
    i32.const 184000 local.get $expected local.get $size call $memcmp i32.eqz i32.and)
  (func (export "argv") (result i32)
    i32.const 180000 i32.const 180256 i32.const 11 call $matches)
  (func (export "exit") (result i32)
    i32.const 180064 i32.const 180288 i32.const 2 call $matches)
  (func $nonzero (param $path i32) (result i32)
    (local $size i32) (local $index i32) (local $digit i32) (local $value i32)
    local.get $path call $capture local.tee $size i32.const 2 i32.lt_s
    if i32.const 0 return end
    local.get $size i32.const 4 i32.gt_s if i32.const 0 return end
    i32.const 184000 local.get $size i32.add i32.const 1 i32.sub i32.load8_u
    i32.const 10 i32.ne if i32.const 0 return end
    block $done loop $digits
      local.get $index local.get $size i32.const 1 i32.sub i32.ge_u br_if $done
      i32.const 184000 local.get $index i32.add i32.load8_u
      i32.const 48 i32.sub local.tee $digit i32.const 9 i32.gt_u
      if i32.const 0 return end
      local.get $value i32.const 10 i32.mul local.get $digit i32.add local.set $value
      local.get $index i32.const 1 i32.add local.set $index br $digits
    end end
    local.get $value i32.const 0 i32.gt_u
    local.get $value i32.const 255 i32.le_u i32.and)
  (func (export "reject-text") (result i32) i32.const 180128 call $nonzero)
  (func (export "require-file") (result i32) i32.const 180192 call $nonzero))
(assert_return (invoke $binary "argv") (i32.const 1))
(assert_return (invoke $binary "exit") (i32.const 1))
(assert_return (invoke $binary "reject-text") (i32.const 1))
(assert_return (invoke $binary "require-file") (i32.const 1))
