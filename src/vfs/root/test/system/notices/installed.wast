;; Capture commands with setup.sh before running these byte-for-byte assertions.
(module $notices
  (import "waste-runtime" "memory" (memory 4))
  (import "libc" "open" (func $open (param i32 i32 i32) (result i32)))
  (import "libc" "close" (func $close (param i32) (result i32)))
  (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
  (import "libc" "memcmp" (func $memcmp (param i32 i32 i32) (result i32)))
  (func (export "same") (param $a i32) (param $b i32) (result i32)
    (local $fa i32) (local $fb i32) (local $na i32) (local $nb i32) (local $ok i32)
    (local.set $fa (call $open (local.get $a) (i32.const 0) (i32.const 0)))
    (if (i32.lt_s (local.get $fa) (i32.const 0)) (then (return (i32.const 0))))
    (local.set $fb (call $open (local.get $b) (i32.const 0) (i32.const 0)))
    (if (i32.lt_s (local.get $fb) (i32.const 0))
      (then (drop (call $close (local.get $fa))) (return (i32.const 0))))
    (block $done
      (loop $next
        (local.set $na (call $read (local.get $fa) (i32.const 210000) (i32.const 128)))
        (local.set $nb (call $read (local.get $fb) (i32.const 211000) (i32.const 128)))
        (br_if $done (i32.or (i32.lt_s (local.get $na) (i32.const 0))
                            (i32.ne (local.get $na) (local.get $nb))))
        (br_if $done (call $memcmp (i32.const 210000) (i32.const 211000) (local.get $na)))
        (if (i32.eqz (local.get $na))
          (then (local.set $ok (i32.const 1)) (br $done)))
        (br $next)))
    (drop (call $close (local.get $fa)))
    (drop (call $close (local.get $fb)))
    (local.get $ok))
  (data (i32.const 180000)
    "/tmp/notices-awk-license\00"
    "/usr/share/licenses/awk/COPYING\00"
    "/tmp/notices-awk-license-error\00"
    "/tmp/notices-empty\00"
    "/tmp/notices-awk-license-status\00"
    "/tmp/notices-zero\00"
    "/tmp/notices-awk-authors\00"
    "/usr/share/licenses/awk/AUTHORS\00"
    "/tmp/notices-awk-authors-error\00"
    "/tmp/notices-awk-authors-status\00"
    "/tmp/notices-basename-license\00"
    "/usr/share/licenses/coreutils/COPYING\00"
    "/tmp/notices-basename-license-error\00"
    "/tmp/notices-basename-license-status\00"
    "/tmp/notices-basename-authors\00"
    "/usr/share/licenses/coreutils/basename/AUTHORS\00"
    "/tmp/notices-basename-authors-error\00"
    "/tmp/notices-basename-authors-status\00"
    "/tmp/notices-bash-license\00"
    "/usr/share/licenses/bash/COPYING\00"
    "/tmp/notices-bash-license-error\00"
    "/tmp/notices-bash-license-status\00"
    "/tmp/notices-bash-authors\00"
    "/usr/share/licenses/bash/AUTHORS\00"
    "/tmp/notices-bash-authors-error\00"
    "/tmp/notices-bash-authors-status\00"
    "/tmp/notices-cat-license\00"
    "/tmp/notices-cat-license-error\00"
    "/tmp/notices-cat-license-status\00"
    "/tmp/notices-cat-authors\00"
    "/usr/share/licenses/coreutils/cat/AUTHORS\00"
    "/tmp/notices-cat-authors-error\00"
    "/tmp/notices-cat-authors-status\00"
    "/tmp/notices-chmod-license\00"
    "/tmp/notices-chmod-license-error\00"
    "/tmp/notices-chmod-license-status\00"
    "/tmp/notices-chmod-authors\00"
    "/usr/share/licenses/coreutils/chmod/AUTHORS\00"
    "/tmp/notices-chmod-authors-error\00"
    "/tmp/notices-chmod-authors-status\00"
    "/tmp/notices-clang-license\00"
    "/usr/share/licenses/llvm/LICENSE.TXT\00"
    "/tmp/notices-clang-license-error\00"
    "/tmp/notices-clang-license-status\00"
    "/tmp/notices-clang-authors\00"
    "/usr/share/licenses/llvm/AUTHORS\00"
    "/tmp/notices-clang-authors-error\00"
    "/tmp/notices-clang-authors-status\00"
    "/tmp/notices-clipboard-license\00"
    "/usr/share/licenses/clipboard/LICENSE\00"
    "/tmp/notices-clipboard-license-error\00"
    "/tmp/notices-clipboard-license-status\00"
    "/tmp/notices-clipboard-authors\00"
    "/usr/share/licenses/clipboard/AUTHORS\00"
    "/tmp/notices-clipboard-authors-error\00"
    "/tmp/notices-clipboard-authors-status\00"
    "/tmp/notices-date-license\00"
    "/tmp/notices-date-license-error\00"
    "/tmp/notices-date-license-status\00"
    "/tmp/notices-date-authors\00"
    "/usr/share/licenses/coreutils/date/AUTHORS\00"
    "/tmp/notices-date-authors-error\00"
    "/tmp/notices-date-authors-status\00"
    "/tmp/notices-dirname-license\00"
    "/tmp/notices-dirname-license-error\00"
    "/tmp/notices-dirname-license-status\00"
    "/tmp/notices-dirname-authors\00"
    "/usr/share/licenses/coreutils/dirname/AUTHORS\00"
    "/tmp/notices-dirname-authors-error\00"
    "/tmp/notices-dirname-authors-status\00"
    "/tmp/notices-dot-license\00"
    "/usr/share/licenses/dot/COPYING\00"
    "/tmp/notices-dot-license-error\00"
    "/tmp/notices-dot-license-status\00"
    "/tmp/notices-dot-authors\00"
    "/usr/share/licenses/dot/AUTHORS\00"
    "/tmp/notices-dot-authors-error\00"
    "/tmp/notices-dot-authors-status\00"
    "/tmp/notices-download-license\00"
    "/usr/share/licenses/download/LICENSE\00"
    "/tmp/notices-download-license-error\00"
    "/tmp/notices-download-license-status\00"
    "/tmp/notices-download-authors\00"
    "/usr/share/licenses/download/AUTHORS\00"
    "/tmp/notices-download-authors-error\00"
    "/tmp/notices-download-authors-status\00"
    "/tmp/notices-echo-license\00"
    "/tmp/notices-echo-license-error\00"
    "/tmp/notices-echo-license-status\00"
    "/tmp/notices-echo-authors\00"
    "/usr/share/licenses/coreutils/echo/AUTHORS\00"
    "/tmp/notices-echo-authors-error\00"
    "/tmp/notices-echo-authors-status\00"
    "/tmp/notices-false-license\00"
    "/tmp/notices-false-license-error\00"
    "/tmp/notices-false-license-status\00"
    "/tmp/notices-one\00"
    "/tmp/notices-false-authors\00"
    "/usr/share/licenses/coreutils/false/AUTHORS\00"
    "/tmp/notices-false-authors-error\00"
    "/tmp/notices-false-authors-status\00"
    "/tmp/notices-ldd-license\00"
    "/usr/share/licenses/ldd/LICENSE\00"
    "/tmp/notices-ldd-license-error\00"
    "/tmp/notices-ldd-license-status\00"
    "/tmp/notices-ldd-authors\00"
    "/usr/share/licenses/ldd/AUTHORS\00"
    "/tmp/notices-ldd-authors-error\00"
    "/tmp/notices-ldd-authors-status\00"
    "/tmp/notices-lld-link-license\00"
    "/tmp/notices-lld-link-license-error\00"
    "/tmp/notices-lld-link-license-status\00"
    "/tmp/notices-lld-link-authors\00"
    "/tmp/notices-lld-link-authors-error\00"
    "/tmp/notices-lld-link-authors-status\00"
    "/tmp/notices-ls-license\00"
    "/tmp/notices-ls-license-error\00"
    "/tmp/notices-ls-license-status\00"
    "/tmp/notices-ls-authors\00"
    "/usr/share/licenses/coreutils/ls/AUTHORS\00"
    "/tmp/notices-ls-authors-error\00"
    "/tmp/notices-ls-authors-status\00"
    "/tmp/notices-m4-license\00"
    "/usr/share/licenses/m4/COPYING\00"
    "/tmp/notices-m4-license-error\00"
    "/tmp/notices-m4-license-status\00"
    "/tmp/notices-m4-authors\00"
    "/usr/share/licenses/m4/AUTHORS\00"
    "/tmp/notices-m4-authors-error\00"
    "/tmp/notices-m4-authors-status\00"
    "/tmp/notices-openssl-license\00"
    "/usr/share/licenses/openssl/LICENSE.txt\00"
    "/tmp/notices-openssl-license-error\00"
    "/tmp/notices-openssl-license-status\00"
    "/tmp/notices-openssl-authors\00"
    "/usr/share/licenses/openssl/AUTHORS\00"
    "/tmp/notices-openssl-authors-error\00"
    "/tmp/notices-openssl-authors-status\00"
    "/tmp/notices-printf-license\00"
    "/tmp/notices-printf-license-error\00"
    "/tmp/notices-printf-license-status\00"
    "/tmp/notices-printf-authors\00"
    "/usr/share/licenses/coreutils/printf/AUTHORS\00"
    "/tmp/notices-printf-authors-error\00"
    "/tmp/notices-printf-authors-status\00"
    "/tmp/notices-pwd-license\00"
    "/tmp/notices-pwd-license-error\00"
    "/tmp/notices-pwd-license-status\00"
    "/tmp/notices-pwd-authors\00"
    "/usr/share/licenses/coreutils/pwd/AUTHORS\00"
    "/tmp/notices-pwd-authors-error\00"
    "/tmp/notices-pwd-authors-status\00"
    "/tmp/notices-rogue-license\00"
    "/usr/share/licenses/rogue/LICENSE.TXT\00"
    "/tmp/notices-rogue-license-error\00"
    "/tmp/notices-rogue-license-status\00"
    "/tmp/notices-rogue-authors\00"
    "/usr/share/licenses/rogue/AUTHORS\00"
    "/tmp/notices-rogue-authors-error\00"
    "/tmp/notices-rogue-authors-status\00"
    "/tmp/notices-sleep-license\00"
    "/tmp/notices-sleep-license-error\00"
    "/tmp/notices-sleep-license-status\00"
    "/tmp/notices-sleep-authors\00"
    "/usr/share/licenses/coreutils/sleep/AUTHORS\00"
    "/tmp/notices-sleep-authors-error\00"
    "/tmp/notices-sleep-authors-status\00"
    "/tmp/notices-true-license\00"
    "/tmp/notices-true-license-error\00"
    "/tmp/notices-true-license-status\00"
    "/tmp/notices-true-authors\00"
    "/usr/share/licenses/coreutils/true/AUTHORS\00"
    "/tmp/notices-true-authors-error\00"
    "/tmp/notices-true-authors-status\00"
    "/tmp/notices-upload-license\00"
    "/usr/share/licenses/upload/LICENSE\00"
    "/tmp/notices-upload-license-error\00"
    "/tmp/notices-upload-license-status\00"
    "/tmp/notices-upload-authors\00"
    "/usr/share/licenses/upload/AUTHORS\00"
    "/tmp/notices-upload-authors-error\00"
    "/tmp/notices-upload-authors-status\00"
    "/tmp/notices-vim-license\00"
    "/usr/share/licenses/vim/LICENSE\00"
    "/tmp/notices-vim-license-error\00"
    "/tmp/notices-vim-license-status\00"
    "/tmp/notices-vim-authors\00"
    "/usr/share/licenses/vim/AUTHORS\00"
    "/tmp/notices-vim-authors-error\00"
    "/tmp/notices-vim-authors-status\00"
    "/tmp/notices-wasm-ld-license\00"
    "/tmp/notices-wasm-ld-license-error\00"
    "/tmp/notices-wasm-ld-license-status\00"
    "/tmp/notices-wasm-ld-authors\00"
    "/tmp/notices-wasm-ld-authors-error\00"
    "/tmp/notices-wasm-ld-authors-status\00"
    "/tmp/notices-wc-license\00"
    "/tmp/notices-wc-license-error\00"
    "/tmp/notices-wc-license-status\00"
    "/tmp/notices-wc-authors\00"
    "/usr/share/licenses/coreutils/wc/AUTHORS\00"
    "/tmp/notices-wc-authors-error\00"
    "/tmp/notices-wc-authors-status\00"
    "/tmp/notices-m4-pipe\00"
    "/tmp/notices-m4-pipe-status\00"
    "/tmp/notices-vim-pipe\00"
    "/tmp/notices-vim-pipe-status\00"
    "/tmp/notices-rogue-pipe\00"
    "/tmp/notices-rogue-pipe-status\00"
    "/tmp/notices-clang-pipe\00"
    "/tmp/notices-clang-pipe-status\00"
    "/tmp/notices-stdin\00"
    "/tmp/notices-stdin-status\00"
    "/tmp/notices-literal\00"
    "/tmp/notices-expect-authors\00"
    "/tmp/notices-echo-extra\00"
    "/tmp/notices-expect-extra\00"
    "/tmp/notices-echo-posix\00"
    "/tmp/notices-true-extra\00"
    "/tmp/notices-true-status\00"
    "/tmp/notices-false-extra\00"
    "/tmp/notices-false-status\00"
    "/tmp/notices-write-status\00"
  )
  (data (i32.const 220000)
    "/tmp/notices-sdk-compile-status\00"
    "/tmp/notices-sdk-authors\00"
    "/tmp/notices-sdk-authors-status\00"
    "/tmp/notices-sdk-authors-error\00"
    "/tmp/notices-sdk-license\00"
    "/tmp/notices-expect-license\00"
    "/tmp/notices-sdk-license-status\00"
    "/tmp/notices-sdk-license-error\00"
  )
)
;; awk-license
(assert_return (invoke $notices "same" (i32.const 180000) (i32.const 180025)) (i32.const 1))
;; awk-license-error
(assert_return (invoke $notices "same" (i32.const 180057) (i32.const 180088)) (i32.const 1))
;; awk-license-status
(assert_return (invoke $notices "same" (i32.const 180107) (i32.const 180139)) (i32.const 1))
;; awk-authors
(assert_return (invoke $notices "same" (i32.const 180157) (i32.const 180182)) (i32.const 1))
;; awk-authors-error
(assert_return (invoke $notices "same" (i32.const 180214) (i32.const 180088)) (i32.const 1))
;; awk-authors-status
(assert_return (invoke $notices "same" (i32.const 180245) (i32.const 180139)) (i32.const 1))
;; basename-license
(assert_return (invoke $notices "same" (i32.const 180277) (i32.const 180307)) (i32.const 1))
;; basename-license-error
(assert_return (invoke $notices "same" (i32.const 180345) (i32.const 180088)) (i32.const 1))
;; basename-license-status
(assert_return (invoke $notices "same" (i32.const 180381) (i32.const 180139)) (i32.const 1))
;; basename-authors
(assert_return (invoke $notices "same" (i32.const 180418) (i32.const 180448)) (i32.const 1))
;; basename-authors-error
(assert_return (invoke $notices "same" (i32.const 180495) (i32.const 180088)) (i32.const 1))
;; basename-authors-status
(assert_return (invoke $notices "same" (i32.const 180531) (i32.const 180139)) (i32.const 1))
;; bash-license
(assert_return (invoke $notices "same" (i32.const 180568) (i32.const 180594)) (i32.const 1))
;; bash-license-error
(assert_return (invoke $notices "same" (i32.const 180627) (i32.const 180088)) (i32.const 1))
;; bash-license-status
(assert_return (invoke $notices "same" (i32.const 180659) (i32.const 180139)) (i32.const 1))
;; bash-authors
(assert_return (invoke $notices "same" (i32.const 180692) (i32.const 180718)) (i32.const 1))
;; bash-authors-error
(assert_return (invoke $notices "same" (i32.const 180751) (i32.const 180088)) (i32.const 1))
;; bash-authors-status
(assert_return (invoke $notices "same" (i32.const 180783) (i32.const 180139)) (i32.const 1))
;; cat-license
(assert_return (invoke $notices "same" (i32.const 180816) (i32.const 180307)) (i32.const 1))
;; cat-license-error
(assert_return (invoke $notices "same" (i32.const 180841) (i32.const 180088)) (i32.const 1))
;; cat-license-status
(assert_return (invoke $notices "same" (i32.const 180872) (i32.const 180139)) (i32.const 1))
;; cat-authors
(assert_return (invoke $notices "same" (i32.const 180904) (i32.const 180929)) (i32.const 1))
;; cat-authors-error
(assert_return (invoke $notices "same" (i32.const 180971) (i32.const 180088)) (i32.const 1))
;; cat-authors-status
(assert_return (invoke $notices "same" (i32.const 181002) (i32.const 180139)) (i32.const 1))
;; chmod-license
(assert_return (invoke $notices "same" (i32.const 181034) (i32.const 180307)) (i32.const 1))
;; chmod-license-error
(assert_return (invoke $notices "same" (i32.const 181061) (i32.const 180088)) (i32.const 1))
;; chmod-license-status
(assert_return (invoke $notices "same" (i32.const 181094) (i32.const 180139)) (i32.const 1))
;; chmod-authors
(assert_return (invoke $notices "same" (i32.const 181128) (i32.const 181155)) (i32.const 1))
;; chmod-authors-error
(assert_return (invoke $notices "same" (i32.const 181199) (i32.const 180088)) (i32.const 1))
;; chmod-authors-status
(assert_return (invoke $notices "same" (i32.const 181232) (i32.const 180139)) (i32.const 1))
;; clang-license
(assert_return (invoke $notices "same" (i32.const 181266) (i32.const 181293)) (i32.const 1))
;; clang-license-error
(assert_return (invoke $notices "same" (i32.const 181330) (i32.const 180088)) (i32.const 1))
;; clang-license-status
(assert_return (invoke $notices "same" (i32.const 181363) (i32.const 180139)) (i32.const 1))
;; clang-authors
(assert_return (invoke $notices "same" (i32.const 181397) (i32.const 181424)) (i32.const 1))
;; clang-authors-error
(assert_return (invoke $notices "same" (i32.const 181457) (i32.const 180088)) (i32.const 1))
;; clang-authors-status
(assert_return (invoke $notices "same" (i32.const 181490) (i32.const 180139)) (i32.const 1))
;; clipboard-license
(assert_return (invoke $notices "same" (i32.const 181524) (i32.const 181555)) (i32.const 1))
;; clipboard-license-error
(assert_return (invoke $notices "same" (i32.const 181593) (i32.const 180088)) (i32.const 1))
;; clipboard-license-status
(assert_return (invoke $notices "same" (i32.const 181630) (i32.const 180139)) (i32.const 1))
;; clipboard-authors
(assert_return (invoke $notices "same" (i32.const 181668) (i32.const 181699)) (i32.const 1))
;; clipboard-authors-error
(assert_return (invoke $notices "same" (i32.const 181737) (i32.const 180088)) (i32.const 1))
;; clipboard-authors-status
(assert_return (invoke $notices "same" (i32.const 181774) (i32.const 180139)) (i32.const 1))
;; date-license
(assert_return (invoke $notices "same" (i32.const 181812) (i32.const 180307)) (i32.const 1))
;; date-license-error
(assert_return (invoke $notices "same" (i32.const 181838) (i32.const 180088)) (i32.const 1))
;; date-license-status
(assert_return (invoke $notices "same" (i32.const 181870) (i32.const 180139)) (i32.const 1))
;; date-authors
(assert_return (invoke $notices "same" (i32.const 181903) (i32.const 181929)) (i32.const 1))
;; date-authors-error
(assert_return (invoke $notices "same" (i32.const 181972) (i32.const 180088)) (i32.const 1))
;; date-authors-status
(assert_return (invoke $notices "same" (i32.const 182004) (i32.const 180139)) (i32.const 1))
;; dirname-license
(assert_return (invoke $notices "same" (i32.const 182037) (i32.const 180307)) (i32.const 1))
;; dirname-license-error
(assert_return (invoke $notices "same" (i32.const 182066) (i32.const 180088)) (i32.const 1))
;; dirname-license-status
(assert_return (invoke $notices "same" (i32.const 182101) (i32.const 180139)) (i32.const 1))
;; dirname-authors
(assert_return (invoke $notices "same" (i32.const 182137) (i32.const 182166)) (i32.const 1))
;; dirname-authors-error
(assert_return (invoke $notices "same" (i32.const 182212) (i32.const 180088)) (i32.const 1))
;; dirname-authors-status
(assert_return (invoke $notices "same" (i32.const 182247) (i32.const 180139)) (i32.const 1))
;; dot-license
(assert_return (invoke $notices "same" (i32.const 182283) (i32.const 182308)) (i32.const 1))
;; dot-license-error
(assert_return (invoke $notices "same" (i32.const 182340) (i32.const 180088)) (i32.const 1))
;; dot-license-status
(assert_return (invoke $notices "same" (i32.const 182371) (i32.const 180139)) (i32.const 1))
;; dot-authors
(assert_return (invoke $notices "same" (i32.const 182403) (i32.const 182428)) (i32.const 1))
;; dot-authors-error
(assert_return (invoke $notices "same" (i32.const 182460) (i32.const 180088)) (i32.const 1))
;; dot-authors-status
(assert_return (invoke $notices "same" (i32.const 182491) (i32.const 180139)) (i32.const 1))
;; download-license
(assert_return (invoke $notices "same" (i32.const 182523) (i32.const 182553)) (i32.const 1))
;; download-license-error
(assert_return (invoke $notices "same" (i32.const 182590) (i32.const 180088)) (i32.const 1))
;; download-license-status
(assert_return (invoke $notices "same" (i32.const 182626) (i32.const 180139)) (i32.const 1))
;; download-authors
(assert_return (invoke $notices "same" (i32.const 182663) (i32.const 182693)) (i32.const 1))
;; download-authors-error
(assert_return (invoke $notices "same" (i32.const 182730) (i32.const 180088)) (i32.const 1))
;; download-authors-status
(assert_return (invoke $notices "same" (i32.const 182766) (i32.const 180139)) (i32.const 1))
;; echo-license
(assert_return (invoke $notices "same" (i32.const 182803) (i32.const 180307)) (i32.const 1))
;; echo-license-error
(assert_return (invoke $notices "same" (i32.const 182829) (i32.const 180088)) (i32.const 1))
;; echo-license-status
(assert_return (invoke $notices "same" (i32.const 182861) (i32.const 180139)) (i32.const 1))
;; echo-authors
(assert_return (invoke $notices "same" (i32.const 182894) (i32.const 182920)) (i32.const 1))
;; echo-authors-error
(assert_return (invoke $notices "same" (i32.const 182963) (i32.const 180088)) (i32.const 1))
;; echo-authors-status
(assert_return (invoke $notices "same" (i32.const 182995) (i32.const 180139)) (i32.const 1))
;; false-license
(assert_return (invoke $notices "same" (i32.const 183028) (i32.const 180307)) (i32.const 1))
;; false-license-error
(assert_return (invoke $notices "same" (i32.const 183055) (i32.const 180088)) (i32.const 1))
;; false-license-status
(assert_return (invoke $notices "same" (i32.const 183088) (i32.const 183122)) (i32.const 1))
;; false-authors
(assert_return (invoke $notices "same" (i32.const 183139) (i32.const 183166)) (i32.const 1))
;; false-authors-error
(assert_return (invoke $notices "same" (i32.const 183210) (i32.const 180088)) (i32.const 1))
;; false-authors-status
(assert_return (invoke $notices "same" (i32.const 183243) (i32.const 183122)) (i32.const 1))
;; ldd-license
(assert_return (invoke $notices "same" (i32.const 183277) (i32.const 183302)) (i32.const 1))
;; ldd-license-error
(assert_return (invoke $notices "same" (i32.const 183334) (i32.const 180088)) (i32.const 1))
;; ldd-license-status
(assert_return (invoke $notices "same" (i32.const 183365) (i32.const 180139)) (i32.const 1))
;; ldd-authors
(assert_return (invoke $notices "same" (i32.const 183397) (i32.const 183422)) (i32.const 1))
;; ldd-authors-error
(assert_return (invoke $notices "same" (i32.const 183454) (i32.const 180088)) (i32.const 1))
;; ldd-authors-status
(assert_return (invoke $notices "same" (i32.const 183485) (i32.const 180139)) (i32.const 1))
;; lld-link-license
(assert_return (invoke $notices "same" (i32.const 183517) (i32.const 181293)) (i32.const 1))
;; lld-link-license-error
(assert_return (invoke $notices "same" (i32.const 183547) (i32.const 180088)) (i32.const 1))
;; lld-link-license-status
(assert_return (invoke $notices "same" (i32.const 183583) (i32.const 180139)) (i32.const 1))
;; lld-link-authors
(assert_return (invoke $notices "same" (i32.const 183620) (i32.const 181424)) (i32.const 1))
;; lld-link-authors-error
(assert_return (invoke $notices "same" (i32.const 183650) (i32.const 180088)) (i32.const 1))
;; lld-link-authors-status
(assert_return (invoke $notices "same" (i32.const 183686) (i32.const 180139)) (i32.const 1))
;; ls-license
(assert_return (invoke $notices "same" (i32.const 183723) (i32.const 180307)) (i32.const 1))
;; ls-license-error
(assert_return (invoke $notices "same" (i32.const 183747) (i32.const 180088)) (i32.const 1))
;; ls-license-status
(assert_return (invoke $notices "same" (i32.const 183777) (i32.const 180139)) (i32.const 1))
;; ls-authors
(assert_return (invoke $notices "same" (i32.const 183808) (i32.const 183832)) (i32.const 1))
;; ls-authors-error
(assert_return (invoke $notices "same" (i32.const 183873) (i32.const 180088)) (i32.const 1))
;; ls-authors-status
(assert_return (invoke $notices "same" (i32.const 183903) (i32.const 180139)) (i32.const 1))
;; m4-license
(assert_return (invoke $notices "same" (i32.const 183934) (i32.const 183958)) (i32.const 1))
;; m4-license-error
(assert_return (invoke $notices "same" (i32.const 183989) (i32.const 180088)) (i32.const 1))
;; m4-license-status
(assert_return (invoke $notices "same" (i32.const 184019) (i32.const 180139)) (i32.const 1))
;; m4-authors
(assert_return (invoke $notices "same" (i32.const 184050) (i32.const 184074)) (i32.const 1))
;; m4-authors-error
(assert_return (invoke $notices "same" (i32.const 184105) (i32.const 180088)) (i32.const 1))
;; m4-authors-status
(assert_return (invoke $notices "same" (i32.const 184135) (i32.const 180139)) (i32.const 1))
;; openssl-license
(assert_return (invoke $notices "same" (i32.const 184166) (i32.const 184195)) (i32.const 1))
;; openssl-license-error
(assert_return (invoke $notices "same" (i32.const 184235) (i32.const 180088)) (i32.const 1))
;; openssl-license-status
(assert_return (invoke $notices "same" (i32.const 184270) (i32.const 180139)) (i32.const 1))
;; openssl-authors
(assert_return (invoke $notices "same" (i32.const 184306) (i32.const 184335)) (i32.const 1))
;; openssl-authors-error
(assert_return (invoke $notices "same" (i32.const 184371) (i32.const 180088)) (i32.const 1))
;; openssl-authors-status
(assert_return (invoke $notices "same" (i32.const 184406) (i32.const 180139)) (i32.const 1))
;; printf-license
(assert_return (invoke $notices "same" (i32.const 184442) (i32.const 180307)) (i32.const 1))
;; printf-license-error
(assert_return (invoke $notices "same" (i32.const 184470) (i32.const 180088)) (i32.const 1))
;; printf-license-status
(assert_return (invoke $notices "same" (i32.const 184504) (i32.const 180139)) (i32.const 1))
;; printf-authors
(assert_return (invoke $notices "same" (i32.const 184539) (i32.const 184567)) (i32.const 1))
;; printf-authors-error
(assert_return (invoke $notices "same" (i32.const 184612) (i32.const 180088)) (i32.const 1))
;; printf-authors-status
(assert_return (invoke $notices "same" (i32.const 184646) (i32.const 180139)) (i32.const 1))
;; pwd-license
(assert_return (invoke $notices "same" (i32.const 184681) (i32.const 180307)) (i32.const 1))
;; pwd-license-error
(assert_return (invoke $notices "same" (i32.const 184706) (i32.const 180088)) (i32.const 1))
;; pwd-license-status
(assert_return (invoke $notices "same" (i32.const 184737) (i32.const 180139)) (i32.const 1))
;; pwd-authors
(assert_return (invoke $notices "same" (i32.const 184769) (i32.const 184794)) (i32.const 1))
;; pwd-authors-error
(assert_return (invoke $notices "same" (i32.const 184836) (i32.const 180088)) (i32.const 1))
;; pwd-authors-status
(assert_return (invoke $notices "same" (i32.const 184867) (i32.const 180139)) (i32.const 1))
;; rogue-license
(assert_return (invoke $notices "same" (i32.const 184899) (i32.const 184926)) (i32.const 1))
;; rogue-license-error
(assert_return (invoke $notices "same" (i32.const 184964) (i32.const 180088)) (i32.const 1))
;; rogue-license-status
(assert_return (invoke $notices "same" (i32.const 184997) (i32.const 180139)) (i32.const 1))
;; rogue-authors
(assert_return (invoke $notices "same" (i32.const 185031) (i32.const 185058)) (i32.const 1))
;; rogue-authors-error
(assert_return (invoke $notices "same" (i32.const 185092) (i32.const 180088)) (i32.const 1))
;; rogue-authors-status
(assert_return (invoke $notices "same" (i32.const 185125) (i32.const 180139)) (i32.const 1))
;; sleep-license
(assert_return (invoke $notices "same" (i32.const 185159) (i32.const 180307)) (i32.const 1))
;; sleep-license-error
(assert_return (invoke $notices "same" (i32.const 185186) (i32.const 180088)) (i32.const 1))
;; sleep-license-status
(assert_return (invoke $notices "same" (i32.const 185219) (i32.const 180139)) (i32.const 1))
;; sleep-authors
(assert_return (invoke $notices "same" (i32.const 185253) (i32.const 185280)) (i32.const 1))
;; sleep-authors-error
(assert_return (invoke $notices "same" (i32.const 185324) (i32.const 180088)) (i32.const 1))
;; sleep-authors-status
(assert_return (invoke $notices "same" (i32.const 185357) (i32.const 180139)) (i32.const 1))
;; true-license
(assert_return (invoke $notices "same" (i32.const 185391) (i32.const 180307)) (i32.const 1))
;; true-license-error
(assert_return (invoke $notices "same" (i32.const 185417) (i32.const 180088)) (i32.const 1))
;; true-license-status
(assert_return (invoke $notices "same" (i32.const 185449) (i32.const 180139)) (i32.const 1))
;; true-authors
(assert_return (invoke $notices "same" (i32.const 185482) (i32.const 185508)) (i32.const 1))
;; true-authors-error
(assert_return (invoke $notices "same" (i32.const 185551) (i32.const 180088)) (i32.const 1))
;; true-authors-status
(assert_return (invoke $notices "same" (i32.const 185583) (i32.const 180139)) (i32.const 1))
;; upload-license
(assert_return (invoke $notices "same" (i32.const 185616) (i32.const 185644)) (i32.const 1))
;; upload-license-error
(assert_return (invoke $notices "same" (i32.const 185679) (i32.const 180088)) (i32.const 1))
;; upload-license-status
(assert_return (invoke $notices "same" (i32.const 185713) (i32.const 180139)) (i32.const 1))
;; upload-authors
(assert_return (invoke $notices "same" (i32.const 185748) (i32.const 185776)) (i32.const 1))
;; upload-authors-error
(assert_return (invoke $notices "same" (i32.const 185811) (i32.const 180088)) (i32.const 1))
;; upload-authors-status
(assert_return (invoke $notices "same" (i32.const 185845) (i32.const 180139)) (i32.const 1))
;; vim-license
(assert_return (invoke $notices "same" (i32.const 185880) (i32.const 185905)) (i32.const 1))
;; vim-license-error
(assert_return (invoke $notices "same" (i32.const 185937) (i32.const 180088)) (i32.const 1))
;; vim-license-status
(assert_return (invoke $notices "same" (i32.const 185968) (i32.const 180139)) (i32.const 1))
;; vim-authors
(assert_return (invoke $notices "same" (i32.const 186000) (i32.const 186025)) (i32.const 1))
;; vim-authors-error
(assert_return (invoke $notices "same" (i32.const 186057) (i32.const 180088)) (i32.const 1))
;; vim-authors-status
(assert_return (invoke $notices "same" (i32.const 186088) (i32.const 180139)) (i32.const 1))
;; wasm-ld-license
(assert_return (invoke $notices "same" (i32.const 186120) (i32.const 181293)) (i32.const 1))
;; wasm-ld-license-error
(assert_return (invoke $notices "same" (i32.const 186149) (i32.const 180088)) (i32.const 1))
;; wasm-ld-license-status
(assert_return (invoke $notices "same" (i32.const 186184) (i32.const 180139)) (i32.const 1))
;; wasm-ld-authors
(assert_return (invoke $notices "same" (i32.const 186220) (i32.const 181424)) (i32.const 1))
;; wasm-ld-authors-error
(assert_return (invoke $notices "same" (i32.const 186249) (i32.const 180088)) (i32.const 1))
;; wasm-ld-authors-status
(assert_return (invoke $notices "same" (i32.const 186284) (i32.const 180139)) (i32.const 1))
;; wc-license
(assert_return (invoke $notices "same" (i32.const 186320) (i32.const 180307)) (i32.const 1))
;; wc-license-error
(assert_return (invoke $notices "same" (i32.const 186344) (i32.const 180088)) (i32.const 1))
;; wc-license-status
(assert_return (invoke $notices "same" (i32.const 186374) (i32.const 180139)) (i32.const 1))
;; wc-authors
(assert_return (invoke $notices "same" (i32.const 186405) (i32.const 186429)) (i32.const 1))
;; wc-authors-error
(assert_return (invoke $notices "same" (i32.const 186470) (i32.const 180088)) (i32.const 1))
;; wc-authors-status
(assert_return (invoke $notices "same" (i32.const 186500) (i32.const 180139)) (i32.const 1))
;; m4-pipe
(assert_return (invoke $notices "same" (i32.const 186531) (i32.const 183958)) (i32.const 1))
;; m4-pipe-status
(assert_return (invoke $notices "same" (i32.const 186552) (i32.const 180139)) (i32.const 1))
;; vim-pipe
(assert_return (invoke $notices "same" (i32.const 186580) (i32.const 186025)) (i32.const 1))
;; vim-pipe-status
(assert_return (invoke $notices "same" (i32.const 186602) (i32.const 180139)) (i32.const 1))
;; rogue-pipe
(assert_return (invoke $notices "same" (i32.const 186631) (i32.const 184926)) (i32.const 1))
;; rogue-pipe-status
(assert_return (invoke $notices "same" (i32.const 186655) (i32.const 180139)) (i32.const 1))
;; clang-pipe
(assert_return (invoke $notices "same" (i32.const 186686) (i32.const 181424)) (i32.const 1))
;; clang-pipe-status
(assert_return (invoke $notices "same" (i32.const 186710) (i32.const 180139)) (i32.const 1))
;; stdin
(assert_return (invoke $notices "same" (i32.const 186741) (i32.const 184074)) (i32.const 1))
;; stdin-status
(assert_return (invoke $notices "same" (i32.const 186760) (i32.const 180139)) (i32.const 1))
;; literal
(assert_return (invoke $notices "same" (i32.const 186786) (i32.const 186807)) (i32.const 1))
;; echo-extra
(assert_return (invoke $notices "same" (i32.const 186835) (i32.const 186859)) (i32.const 1))
;; echo-posix
(assert_return (invoke $notices "same" (i32.const 186885) (i32.const 186807)) (i32.const 1))
;; true-extra
(assert_return (invoke $notices "same" (i32.const 186909) (i32.const 180088)) (i32.const 1))
;; true-status
(assert_return (invoke $notices "same" (i32.const 186933) (i32.const 180139)) (i32.const 1))
;; false-extra
(assert_return (invoke $notices "same" (i32.const 186958) (i32.const 180088)) (i32.const 1))
;; false-status
(assert_return (invoke $notices "same" (i32.const 186983) (i32.const 183122)) (i32.const 1))
;; write-status
(assert_return (invoke $notices "same" (i32.const 187009) (i32.const 183122)) (i32.const 1))
;; sdk-compile-status
(assert_return (invoke $notices "same" (i32.const 220000) (i32.const 180139)) (i32.const 1))
;; sdk-authors
(assert_return (invoke $notices "same" (i32.const 220032) (i32.const 186807)) (i32.const 1))
;; sdk-authors-status
(assert_return (invoke $notices "same" (i32.const 220057) (i32.const 180139)) (i32.const 1))
;; sdk-authors-error
(assert_return (invoke $notices "same" (i32.const 220089) (i32.const 180088)) (i32.const 1))
;; sdk-license
(assert_return (invoke $notices "same" (i32.const 220120) (i32.const 220145)) (i32.const 1))
;; sdk-license-status
(assert_return (invoke $notices "same" (i32.const 220173) (i32.const 180139)) (i32.const 1))
;; sdk-license-error
(assert_return (invoke $notices "same" (i32.const 220205) (i32.const 180088)) (i32.const 1))
