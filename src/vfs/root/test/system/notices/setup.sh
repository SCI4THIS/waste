# Capture observations only; installed.wast owns every assertion.
unset POSIXLY_CORRECT
LC_ALL=C; export LC_ALL
printf '0\n' > /tmp/notices-zero
printf '1\n' > /tmp/notices-one
: > /tmp/notices-empty
/usr/bin/awk --license > /tmp/notices-awk-license 2> /tmp/notices-awk-license-error
printf '%s\n' "$?" > /tmp/notices-awk-license-status
/usr/bin/awk --authors > /tmp/notices-awk-authors 2> /tmp/notices-awk-authors-error
printf '%s\n' "$?" > /tmp/notices-awk-authors-status
/usr/bin/basename --license > /tmp/notices-basename-license 2> /tmp/notices-basename-license-error
printf '%s\n' "$?" > /tmp/notices-basename-license-status
/usr/bin/basename --authors > /tmp/notices-basename-authors 2> /tmp/notices-basename-authors-error
printf '%s\n' "$?" > /tmp/notices-basename-authors-status
/usr/bin/bash --license > /tmp/notices-bash-license 2> /tmp/notices-bash-license-error
printf '%s\n' "$?" > /tmp/notices-bash-license-status
/usr/bin/bash --authors > /tmp/notices-bash-authors 2> /tmp/notices-bash-authors-error
printf '%s\n' "$?" > /tmp/notices-bash-authors-status
/usr/bin/cat --license > /tmp/notices-cat-license 2> /tmp/notices-cat-license-error
printf '%s\n' "$?" > /tmp/notices-cat-license-status
/usr/bin/cat --authors > /tmp/notices-cat-authors 2> /tmp/notices-cat-authors-error
printf '%s\n' "$?" > /tmp/notices-cat-authors-status
/usr/bin/chmod --license > /tmp/notices-chmod-license 2> /tmp/notices-chmod-license-error
printf '%s\n' "$?" > /tmp/notices-chmod-license-status
/usr/bin/chmod --authors > /tmp/notices-chmod-authors 2> /tmp/notices-chmod-authors-error
printf '%s\n' "$?" > /tmp/notices-chmod-authors-status
/usr/bin/clang --license > /tmp/notices-clang-license 2> /tmp/notices-clang-license-error
printf '%s\n' "$?" > /tmp/notices-clang-license-status
/usr/bin/clang --authors > /tmp/notices-clang-authors 2> /tmp/notices-clang-authors-error
printf '%s\n' "$?" > /tmp/notices-clang-authors-status
/usr/bin/clipboard --license > /tmp/notices-clipboard-license 2> /tmp/notices-clipboard-license-error
printf '%s\n' "$?" > /tmp/notices-clipboard-license-status
/usr/bin/clipboard --authors > /tmp/notices-clipboard-authors 2> /tmp/notices-clipboard-authors-error
printf '%s\n' "$?" > /tmp/notices-clipboard-authors-status
/usr/bin/date --license > /tmp/notices-date-license 2> /tmp/notices-date-license-error
printf '%s\n' "$?" > /tmp/notices-date-license-status
/usr/bin/date --authors > /tmp/notices-date-authors 2> /tmp/notices-date-authors-error
printf '%s\n' "$?" > /tmp/notices-date-authors-status
/usr/bin/dirname --license > /tmp/notices-dirname-license 2> /tmp/notices-dirname-license-error
printf '%s\n' "$?" > /tmp/notices-dirname-license-status
/usr/bin/dirname --authors > /tmp/notices-dirname-authors 2> /tmp/notices-dirname-authors-error
printf '%s\n' "$?" > /tmp/notices-dirname-authors-status
/usr/bin/dot --license > /tmp/notices-dot-license 2> /tmp/notices-dot-license-error
printf '%s\n' "$?" > /tmp/notices-dot-license-status
/usr/bin/dot --authors > /tmp/notices-dot-authors 2> /tmp/notices-dot-authors-error
printf '%s\n' "$?" > /tmp/notices-dot-authors-status
/usr/bin/download --license > /tmp/notices-download-license 2> /tmp/notices-download-license-error
printf '%s\n' "$?" > /tmp/notices-download-license-status
/usr/bin/download --authors > /tmp/notices-download-authors 2> /tmp/notices-download-authors-error
printf '%s\n' "$?" > /tmp/notices-download-authors-status
/usr/bin/echo --license > /tmp/notices-echo-license 2> /tmp/notices-echo-license-error
printf '%s\n' "$?" > /tmp/notices-echo-license-status
/usr/bin/echo --authors > /tmp/notices-echo-authors 2> /tmp/notices-echo-authors-error
printf '%s\n' "$?" > /tmp/notices-echo-authors-status
/usr/bin/false --license > /tmp/notices-false-license 2> /tmp/notices-false-license-error
printf '%s\n' "$?" > /tmp/notices-false-license-status
/usr/bin/false --authors > /tmp/notices-false-authors 2> /tmp/notices-false-authors-error
printf '%s\n' "$?" > /tmp/notices-false-authors-status
/usr/bin/ldd --license > /tmp/notices-ldd-license 2> /tmp/notices-ldd-license-error
printf '%s\n' "$?" > /tmp/notices-ldd-license-status
/usr/bin/ldd --authors > /tmp/notices-ldd-authors 2> /tmp/notices-ldd-authors-error
printf '%s\n' "$?" > /tmp/notices-ldd-authors-status
/usr/bin/lld-link --license > /tmp/notices-lld-link-license 2> /tmp/notices-lld-link-license-error
printf '%s\n' "$?" > /tmp/notices-lld-link-license-status
/usr/bin/lld-link --authors > /tmp/notices-lld-link-authors 2> /tmp/notices-lld-link-authors-error
printf '%s\n' "$?" > /tmp/notices-lld-link-authors-status
/usr/bin/ls --license > /tmp/notices-ls-license 2> /tmp/notices-ls-license-error
printf '%s\n' "$?" > /tmp/notices-ls-license-status
/usr/bin/ls --authors > /tmp/notices-ls-authors 2> /tmp/notices-ls-authors-error
printf '%s\n' "$?" > /tmp/notices-ls-authors-status
/usr/bin/m4 --license > /tmp/notices-m4-license 2> /tmp/notices-m4-license-error
printf '%s\n' "$?" > /tmp/notices-m4-license-status
/usr/bin/m4 --authors > /tmp/notices-m4-authors 2> /tmp/notices-m4-authors-error
printf '%s\n' "$?" > /tmp/notices-m4-authors-status
/usr/bin/openssl --license > /tmp/notices-openssl-license 2> /tmp/notices-openssl-license-error
printf '%s\n' "$?" > /tmp/notices-openssl-license-status
/usr/bin/openssl --authors > /tmp/notices-openssl-authors 2> /tmp/notices-openssl-authors-error
printf '%s\n' "$?" > /tmp/notices-openssl-authors-status
/usr/bin/printf --license > /tmp/notices-printf-license 2> /tmp/notices-printf-license-error
printf '%s\n' "$?" > /tmp/notices-printf-license-status
/usr/bin/printf --authors > /tmp/notices-printf-authors 2> /tmp/notices-printf-authors-error
printf '%s\n' "$?" > /tmp/notices-printf-authors-status
/usr/bin/pwd --license > /tmp/notices-pwd-license 2> /tmp/notices-pwd-license-error
printf '%s\n' "$?" > /tmp/notices-pwd-license-status
/usr/bin/pwd --authors > /tmp/notices-pwd-authors 2> /tmp/notices-pwd-authors-error
printf '%s\n' "$?" > /tmp/notices-pwd-authors-status
/usr/bin/rogue --license > /tmp/notices-rogue-license 2> /tmp/notices-rogue-license-error
printf '%s\n' "$?" > /tmp/notices-rogue-license-status
/usr/bin/rogue --authors > /tmp/notices-rogue-authors 2> /tmp/notices-rogue-authors-error
printf '%s\n' "$?" > /tmp/notices-rogue-authors-status
/usr/bin/sleep --license > /tmp/notices-sleep-license 2> /tmp/notices-sleep-license-error
printf '%s\n' "$?" > /tmp/notices-sleep-license-status
/usr/bin/sleep --authors > /tmp/notices-sleep-authors 2> /tmp/notices-sleep-authors-error
printf '%s\n' "$?" > /tmp/notices-sleep-authors-status
/usr/bin/true --license > /tmp/notices-true-license 2> /tmp/notices-true-license-error
printf '%s\n' "$?" > /tmp/notices-true-license-status
/usr/bin/true --authors > /tmp/notices-true-authors 2> /tmp/notices-true-authors-error
printf '%s\n' "$?" > /tmp/notices-true-authors-status
/usr/bin/upload --license > /tmp/notices-upload-license 2> /tmp/notices-upload-license-error
printf '%s\n' "$?" > /tmp/notices-upload-license-status
/usr/bin/upload --authors > /tmp/notices-upload-authors 2> /tmp/notices-upload-authors-error
printf '%s\n' "$?" > /tmp/notices-upload-authors-status
/usr/bin/vim --license > /tmp/notices-vim-license 2> /tmp/notices-vim-license-error
printf '%s\n' "$?" > /tmp/notices-vim-license-status
/usr/bin/vim --authors > /tmp/notices-vim-authors 2> /tmp/notices-vim-authors-error
printf '%s\n' "$?" > /tmp/notices-vim-authors-status
/usr/bin/wasm-ld --license > /tmp/notices-wasm-ld-license 2> /tmp/notices-wasm-ld-license-error
printf '%s\n' "$?" > /tmp/notices-wasm-ld-license-status
/usr/bin/wasm-ld --authors > /tmp/notices-wasm-ld-authors 2> /tmp/notices-wasm-ld-authors-error
printf '%s\n' "$?" > /tmp/notices-wasm-ld-authors-status
/usr/bin/wc --license > /tmp/notices-wc-license 2> /tmp/notices-wc-license-error
printf '%s\n' "$?" > /tmp/notices-wc-license-status
/usr/bin/wc --authors > /tmp/notices-wc-authors 2> /tmp/notices-wc-authors-error
printf '%s\n' "$?" > /tmp/notices-wc-authors-status
/usr/bin/m4 --license | /usr/bin/cat > /tmp/notices-m4-pipe
printf '%s\n' "$?" > /tmp/notices-m4-pipe-status
/usr/bin/vim --authors | /usr/bin/cat > /tmp/notices-vim-pipe
printf '%s\n' "$?" > /tmp/notices-vim-pipe-status
/usr/bin/rogue --license | /usr/bin/cat > /tmp/notices-rogue-pipe
printf '%s\n' "$?" > /tmp/notices-rogue-pipe-status
/usr/bin/clang --authors | /usr/bin/cat > /tmp/notices-clang-pipe
printf '%s\n' "$?" > /tmp/notices-clang-pipe-status
printf 'm4exit(99)\n' | /usr/bin/m4 --authors > /tmp/notices-stdin
printf '%s\n' "$?" > /tmp/notices-stdin-status
/usr/bin/printf -- '%s\n' --authors > /tmp/notices-literal
/usr/bin/echo --authors extra > /tmp/notices-echo-extra
POSIXLY_CORRECT=1 /usr/bin/echo --authors > /tmp/notices-echo-posix
/usr/bin/true --authors extra > /tmp/notices-true-extra
printf '%s\n' "$?" > /tmp/notices-true-status
/usr/bin/false --authors extra > /tmp/notices-false-extra
printf '%s\n' "$?" > /tmp/notices-false-status
/usr/bin/upload --license >&- 2> /tmp/notices-write-error
printf '%s\n' "$?" > /tmp/notices-write-status
printf '%s\n' --authors > /tmp/notices-expect-authors
printf '%s\n' '--authors extra' > /tmp/notices-expect-extra
/usr/bin/clang /root/test/system/notices/sdk-plain.c -o /tmp/notices-sdk-probe > /tmp/notices-sdk-compile 2> /tmp/notices-sdk-compile-error
printf '%s\n' "$?" > /tmp/notices-sdk-compile-status
/tmp/notices-sdk-probe --authors > /tmp/notices-sdk-authors 2> /tmp/notices-sdk-authors-error
printf '%s\n' "$?" > /tmp/notices-sdk-authors-status
/tmp/notices-sdk-probe --license > /tmp/notices-sdk-license 2> /tmp/notices-sdk-license-error
printf '%s\n' "$?" > /tmp/notices-sdk-license-status
printf '%s\n' --license > /tmp/notices-expect-license
