;; Actual production seed reader, original native stream comparison and ownership.
(module $seed
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "memcmp" (func $compare (param i32 i32 i32) (result i32)))
  (global $bytes (mut i32) (i32.const 0))
  (data (i32.const 180000) "/tmp/seed-file.bin\00")
  (data (i32.const 180064) "/tmp/seed-file-reference.bin\00")
  (data (i32.const 180128) "/tmp/seed-file-status.txt\00")
  (data (i32.const 210000) "old-seed")
  (data (i32.const 210064) "")
  (data (i32.const 210128) "seed")
  (data (i32.const 210192) "  leading \09 trailing \0d")
  (data (i32.const 210256) "ab\00cd")
  (data (i32.const 210320) "last-line")
  (data (i32.const 210384) "")
  (func $capture (param $path i32) (param $buffer i32) (result i32)
    (local $fd i32) (local $got i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const -1 return end
    local.get $fd local.get $buffer i32.const 8192 call $read local.set $got
    local.get $fd call $close drop local.get $got)
  (func (export "load") (result i32)
    i32.const 180000 i32.const 184000 call $capture global.set $bytes
    global.get $bytes i32.const 4 i32.ge_s)
  (func (export "count") (result i32) i32.const 184000 i32.load)
  (func $length (param $index i32) (result i32)
    i32.const 184008 local.get $index i32.const 8 i32.mul i32.add i32.load)
  (func $position (param $index i32) (result i32)
    (local $count i32) (local $offset i32) (local $at i32) (local $len i32)
    global.get $bytes i32.const 4 i32.lt_s if i32.const 0 return end
    i32.const 184000 i32.load local.tee $count i32.const 64 i32.gt_u
    if i32.const 0 return end
    local.get $index local.get $count i32.ge_u if i32.const 0 return end
    i32.const 4 local.get $count i32.const 8 i32.mul i32.add local.tee $offset
    global.get $bytes i32.gt_u if i32.const 0 return end
    loop $items
      local.get $at call $length local.set $len
      local.get $len global.get $bytes local.get $offset i32.sub i32.gt_u
      if i32.const 0 return end
      local.get $at local.get $index i32.eq
      if i32.const 184000 local.get $offset i32.add return end
      local.get $offset local.get $len i32.add local.set $offset
      local.get $at i32.const 1 i32.add local.set $at br $items
    end i32.const 0)
  (func (export "opened") (param $index i32) (result i32)
    local.get $index call $position i32.eqz if i32.const -1 return end
    i32.const 184004 local.get $index i32.const 8 i32.mul i32.add i32.load)
  (func (export "text") (param $index i32) (param $expected i32) (param $count i32) (result i32)
    (local $pos i32)
    local.get $index call $position local.tee $pos i32.eqz if i32.const 0 return end
    local.get $index call $length local.get $count i32.ne if i32.const 0 return end
    local.get $pos local.get $expected local.get $count call $compare i32.eqz)
  (func (export "long-line") (result i32)
    (local $pos i32) (local $at i32)
    i32.const 7 call $position local.tee $pos i32.eqz if i32.const 0 return end
    i32.const 7 call $length i32.const 4097 i32.ne if i32.const 0 return end
    block $done loop $scan
      local.get $at i32.const 4097 i32.ge_u br_if $done
      local.get $pos local.get $at i32.add i32.load8_u i32.const 76 i32.ne
      if i32.const 0 return end
      local.get $at i32.const 1 i32.add local.set $at br $scan
    end end i32.const 1)
  (func (export "native-reference") (result i32)
    global.get $bytes i32.const 4 i32.lt_s if i32.const 0 return end
    i32.const 180064 i32.const 196000 call $capture global.get $bytes i32.ne
    if i32.const 0 return end
    i32.const 184000 i32.const 196000 global.get $bytes call $compare i32.eqz)
  (func (export "status") (result i32)
    i32.const 180128 i32.const 220000 call $capture i32.const 3 i32.eq
    i32.const 220000 i32.load16_u i32.const 0x3633 i32.eq i32.and
    i32.const 220002 i32.load8_u i32.const 10 i32.eq i32.and))
(assert_return (invoke "load") (i32.const 1))
(assert_return (invoke "count") (i32.const 8))
(assert_return (invoke "opened" (i32.const 0)) (i32.const 0))
(assert_return (invoke "text" (i32.const 0) (i32.const 210000) (i32.const 8)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 1)) (i32.const 1))
(assert_return (invoke "text" (i32.const 1) (i32.const 210064) (i32.const 0)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 2)) (i32.const 1))
(assert_return (invoke "text" (i32.const 2) (i32.const 210128) (i32.const 4)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 3)) (i32.const 1))
(assert_return (invoke "text" (i32.const 3) (i32.const 210192) (i32.const 22)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 4)) (i32.const 1))
(assert_return (invoke "text" (i32.const 4) (i32.const 210256) (i32.const 5)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 5)) (i32.const 1))
(assert_return (invoke "text" (i32.const 5) (i32.const 210320) (i32.const 9)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 6)) (i32.const 1))
(assert_return (invoke "text" (i32.const 6) (i32.const 210384) (i32.const 0)) (i32.const 1))
(assert_return (invoke "opened" (i32.const 7)) (i32.const 1))
(assert_return (invoke "long-line") (i32.const 1))
(assert_return (invoke "native-reference") (i32.const 1))
(assert_return (invoke "status") (i32.const 1))
