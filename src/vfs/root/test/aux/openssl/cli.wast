;; Run after setup.sh from Bash. Check CLI statuses and actual output bytes.
(module $cli
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $cmp (param i32 i32 i32) (result i32)))
  (func (export "matches") (param $path i32) (param $expected i32) (param $length i32) (result i32)
    (local $fd i32) (local $n i32)
    (local.set $fd (call $open (local.get $path) (i32.const 0) (i32.const 0)))
    (local.set $n (call $read (local.get $fd) (i32.const 181000) (i32.add (local.get $length) (i32.const 1))))
    (drop (call $close (local.get $fd)))
    (i32.and (i32.eq (local.get $n) (local.get $length))
             (i32.eqz (call $cmp (i32.const 181000) (local.get $expected) (local.get $length)))))
  (data (i32.const 180000) "0\0a")
  (data (i32.const 180008) "1\0a")
  (data (i32.const 180016) "abc")
  (data (i32.const 180032) "\ba\78\16\bf\8f\01\cf\ea\41\41\40\de\5d\ae\22\23\b0\03\61\a3\96\17\7a\9c\b4\10\ff\61\f2\00\15\ad")
  (data (i32.const 182000) "/tmp/openssl-status-version\00")
  (data (i32.const 182128) "/tmp/openssl-status-digest\00")
  (data (i32.const 182256) "/tmp/openssl-status-encrypt-aes\00")
  (data (i32.const 182384) "/tmp/openssl-status-decrypt-aes\00")
  (data (i32.const 182512) "/tmp/openssl-status-bad-password\00")
  (data (i32.const 182640) "/tmp/openssl-status-encrypt-chacha\00")
  (data (i32.const 182768) "/tmp/openssl-status-decrypt-chacha\00")
  (data (i32.const 182896) "/tmp/openssl-status-generate-key\00")
  (data (i32.const 183024) "/tmp/openssl-status-check-key\00")
  (data (i32.const 183152) "/tmp/openssl-status-create-csr\00")
  (data (i32.const 183280) "/tmp/openssl-status-verify-csr\00")
  (data (i32.const 183408) "/tmp/openssl-status-create-certificate\00")
  (data (i32.const 183536) "/tmp/openssl-status-verify-certificate\00")
  (data (i32.const 184000) "/tmp/openssl-digest\00")
  (data (i32.const 184128) "/tmp/openssl-aes-decoded\00")
  (data (i32.const 184256) "/tmp/openssl-chacha-decoded\00")
)
;; version
(assert_return (invoke $cli "matches" (i32.const 182000) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; digest
(assert_return (invoke $cli "matches" (i32.const 182128) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; encrypt-aes
(assert_return (invoke $cli "matches" (i32.const 182256) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; decrypt-aes
(assert_return (invoke $cli "matches" (i32.const 182384) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; bad-password
(assert_return (invoke $cli "matches" (i32.const 182512) (i32.const 180008) (i32.const 2)) (i32.const 1))
;; encrypt-chacha
(assert_return (invoke $cli "matches" (i32.const 182640) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; decrypt-chacha
(assert_return (invoke $cli "matches" (i32.const 182768) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; generate-key
(assert_return (invoke $cli "matches" (i32.const 182896) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; check-key
(assert_return (invoke $cli "matches" (i32.const 183024) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; create-csr
(assert_return (invoke $cli "matches" (i32.const 183152) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; verify-csr
(assert_return (invoke $cli "matches" (i32.const 183280) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; create-certificate
(assert_return (invoke $cli "matches" (i32.const 183408) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; verify-certificate
(assert_return (invoke $cli "matches" (i32.const 183536) (i32.const 180000) (i32.const 2)) (i32.const 1))
;; digest-bytes
(assert_return (invoke $cli "matches" (i32.const 184000) (i32.const 180032) (i32.const 32)) (i32.const 1))
;; aes-roundtrip
(assert_return (invoke $cli "matches" (i32.const 184128) (i32.const 180016) (i32.const 3)) (i32.const 1))
;; chacha-roundtrip
(assert_return (invoke $cli "matches" (i32.const 184256) (i32.const 180016) (i32.const 3)) (i32.const 1))
