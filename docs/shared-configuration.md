# Shared runtime configuration

`src/config.h` is the source for numeric settings that must agree across C,
Python packaging, JavaScript runtimes, and host tests. Change a setting there;
do not add another literal and an equality test to keep it synchronized.

`src/html-rt/tools/runtime_config.py` reads the same integer macros, including
references and arithmetic expressions. The VFS installer uses it for limits
and prepends generated configuration to the installed worker and suite
controller. These scripts remain in the compressed VFS inside `bash.html`.
Node harnesses use `tests/runtime-config.cjs` to load
`build/engine/runtime-config.json`, emitted by the same reader during the
engine build. This does not start a subprocess for each harness invocation.
Staging builds emit `build/html-rt/runtime-config.js` for the source page.

| Configuration | Consumers |
| --- | --- |
| VFS entries, bytes, metadata bytes, path bytes, runtime reserve | C inventory/kernel, Python installer and boundary probes |
| Data/element segment limits | Parser/executor and generated overflow cases in both host harnesses |
| JSON nesting and shebang bounds | C readers and generated malformed-input cases |
| Descriptor and terminal input capacities | C kernel, portable exhaustion tests, browser input queue |
| Execution deadline and pump ceilings | C browser API, native session, production worker and control probes |
| Suite deadlines, jobs, output and guest request/reply bounds | Native companion, guest launcher, engine capability adapter and browser controller |
| Browser result name/error storage and setup error storage | C report records and JavaScript readers |
| Renderer diagnostic reply capacity | Engine/browser capability, worker reply validation; queried by the WAST guest |
| Legacy harness deadlines | Native/browser Node harnesses, kept distinct where their budgets differ |

Boundary tests construct a case beyond the configured limit and verify its
rejection. Descriptor tests discover exhaustion and check allocation, error
handling, and recycling without assuming a particular table size. Literal
capacity equality checks and header-spelling checks have been removed.

Corpus counts and expected full-suite coverage come from the manifest and
selected identities. They are derived data, so they are not settings in the
header. Adding a test no longer requires updating pinned total counts in the
packaging and full-browser gates.

Wasm encoding values, POSIX error numbers, guest ABI layouts, and numeric
inputs/expected results within a test continue to describe their respective
language or ABI contracts. They are not deployment settings.

After changing shared configuration, rebuild the affected C runtimes and
guest launcher, install the app/launcher snapshots, and regenerate `bash.html`.
Installed snapshots carry the configuration used when they were installed;
an existing offline page does not read the host's current header.
