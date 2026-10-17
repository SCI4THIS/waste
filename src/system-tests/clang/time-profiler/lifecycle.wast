;; Archive-linked LLVM TimeProfiler public lifecycle. Controlled clock steps
;; are shared by native and packaged-worker drivers in lifecycle.json.
;; Sequential finished instances exercise the disabled-thread compiler profile.
(module $profiler
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "libc" "__errno_location" (func $errno (result i32)))
  (data (i32.const 180000) "/tmp/profiler-lifecycle.bin\00")
  (data (i32.const 180128) "/tmp/profiler-lifecycle-status.txt\00")
  (data (i32.const 180256) "/tmp/profiler-stream.json\00")
  (data (i32.const 180384) "/tmp/profiler-preferred.json\00")
  (data (i32.const 180512) "/tmp/profiler-fallback.time-trace.json\00")
  (data (i32.const 180640) "/root/out.time-trace.json\00")
  (data (i32.const 180768) "/tmp/profiler-threshold.json\00")
  (data (i32.const 180896) "/tmp/profiler-reuse.json\00")
  (data (i32.const 181024) "/tmp/profiler-unused\00")
  (data (i32.const 181152) "/tmp/profiler-unused.time-trace.json\00")
  (data (i32.const 181280) "/tmp/profiler-missing/trace.json\00")
  (data (i32.const 210000)
    "{\22traceEvents\22:[{\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22X\22,\22dur\22:3000,\22name\22:\22Driver\22,\22args\22:{\22detail\22:\22scope\22"
    "}},{\22pid\22:2,\22tid\22:0,\22ts\22:-9000,\22ph\22:\22X\22,\22dur\22:2000,\22name\22:\22Compile\22,\22args\22:{\22detail\22:\22nested\22}},{\22pi"
    "d\22:2,\22tid\22:0,\22ts\22:-9000,\22ph\22:\22i\22,\22name\22:\22nested instant\22,\22args\22:{\22detail\22:\22quote\5c\22\5c\5c\5cn\5ct\22}},{\22pid\22:2"
    ",\22tid\22:0,\22ts\22:-7000,\22ph\22:\22X\22,\22dur\22:1000,\22name\22:\22Parse\22,\22args\22:{\22detail\22:\22source\22,\22file\22:\22/root/a\5c\22.c"
    "\22,\22line\22:17}},{\22pid\22:2,\22tid\22:0,\22ts\22:-9000,\22ph\22:\22X\22,\22dur\22:3000,\22name\22:\22Compile\22,\22args\22:{\22detail\22:\22ddd"
    "ddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\22}},{\22pid\22:2,\22tid\22:0,\22ts\22:-6000,\22cat\22:\22"
    "Link\22,\22ph\22:\22b\22,\22id\22:0,\22name\22:\22Link\22,\22args\22:{\22detail\22:\22async\22}},{\22pid\22:2,\22tid\22:0,\22ts\22:-4000,\22cat\22:\22Li"
    "nk\22,\22ph\22:\22e\22,\22id\22:0,\22name\22:\22Link\22},{\22pid\22:2,\22tid\22:0,\22ts\22:-6000,\22ph\22:\22i\22,\22name\22:\22link instant\22,\22args\22"
    ":{\22detail\22:\22inside\22}},{\22pid\22:2,\22tid\22:0,\22ts\22:-4000,\22ph\22:\22X\22,\22dur\22:4000,\22name\22:\22Compile\22,\22args\22:{\22deta"
    "il\22:\22lazy\22}},{\22pid\22:2,\22tid\22:1,\22ph\22:\22X\22,\22ts\22:0,\22dur\22:7000,\22name\22:\22Total Compile\22,\22args\22:{\22count\22:2,\22a"
    "vg ms\22:3}},{\22pid\22:2,\22tid\22:2,\22ph\22:\22X\22,\22ts\22:0,\22dur\22:3000,\22name\22:\22Total Driver\22,\22args\22:{\22count\22:1,\22avg "
    "ms\22:3}},{\22pid\22:2,\22tid\22:3,\22ph\22:\22X\22,\22ts\22:0,\22dur\22:2000,\22name\22:\22Total Link\22,\22args\22:{\22count\22:1,\22avg ms\22:2"
    "}},{\22pid\22:2,\22tid\22:4,\22ph\22:\22X\22,\22ts\22:0,\22dur\22:1000,\22name\22:\22Total Parse\22,\22args\22:{\22count\22:1,\22avg ms\22:1}},{"
    "\22cat\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22process_name\22,\22args\22:{\22name\22:\22driver clang\22}},{\22cat"
    "\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22thread_name\22,\22args\22:{\22name\22:\22\22}},{\22cat\22:\22\22,\22pid\22:2,\22tid"
    "\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22thread_name\22,\22args\22:{\22name\22:\22\22}},{\22cat\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22"
    "M\22,\22name\22:\22thread_name\22,\22args\22:{\22name\22:\22\22}}],\22beginningOfTime\22:4294967296123456}")
  (data (i32.const 215000)
    "{\22traceEvents\22:[{\22pid\22:2,\22tid\22:1,\22ph\22:\22X\22,\22ts\22:0,\22dur\22:1000,\22name\22:\22Total Short\22,\22args\22:{\22count\22:1,\22"
    "avg ms\22:1}},{\22pid\22:2,\22tid\22:2,\22ph\22:\22X\22,\22ts\22:0,\22dur\22:0,\22name\22:\22Total Zero\22,\22args\22:{\22count\22:1,\22avg ms\22:"
    "0}},{\22cat\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22process_name\22,\22args\22:{\22name\22:\22threshold\22}},{\22c"
    "at\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22thread_name\22,\22args\22:{\22name\22:\22\22}}],\22beginningOfTime\22:4"
    "294967296123456}")
  (data (i32.const 218000)
    "{\22traceEvents\22:[{\22cat\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22process_name\22,\22args\22:{\22name\22:\22reus"
    "e\22}},{\22cat\22:\22\22,\22pid\22:2,\22tid\22:0,\22ts\22:0,\22ph\22:\22M\22,\22name\22:\22thread_name\22,\22args\22:{\22name\22:\22\22}}],\22beginningO"
    "fTime\22:4294967296123456}")
  (func $capture (param $path i32) (param $output i32) (param $count i32) (result i32)
    (local $fd i32) (local $bytes i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const 0 i32.lt_s
    if i32.const -1 return end
    local.get $fd local.get $output local.get $count call $read local.set $bytes
    local.get $fd call $close drop
    local.get $bytes)
  (func (export "capture") (result i32)
    i32.const 180000 i32.const 182000 i32.const 1024 call $capture)
  (func (export "exit-status") (result i32)
    i32.const 180128 i32.const 184000 i32.const 8 call $capture i32.const 3 i32.eq
    i32.const 184000 i32.load16_u i32.const 0x3731 i32.eq i32.and
    i32.const 184002 i32.load8_u i32.const 10 i32.eq i32.and)
  (func (export "value") (param $index i32) (result i64)
    i32.const 182000 local.get $index i32.const 8 i32.mul i32.add i64.load)
  (func (export "json") (param $path i32) (param $expected i32) (param $size i32) (result i32)
    (local $index i32)
    local.get $path i32.const 190000 i32.const 8192 call $capture
    local.get $size i32.ne if i32.const 0 return end
    block $done
      loop $bytes
        local.get $index local.get $size i32.ge_u br_if $done
        i32.const 190000 local.get $index i32.add i32.load8_u
        local.get $expected local.get $index i32.add i32.load8_u
        i32.ne if i32.const 0 return end
        local.get $index i32.const 1 i32.add local.set $index
        br $bytes
      end
    end
    i32.const 1)
  (func (export "absent") (param $path i32) (result i32)
    (local $fd i32)
    local.get $path i32.const 0 i32.const 0 call $open local.tee $fd
    i32.const -1 i32.eq
    if (result i32) call $errno i32.load i32.const 2 i32.eq
    else local.get $fd call $close drop i32.const 0 end)
)
(assert_return (invoke $profiler "capture") (i32.const 408))
(assert_return (invoke $profiler "exit-status") (i32.const 1))
;; Registration, disabled calls and lazy details do not create entries.
(assert_return (invoke $profiler "value" (i32.const 0)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 1)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 2)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 3)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 4)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 5)) (i64.const 0))
;; Initialize enables verbose tracing and owns mutated names/metadata.
(assert_return (invoke $profiler "value" (i32.const 6)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 7)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 8)) (i64.const 1))
;; Real usleep continuations precede finishing two sequential instances.
(assert_return (invoke $profiler "value" (i32.const 9)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 10)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 11)) (i64.const 0))
;; Finish disables the instance; the registry vector grows from 4 to 8 bytes.
(assert_return (invoke $profiler "value" (i32.const 12)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 13)) (i64.const 4))
(assert_return (invoke $profiler "value" (i32.const 14)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 15)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 16)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 17)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 18)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 19)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 20)) (i64.const 8))
(assert_return (invoke $profiler "value" (i32.const 21)) (i64.const 0))
;; An instant outside a section does not evaluate its lazy detail.
(assert_return (invoke $profiler "value" (i32.const 22)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 23)) (i64.const 0))
;; Raw-stream and preferred/fallback/default file writers; missing parent reports ENOENT.
(assert_return (invoke $profiler "value" (i32.const 24)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 25)) (i64.const 1680))
(assert_return (invoke $profiler "value" (i32.const 26)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 27)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 28)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 29)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 30)) (i64.const 2))
;; Cleanup clears current/finished ownership but retains vector capacity.
(assert_return (invoke $profiler "value" (i32.const 31)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 32)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 33)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 34)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 35)) (i64.const 0))
;; Threshold filters short/zero entries and their instants, retaining totals.
(assert_return (invoke $profiler "value" (i32.const 36)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 37)) (i64.const 2))
(assert_return (invoke $profiler "value" (i32.const 38)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 39)) (i64.const 416))
(assert_return (invoke $profiler "value" (i32.const 40)) (i64.const 0))
;; Fresh initialization has no old entries, totals or metadata.
(assert_return (invoke $profiler "value" (i32.const 41)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 42)) (i64.const 224))
(assert_return (invoke $profiler "value" (i32.const 43)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 44)) (i64.const 0))
(assert_return (invoke $profiler "value" (i32.const 45)) (i64.const 0))
;; At normal exit the actual static registry frees its last backing allocation exactly once.
(assert_return (invoke $profiler "value" (i32.const 46)) (i64.const 990))
(assert_return (invoke $profiler "value" (i32.const 47)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 48)) (i64.const 8))
(assert_return (invoke $profiler "value" (i32.const 49)) (i64.const 1))
(assert_return (invoke $profiler "value" (i32.const 50)) (i64.const 0))
;; Exact JSON bytes: ordered events/async pairs, recursive totals, escaped owning
;; metadata, negative cross-instance timestamps and full-width realtime micros.
(assert_return (invoke $profiler "json" (i32.const 180256) (i32.const 210000) (i32.const 1680)) (i32.const 1))
(assert_return (invoke $profiler "json" (i32.const 180384) (i32.const 210000) (i32.const 1680)) (i32.const 1))
(assert_return (invoke $profiler "json" (i32.const 180512) (i32.const 210000) (i32.const 1680)) (i32.const 1))
(assert_return (invoke $profiler "json" (i32.const 180640) (i32.const 210000) (i32.const 1680)) (i32.const 1))
(assert_return (invoke $profiler "json" (i32.const 180768) (i32.const 215000) (i32.const 416)) (i32.const 1))
(assert_return (invoke $profiler "json" (i32.const 180896) (i32.const 218000) (i32.const 224)) (i32.const 1))
;; Preferred paths do not produce fallback files; open error leaves no file.
(assert_return (invoke $profiler "absent" (i32.const 181024)) (i32.const 1))
(assert_return (invoke $profiler "absent" (i32.const 181152)) (i32.const 1))
(assert_return (invoke $profiler "absent" (i32.const 181280)) (i32.const 1))
