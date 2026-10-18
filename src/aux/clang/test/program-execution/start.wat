;; A long module-start transaction; the exit observes its final counter.
(module
  (import "env" "_exit" (func $exit (param i32)))
  (global $counter (mut i32) (i32.const 0))
  (func $initialize
    (loop $work
      global.get $counter i32.const 1 i32.add global.set $counter
      global.get $counter i32.const 80000 i32.lt_u br_if $work))
  (start $initialize)
  (func (export "_start")
    global.get $counter i32.const 255 i32.and call $exit))
