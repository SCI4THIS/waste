"use strict";

const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const mode = process.argv[2] === "threaded" ? "threaded" : "sequential";
const fixtureFilter = process.argv[3];
const loaderPath = path.join(
  root,
  mode === "threaded" ? "build/ocaml-wasm/dist-threaded" : "build/ocaml-wasm/dist",
  "wasm_cli.bc.wasm.js"
);
const fixtureRoot = path.join(root, "build/waste-libc/tests");
const fixtures = fs.readdirSync(fixtureRoot)
  .filter(name => name.endsWith(".wast"))
  .filter(name => !fixtureFilter || name === fixtureFilter)
  .sort()
  .map(name => ({name, source: fs.readFileSync(path.join(fixtureRoot, name), "utf8")}));

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
