# Staged file identities

The guest assertions live once in
`src/vfs/root/test/system/cli-runtime/staged-identities.wast`. The native shell
driver and browser JSON descriptor stage identical bytes at two paths and run
that WAST from the installed Bash. They check distinct identities for separate
files, stable descriptor identity, and separation from guest-created and
catalogue-mounted files.

From this checkout:

```sh
make -C src/cli-rt staged-identities
make -C src/cli-rt staged-identities-sanitize
node src/html-rt/test/staged-identities.cjs build/html-rt/waste-wast.wasm src/vfs
```

The browser wrapper reuses the API session harness and changes only fixture
metadata: Bash receives valid catalogue inode 1000, exposing the former
staging-counter collision. All functional assertions remain in WAST. Expect
six guest assertions, or seven browser results including launch, and exit 0.
The sanitizer gate requires a host environment that permits LeakSanitizer.

Native results are saved under `build/cli-rt/`; redirect browser stdout to a
JSON file under `build/engine/logs/`. The wrapper currently imports baseline
`tests/` harness helpers; reconcile those paths with the test migration at the
integration handoff. These API checks do not establish offline page behavior.
