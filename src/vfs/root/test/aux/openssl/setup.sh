# Fixed AES salt and low PBKDF2 count are regression vectors only.
# Commands only; cli.wast checks statuses and file contents.
printf abc > /tmp/openssl-plain
/usr/bin/openssl version > /tmp/openssl-version
printf '%s\n' "$?" > /tmp/openssl-status-version
/usr/bin/openssl dgst -sha256 -binary -out /tmp/openssl-digest /tmp/openssl-plain
printf '%s\n' "$?" > /tmp/openssl-status-digest
/usr/bin/openssl enc -aes-256-cbc -S 0011223344556677 -pbkdf2 -iter 10 -salt -pass pass:waste-test -in /tmp/openssl-plain -out /tmp/openssl-encrypted
printf '%s\n' "$?" > /tmp/openssl-status-encrypt-aes
/usr/bin/openssl enc -d -aes-256-cbc -S 0011223344556677 -pbkdf2 -iter 10 -pass pass:waste-test -in /tmp/openssl-encrypted -out /tmp/openssl-aes-decoded
printf '%s\n' "$?" > /tmp/openssl-status-decrypt-aes
/usr/bin/openssl enc -d -aes-256-cbc -S 0011223344556677 -pbkdf2 -iter 10 -pass pass:wrong -in /tmp/openssl-encrypted -out /tmp/openssl-bad
printf '%s\n' "$?" > /tmp/openssl-status-bad-password
/usr/bin/openssl enc -chacha20 -pbkdf2 -iter 10 -pass pass:waste-test -in /tmp/openssl-plain -out /tmp/openssl-chacha
printf '%s\n' "$?" > /tmp/openssl-status-encrypt-chacha
/usr/bin/openssl enc -d -chacha20 -pbkdf2 -iter 10 -pass pass:waste-test -in /tmp/openssl-chacha -out /tmp/openssl-chacha-decoded
printf '%s\n' "$?" > /tmp/openssl-status-decrypt-chacha
/usr/bin/openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out /tmp/openssl-key.pem
printf '%s\n' "$?" > /tmp/openssl-status-generate-key
/usr/bin/openssl pkey -in /tmp/openssl-key.pem -check -noout
printf '%s\n' "$?" > /tmp/openssl-status-check-key
/usr/bin/openssl req -new -key /tmp/openssl-key.pem -subj /CN=waste.test -out /tmp/openssl.csr
printf '%s\n' "$?" > /tmp/openssl-status-create-csr
/usr/bin/openssl req -in /tmp/openssl.csr -verify -noout
printf '%s\n' "$?" > /tmp/openssl-status-verify-csr
/usr/bin/openssl req -x509 -new -key /tmp/openssl-key.pem -subj /CN=waste.test -days 1 -out /tmp/openssl-cert.pem
printf '%s\n' "$?" > /tmp/openssl-status-create-certificate
/usr/bin/openssl verify -CAfile /tmp/openssl-cert.pem /tmp/openssl-cert.pem
printf '%s\n' "$?" > /tmp/openssl-status-verify-certificate
