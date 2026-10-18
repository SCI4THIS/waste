# Bash WAST reporting

These sources exercise `/bin/wast` itself through Bash. Install snapshots with
`make -C src/html-rt vfs-tests-install` and rebuild `bash.html`. From its prompt:

```sh
/bin/wast --verbose /root/test/html-rt/pass.wast > /tmp/wast-report.txt
/bin/wast /root/test/html-rt/pass.wast > /tmp/wast-quiet.txt
/bin/wast --verbose /root/test/html-rt/report-files.wast
```

`report-files.wast` checks the exact report bytes and empty quiet output. It
prints two dots and `WAST: 2 PASS, 0 FAIL, 2 total`. Report bytes use LF even
when the terminal displays CRLF. View or download the report:

```sh
cat /tmp/wast-report.txt
download /tmp/wast-report.txt
```

The negative control intentionally fails its middle assertion, continues to
the last assertion, prints `.F.` and `WAST: 2 PASS, 1 FAIL, 3 total`, then the
first mismatch. Capture the status in the same command:

```sh
/bin/wast --verbose /root/test/html-rt/report.fail.wast; printf 'status: %s\n' "$?"
/bin/wast --verbose /root/test/html-rt/pass.wast
```

Expected statuses are 1 then 0, with a fresh three-pass summary on the second
run. The same files run from native Bash. These process-context tests are
listed as skipped by isolated language batches; `report.fail.wast` is a
reporting control, not a conformance failure baseline.
