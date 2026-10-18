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
