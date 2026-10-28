;; Run setup.sh in Bash first. Assertions check the installed Gawk executable.
(module $awk
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "access" (func $access (param i32 i32) (result i32)))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $cmp (param i32 i32 i32) (result i32)))
  (data (i32.const 180000) "/usr/bin/awk\00")
  (func (export "executable") (result i32)
    (i32.eqz (call $access (i32.const 180000) (i32.const 1))))
  (func (export "matches") (param $path i32) (param $expected i32) (param $length i32) (result i32)
    (local $fd i32) (local $n i32)
    (local.set $fd (call $open (local.get $path) (i32.const 0) (i32.const 0)))
    (if (i32.lt_s (local.get $fd) (i32.const 0)) (then (return (i32.const 0))))
    (local.set $n (call $read (local.get $fd) (i32.const 190000) (i32.add (local.get $length) (i32.const 1))))
    (drop (call $close (local.get $fd)))
    (i32.and (i32.eq (local.get $n) (local.get $length))
      (i32.eqz (call $cmp (i32.const 190000) (local.get $expected) (local.get $length)))))
  (data (i32.const 181000) "\2f\74\6d\70\2f\61\77\6b\2d\62\65\67\69\6e\00")
  (data (i32.const 185000) "\47\4e\55\20\41\77\6b\20\34\32\0a\33\2e\35\30\0a")
  (data (i32.const 181128) "\2f\74\6d\70\2f\61\77\6b\2d\61\72\72\61\79\00")
  (data (i32.const 185128) "\33\20\36\20\33\0a")
  (data (i32.const 181256) "\2f\74\6d\70\2f\61\77\6b\2d\72\65\67\65\78\00")
  (data (i32.const 185256) "\31\20\32\20\41\50\50\4c\45\20\32\0a\33\20\32\20\41\50\50\4c\45\20\34\0a")
  (data (i32.const 181384) "\2f\74\6d\70\2f\61\77\6b\2d\66\69\65\6c\64\73\00")
  (data (i32.const 185384) "\6f\6b\20\72\6f\77\20\34\32\0a")
  (data (i32.const 181512) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\72\69\6e\67\00")
  (data (i32.const 185512) "\32\20\61\58\62\58\0a\33\20\74\77\6f\0a\62\63\64\20\33\0a")
  (data (i32.const 181640) "\2f\74\6d\70\2f\61\77\6b\2d\67\65\74\6c\69\6e\65\00")
  (data (i32.const 185640) "\33\0a")
  (data (i32.const 181768) "\2f\74\6d\70\2f\61\77\6b\2d\73\61\76\65\64\00")
  (data (i32.const 185768) "\73\61\76\65\64\0a")
  (data (i32.const 181896) "\2f\74\6d\70\2f\61\77\6b\2d\70\69\70\65\00")
  (data (i32.const 185896) "\39\0a")
  (data (i32.const 182024) "\2f\74\6d\70\2f\61\77\6b\2d\65\78\69\74\00")
  (data (i32.const 186024) "")
  (data (i32.const 182152) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\62\65\67\69\6e\00")
  (data (i32.const 186152) "\30\0a")
  (data (i32.const 182280) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\61\72\72\61\79\00")
  (data (i32.const 186280) "\30\0a")
  (data (i32.const 182408) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\72\65\67\65\78\00")
  (data (i32.const 186408) "\30\0a")
  (data (i32.const 182536) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\66\69\65\6c\64\73\00")
  (data (i32.const 186536) "\30\0a")
  (data (i32.const 182664) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\73\74\72\69\6e\67\00")
  (data (i32.const 186664) "\30\0a")
  (data (i32.const 182792) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\67\65\74\6c\69\6e\65\00")
  (data (i32.const 186792) "\30\0a")
  (data (i32.const 182920) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\70\69\70\65\00")
  (data (i32.const 186920) "\30\0a")
  (data (i32.const 183048) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\65\78\69\74\00")
  (data (i32.const 187048) "\37\0a")
  (data (i32.const 183176) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\69\6e\76\61\6c\69\64\00")
  (data (i32.const 187176) "\31\0a")
  (data (i32.const 183304) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\76\65\72\73\69\6f\6e\00")
  (data (i32.const 187304) "\30\0a")
  (data (i32.const 188000) "\2f\74\6d\70\2f\61\77\6b\2d\6d\61\74\68\00")
  (data (i32.const 188128) "\30\2e\38\34\31\34\37\31\20\30\2e\35\34\30\33\30\32\20\30\2e\37\38\35\33\39\38\20\32\2e\37\31\38\32\38\32\20\30\2e\36\39\33\31\34\37\20\31\30\32\34\20\33\20\31\2e\35\0a")
  (data (i32.const 188256) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\6d\61\74\68\00")
  (data (i32.const 188384) "\30\0a")
  (data (i32.const 188512) "\2f\74\6d\70\2f\61\77\6b\2d\70\72\6f\63\65\73\73\00")
  (data (i32.const 188640) "\70\69\70\65\20\30\0a\33\0a\30\0a")
  (data (i32.const 188768) "\2f\74\6d\70\2f\61\77\6b\2d\73\74\61\74\75\73\2d\70\72\6f\63\65\73\73\00")
  (data (i32.const 188896) "\30\0a")

  (data (i32.const 189200) "/tmp/awk-child\00")
  (data (i32.const 189264) "written\0a")

)
(assert_return (invoke $awk "executable") (i32.const 1))
;; begin
(assert_return (invoke $awk "matches" (i32.const 181000) (i32.const 185000) (i32.const 16)) (i32.const 1))
;; array
(assert_return (invoke $awk "matches" (i32.const 181128) (i32.const 185128) (i32.const 6)) (i32.const 1))
;; regex
(assert_return (invoke $awk "matches" (i32.const 181256) (i32.const 185256) (i32.const 24)) (i32.const 1))
;; fields
(assert_return (invoke $awk "matches" (i32.const 181384) (i32.const 185384) (i32.const 10)) (i32.const 1))
;; string
(assert_return (invoke $awk "matches" (i32.const 181512) (i32.const 185512) (i32.const 19)) (i32.const 1))
;; getline
(assert_return (invoke $awk "matches" (i32.const 181640) (i32.const 185640) (i32.const 2)) (i32.const 1))
;; saved
(assert_return (invoke $awk "matches" (i32.const 181768) (i32.const 185768) (i32.const 6)) (i32.const 1))
;; pipe
(assert_return (invoke $awk "matches" (i32.const 181896) (i32.const 185896) (i32.const 2)) (i32.const 1))
;; exit
(assert_return (invoke $awk "matches" (i32.const 182024) (i32.const 186024) (i32.const 0)) (i32.const 1))
;; status-begin
(assert_return (invoke $awk "matches" (i32.const 182152) (i32.const 186152) (i32.const 2)) (i32.const 1))
;; status-array
(assert_return (invoke $awk "matches" (i32.const 182280) (i32.const 186280) (i32.const 2)) (i32.const 1))
;; status-regex
(assert_return (invoke $awk "matches" (i32.const 182408) (i32.const 186408) (i32.const 2)) (i32.const 1))
;; status-fields
(assert_return (invoke $awk "matches" (i32.const 182536) (i32.const 186536) (i32.const 2)) (i32.const 1))
;; status-string
(assert_return (invoke $awk "matches" (i32.const 182664) (i32.const 186664) (i32.const 2)) (i32.const 1))
;; status-getline
(assert_return (invoke $awk "matches" (i32.const 182792) (i32.const 186792) (i32.const 2)) (i32.const 1))
;; status-pipe
(assert_return (invoke $awk "matches" (i32.const 182920) (i32.const 186920) (i32.const 2)) (i32.const 1))
;; status-exit
(assert_return (invoke $awk "matches" (i32.const 183048) (i32.const 187048) (i32.const 2)) (i32.const 1))
;; status-invalid
(assert_return (invoke $awk "matches" (i32.const 183176) (i32.const 187176) (i32.const 2)) (i32.const 1))
;; status-version
(assert_return (invoke $awk "matches" (i32.const 183304) (i32.const 187304) (i32.const 2)) (i32.const 1))
;; math
(assert_return (invoke $awk "matches" (i32.const 188000) (i32.const 188128) (i32.const 56)) (i32.const 1))
;; status-math
(assert_return (invoke $awk "matches" (i32.const 188256) (i32.const 188384) (i32.const 2)) (i32.const 1))
;; process
(assert_return (invoke $awk "matches" (i32.const 188512) (i32.const 188640) (i32.const 11)) (i32.const 1))
;; status-process
(assert_return (invoke $awk "matches" (i32.const 188768) (i32.const 188896) (i32.const 2)) (i32.const 1))
;; command-pipe output from the child, after close waited for it.
(assert_return (invoke $awk "matches" (i32.const 189200) (i32.const 189264) (i32.const 8)) (i32.const 1))
