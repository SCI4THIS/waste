"use strict";

const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const mode = process.argv[2] === "threaded" ? "threaded" : "sequential";
const fixtureFilter = process.argv[3];
const loaderPath = path.join(
  root,
  mode === "threaded" ? "build/ocaml/dist-threaded" : "build/ocaml/dist",
  "wasm_cli.bc.wasm.js"
);
const fixtureRoot = path.join(root, "build/html-rt/waste-libc/tests");
/* The OCaml oracle has no C-engine host resolver.  Provide only the small,
 * deterministic synchronous kernel surface used by libc wrapper fixtures.
 * C-engine/browser fixtures remain unprovided and therefore exercise the real
 * waste_kernel host imports. */
const oracleKernel = `(module $waste_kernel
  (func (export "select_v1") (param $nfds i32) (param i32) (param i32)
        (param i32) (param i32) (result i32)
    local.get $nfds i32.const 0 i32.lt_s
    if (result i32) i32.const -22 else i32.const 0 end)
  (func (export "pselect_v1") (param $nfds i32) (param i32) (param i32)
        (param i32) (param i32) (param i32) (result i32)
    local.get $nfds i32.const 0 i32.lt_s
    if (result i32) i32.const -22 else i32.const 0 end))
(register "waste_kernel" $waste_kernel)
`;
const fixtures = fs.readdirSync(fixtureRoot)
  .filter(name => name.endsWith(".wast"))
  .filter(name => !fixtureFilter || name === fixtureFilter)
  .sort()
.map(name => ({name, source: oracleKernel + fs.readFileSync(path.join(fixtureRoot, name), "utf8")}));

globalThis.waste_exit_code = 0;
process.argv = [
  process.execPath,
  loaderPath,
  "-ca",
  "--schedule",
  "-q",
  "10000",
  "--threads",
  String(fixtures.length)
];
for (const fixture of fixtures) process.argv.push("-e", fixture.source);
require.main.filename = loaderPath;

(async () => {
  const completion = eval(fs.readFileSync(loaderPath, "utf8"));
  await completion;
  if (globalThis.waste_exit_code !== 0)
    throw new Error(`interpreter exited with ${globalThis.waste_exit_code}`);
  console.log(`waste-libc (${mode}): ${fixtures.length} suites pass`);
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
