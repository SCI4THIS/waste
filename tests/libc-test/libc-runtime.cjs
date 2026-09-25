"use strict";

const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const mode = process.argv[2] === "threaded" ? "threaded" : "sequential";
const fixtureFilter = process.argv[3];
const assertionFilter = process.argv[4];
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
  (func (export "tcgetattr_v1") (param i32 i32) (result i32)
    i32.const -25)
  (func (export "tcsetattr_v1") (param i32 i32 i32) (result i32)
    i32.const -25)
  (func (export "startup_v1") (result i32) i32.const 0)
  (func (export "isatty_v1") (param i32) (result i32) i32.const 0)
  (func (export "path_access_v1") (param i32 i32 i32 i32) (result i32)
    i32.const -2)
  (func (export "path_stat_v1") (param i32 i32 i32 i32) (result i32)
    i32.const -38)
  (func (export "ioctl_v1") (param i32 i32 i32) (result i32)
    i32.const -38)
  (func (export "select_v1") (param $nfds i32) (param i32) (param i32)
        (param i32) (param i32) (result i32)
    local.get $nfds i32.const 0 i32.lt_s
    if (result i32) i32.const -22 else i32.const 0 end)
  (func (export "pselect_v1") (param $nfds i32) (param i32) (param i32)
        (param i32) (param i32) (param i32) (result i32)
    local.get $nfds i32.const 0 i32.lt_s
    if (result i32) i32.const -22 else i32.const 0 end))
(register "waste_kernel" $waste_kernel)
(module $env
  (func (export "open") (param i32 i32 i32) (result i32) i32.const -38)
  (func (export "close") (param i32) (result i32) i32.const -38)
  (func (export "readdir_v1") (param i32 i32 i32 i32) (result i32) i32.const -38)
  (func (export "chdir") (param i32) (result i32) i32.const -38)
  (func (export "getcwd") (param i32 i32) (result i32) i32.const -38)
  (func (export "execve") (param i32 i32 i32) (result i32) i32.const -38)
  (func (export "readlink") (param i32 i32 i32) (result i32) i32.const -38))
(register "env" $env)
`;
const fixtures = fs.readdirSync(fixtureRoot)
  .filter(name => name.endsWith(".wast"))
  .filter(name => !fixtureFilter || name === fixtureFilter)
  .sort()
.map(name => {
  let source = fs.readFileSync(path.join(fixtureRoot, name), "utf8");
  if (assertionFilter)
    source = source.replace(/^\(assert_return[^\n]*\n/gm,
      line => line.includes(`"${assertionFilter}"`) ? line : "");
  return {name, source: oracleKernel + source};
});

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
