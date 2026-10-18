# Capture observations only; WAST owns all assertions.
printf 'Hello from WASTE!\n' > /tmp/clipboard-text
printf '\000\200\377A' > /tmp/clipboard-binary
printf '' > /tmp/clipboard-empty
printf f > /tmp/clipboard-one
printf fo > /tmp/clipboard-two
printf foo > /tmp/clipboard-three
printf 'caf\303\251' > /tmp/clipboard-utf8
/usr/bin/clipboard > /tmp/clipboard-no-arguments-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-no-arguments-status
/usr/bin/clipboard copy > /tmp/clipboard-missing-file-argument-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-missing-file-argument-status
/usr/bin/clipboard paste /tmp/clipboard-text > /tmp/clipboard-unknown-subcommand-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-unknown-subcommand-status
/usr/bin/clipboard copy --unknown /tmp/clipboard-text > /tmp/clipboard-unknown-option-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-unknown-option-status
/usr/bin/clipboard copy /tmp/clipboard-text extra > /tmp/clipboard-extra-argument-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-extra-argument-status
/usr/bin/clipboard copy /tmp/clipboard-absent > /tmp/clipboard-missing-file-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-missing-file-status
/usr/bin/clipboard copy /tmp > /tmp/clipboard-directory-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-directory-status
/usr/bin/clipboard --help > /tmp/clipboard-help-output 2>&1
printf '%s\n' "$?" > /tmp/clipboard-help-status
/usr/bin/clipboard copy /tmp/clipboard-text
printf '%s\n' "$?" > /tmp/clipboard-text-copy-status
upload /tmp/clipboard-text-captured
/usr/bin/clipboard copy --base64 /tmp/clipboard-binary
printf '%s\n' "$?" > /tmp/clipboard-binary-copy-status
upload /tmp/clipboard-binary-captured
/usr/bin/clipboard copy --base64 /tmp/clipboard-empty
printf '%s\n' "$?" > /tmp/clipboard-empty-copy-status
upload /tmp/clipboard-empty-captured
/usr/bin/clipboard copy --base64 /tmp/clipboard-one
printf '%s\n' "$?" > /tmp/clipboard-one-copy-status
upload /tmp/clipboard-one-captured
/usr/bin/clipboard copy --base64 /tmp/clipboard-two
printf '%s\n' "$?" > /tmp/clipboard-two-copy-status
upload /tmp/clipboard-two-captured
/usr/bin/clipboard copy --base64 /tmp/clipboard-three
printf '%s\n' "$?" > /tmp/clipboard-three-copy-status
upload /tmp/clipboard-three-captured
/usr/bin/clipboard copy /tmp/clipboard-utf8
printf '%s\n' "$?" > /tmp/clipboard-utf8-copy-status
upload /tmp/clipboard-utf8-captured
/usr/bin/clipboard copy /tmp/clipboard-binary
printf '%s\n' "$?" > /tmp/clipboard-raw-binary-copy-status
upload /tmp/clipboard-raw-binary-captured
/usr/bin/clipboard copy /tmp/clipboard-text
printf '%s\n' "$?" > /tmp/clipboard-cancel-status
