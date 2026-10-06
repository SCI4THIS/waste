;; Run in bash.html: wast /root/waste/tests/render/terminal.wast
;; Then: download /tmp/terminal-renderer-results.json
;; The DOM adapter performs real WebGL pixel checks. This guest writes the JSON
;; even when those checks fail, then asserts the returned pass flag.
(module
  (import "waste_kernel" "render_test_v1"
    (func $render (param i32 i32) (result i32)))
  (import "waste_kernel" "open_v1" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "write" (func $write (param i32 i32 i32) (result i32)))
  (import "env" "close" (func $close (param i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "__errno_location") (result i32) (i32.const 0))
  (data (i32.const 64) "/tmp/terminal-renderer-results.json\00")
  (data (i32.const 128) "GLF: Il1| O0 []{}() /\5c\0a!\22#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\5c]^_`\0aabcdefghijklmnopqrstuvwxyz{|}~\0a\1b[38;2;0;255;0mGREEN\1b[0m \1b[48;2;255;0;0m RED \1b[48;2;0;0;255m BLUE \1b[0m\0a")
  (global $passed (mut i32) (i32.const 0))
  (func (export "run") (result i32)
    (local $capacity i32) (local $pages i32) (local $length i32)
    (local $fd i32) (local $offset i32) (local $written i32)
    (drop (call $write (i32.const 1) (i32.const 128) (i32.const 190)))
    ;; Query the shared configuration rather than pinning a reply limit here.
    (local.set $capacity (call $render (i32.const 0) (i32.const 0)))
    (if (i32.le_s (local.get $capacity) (i32.const 4))
      (then (return (i32.const -1))))
    (local.set $pages (i32.shr_u
      (i32.add (local.get $capacity) (i32.const 69631)) (i32.const 16)))
    (if (i32.eq (memory.grow (i32.sub (local.get $pages) (memory.size))) (i32.const -1))
      (then (return (i32.const -2))))
    (local.set $length (call $render (i32.const 4096) (local.get $capacity)))
    (if (i32.le_s (local.get $length) (i32.const 4))
      (then (return (i32.const -3))))
    (global.set $passed (i32.load (i32.const 4096)))
    ;; O_WRONLY | O_CREAT | O_TRUNC; ordinary kernel descriptors own the file.
    (local.set $fd (call $open (i32.const 64) (i32.const 577) (i32.const 420)))
    (if (i32.lt_s (local.get $fd) (i32.const 0))
      (then (return (i32.sub (i32.const -100) (i32.load (i32.const 0))))))
    (local.set $offset (i32.const 4100))
    (local.set $length (i32.sub (local.get $length) (i32.const 4)))
    (loop $write_all
      (local.set $written (call $write (local.get $fd) (local.get $offset) (local.get $length)))
      (if (i32.le_s (local.get $written) (i32.const 0))
        (then (drop (call $close (local.get $fd))) (return (i32.const -5))))
      (local.set $offset (i32.add (local.get $offset) (local.get $written)))
      (local.set $length (i32.sub (local.get $length) (local.get $written)))
      (br_if $write_all (local.get $length)))
    (call $close (local.get $fd)))
  (func (export "render_passed") (result i32) (global.get $passed)))
(assert_return (invoke "run") (i32.const 0))
(assert_return (invoke "render_passed") (i32.const 1))
