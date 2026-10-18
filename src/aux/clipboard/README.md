# Clipboard command

Build with `make -C src/aux clipboard`, explicitly publish with
`make -C src/aux install-clipboard`, and test with `make -C src/aux test-clipboard`.
The authored C command uses the installed guest SDK, CRT and shared libc.
`start.sh` exposes the same install target. Build outputs stay under
`build/aux/clipboard`; installation publishes `/usr/bin/clipboard`.

In a freshly opened `build/html-rt/bash.html`:

```sh
clipboard copy /root/demo/windows-hello.c
clipboard copy /tmp/hello.exe
clipboard copy --base64 /tmp/hello.exe
```

Each command opens a browser dialog. Press **Copy to clipboard** to authorize
copying, or **Cancel** / Escape to return a nonzero status. Bash resumes after
copy/cancellation. The selected text remains visible for inspection or retry
if clipboard access is denied. The page supports offline Firefox using a
selected textarea and `document.execCommand('copy')` from an actual click;
`navigator.clipboard.writeText` is the alternative where available. No server
or browser extension is needed. Restart/stop discards pending UI replies.

Plain copy treats file bytes as UTF-8 text (malformed sequences become replacement
characters). It accepts binary files for experimentation, but a text clipboard,
textarea and editor can change invalid bytes, embedded NULs and line endings.
Seeing unusual symbols in Notepad does not establish a byte-preserving copy.
Base64 emits ASCII with standard padding and no added newline, preserving every
input byte after decoding. Empty files are valid. `--` permits filenames starting
with a dash. The maximum outgoing payload is `CLIPBOARD_MAX_BYTES` in
`src/config.h`; Base64 expansion counts toward that limit. Only regular files
are accepted. Success is quiet; errors go to stderr and return 1.

For a Windows executable, use Base64, paste into Notepad and save as
`hello.exe.b64` (choose **All files** and UTF-8). In Windows Command Prompt or
PowerShell, decode and compare with the original download's hash:

```powershell
certutil -decode hello.exe.b64 hello.exe
certutil -hashfile hello.exe SHA256
```

`certutil` normally ships with Windows 10/11; its
[documented -decode command](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/certutil#-decode)
accepts Base64 files. If unavailable, PowerShell can decode with .NET:

```powershell
[IO.File]::WriteAllBytes("$PWD\hello.exe", [Convert]::FromBase64String([IO.File]::ReadAllText("$PWD\hello.exe.b64")))
```

## Runtime boundary and checks

The command encodes Base64 and calls
`waste_kernel.host_clipboard_copy_v1(data, length, flags=0)`. The engine copies
and bounds the payload, yields through existing host I/O, frees its copy after
acknowledgment, and returns 0 or -1. The browser worker transports bytes and
requires an ID-matched reply; the main thread owns clipboard access. This adds
no Asyncify or host desktop API to the engine. The native runtime currently
has no granted desktop clipboard capability: valid copies fail cleanly while
Bash continues. It does not silently write clipboard escape sequences to stdout.

`test/system` points to canonical tests at `/root/test/aux/clipboard`. The native
setup captures invalid command/file cases and unsupported-host behavior; WAST
owns the assertions, including invalid ABI flags, pointer and length handling.
The browser setup exercises text, binary/Base64, all padding lengths, empty
files, UTF-8 and cancellation. Its host harness returns captured clipboard
payloads through the existing upload transport for WAST byte/status assertions.

```sh
node src/aux/clipboard/test/browser-adapter.cjs
node src/aux/clipboard/test/browser.cjs build/html-rt/bash.html
```

The first checks actual authored DOM adapter lifecycle with simulated clipboard
APIs. The second runs the installed command in production guest Bash/worker;
it does not modify the host clipboard. Real offline clipboard permission and
paste behavior still require a manual browser check. `browser/setup.sh` uses
scripted upload responses and is for the transport harness, not manual sourcing.
For manual acceptance, copy a known text file, try cancellation and `echo $?`,
then copy a downloaded executable as Base64, decode on Windows and compare hashes.
