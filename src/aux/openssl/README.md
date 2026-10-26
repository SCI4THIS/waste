# OpenSSL guest package

Upstream: read-only `submodules/openssl` (OpenSSL 3.5.9). Repository-owned
configuration and patches stage under `build/aux/openssl`; host Perl and
Text::Template (upstream includes a fallback), Clang, llvm-ar, llvm-ranlib,
wasm-ld, patch, Make and Python 3 are required.

```
make -C src/aux install-libc
make -C src/aux -j4 openssl libcrypto
make -C src/aux install-openssl
make -C src/aux test-openssl
```

`libcrypto` builds `build/aux/openssl/libcrypto.so.wasm`; `openssl` builds
`build/aux/openssl/openssl.wasm`. `install-libcrypto` publishes `/lib` and
`/usr/lib` copies, matching public OpenSSL headers, `/etc/ssl/openssl.cnf`, and
the Apache-2.0 license. `install-openssl` additionally installs `/usr/bin/openssl`.
`test-libcrypto` and `test-openssl` run the canonical WAST package tests in
Bash. They cover libcrypto SHA-256/RAND APIs, digest output, AES and ChaCha20
file encryption/decryption, wrong-password rejection, EC private keys, CSRs,
and self-signed certificates. Public CLI exit status and file bytes are tested.
Tests use fixed AES salt and a low PBKDF2 iteration count only as regression
vectors, not as recommended encryption settings. The native package gate allows
five minutes by default (`WASTE_TEST_TIMEOUT_MS` overrides this); loading the
large provider for each command is expensive in the interpreter.

The CLI uses shared libcrypto and guest libc; its TLS support code is linked
from upstream libssl.a. There is no separate libssl.so.wasm. Providers are built
in; assembly, threads, async jobs, engines, dynamic modules, secure-memory OS
facilities, QUIC and socket commands are disabled. Socket ABI entry points in
guest libc return -1/EOPNOTSUPP without touching output buffers. Network TLS
commands require a later socket/broker implementation. Crypto operations use
native OS entropy or browser Web Crypto through the versioned guest ABI;
unavailable entropy fails, with no deterministic fallback. The unrelated
compatibility rand/arc4random functions are not cryptographic RNGs.

In guest Bash:

```
printf 'hello\n' > /tmp/plain.txt
openssl enc -aes-256-cbc -salt -pbkdf2 -pass pass:example -in /tmp/plain.txt -out /tmp/secret.enc
openssl enc -d -aes-256-cbc -pbkdf2 -pass pass:example -in /tmp/secret.enc -out /tmp/plain-again.txt
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out /tmp/key.pem
openssl req -new -key /tmp/key.pem -subj /CN=example.test -out /tmp/request.pem
openssl req -new -x509 -key /tmp/key.pem -subj /CN=example.test -days 365 -out /tmp/cert.pem
openssl x509 -in /tmp/cert.pem -noout -subject -dates
```

Use `-pass file:PATH` or an interactive prompt instead of the illustrative
command-line password when working with secrets. Certificate creation does not
make a certificate publicly trusted. No system CA trust bundle is installed.

Rebuild the browser engine and package using the normal HTML workflow after
installation; packaging never builds or installs OpenSSL itself. Run the same
WAST files from `/root/test/aux/openssl/` in the offline page, after running
`. /root/test/aux/openssl/setup.sh`. The production worker acceptance command is:

```
node src/html-rt/test/guest-session/worker.cjs build/html-rt/bash.html src/aux/openssl/test/browser.json
```

The command setup records statuses; WAST owns every functional assertion. Kernel entropy
bounds tests live at `/root/test/system/entropy/getrandom.wast`; libc errno and
socket checks live at `/root/test/aux/libc/entropy-sockets.wast`.
