;; Run pass.wast --verbose > /tmp/wast-report.txt and pass.wast > /tmp/wast-quiet.txt first.
;; Report files must contain plain LF bytes; quiet execution writes no report.
(module
  (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "env" "close" (func $close (param i32) (result i32)))
  (memory 1)
  (data (i32.const 0) "/tmp/wast-report.txt\00")
  (data (i32.const 32) "/tmp/wast-quiet.txt\00")
  (data (i32.const 128) "\2e\2e\2e\0a\57\41\53\54\3a\20\33\20\50\41\53\53\2c\20\30\20\46\41\49\4c\2c\20\33\20\74\6f\74\61\6c\0a")
  (func (export "report-bytes") (result i32)
    (local $fd i32) (local $count i32) (local $index i32)
    i32.const 0 i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 512 i32.const 128 call $read local.set $count
    local.get $fd call $close drop
    local.get $count i32.const 34 i32.ne if i32.const 0 return end
    loop $bytes
      local.get $index i32.const 128 i32.add i32.load8_u
      local.get $index i32.const 512 i32.add i32.load8_u
      i32.ne if i32.const 0 return end
      local.get $index i32.const 1 i32.add local.tee $index
      i32.const 34 i32.lt_u br_if $bytes
    end
    i32.const 1)
  (func (export "quiet-file") (result i32)
    (local $fd i32) (local $count i32)
    i32.const 32 i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s if i32.const 0 return end
    local.get $fd i32.const 512 i32.const 128 call $read local.set $count
    local.get $fd call $close drop
    local.get $count i32.eqz))
(assert_return (invoke "report-bytes") (i32.const 1))
(assert_return (invoke "quiet-file") (i32.const 1))
