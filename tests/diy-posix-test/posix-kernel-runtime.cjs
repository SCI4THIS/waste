"use strict";

const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const mode = process.argv[2] === "threaded" ? "threaded" : "sequential";
const loaderPath = path.join(
  root,
  mode === "threaded" ? "build/ocaml-wasm/dist-threaded" : "build/ocaml-wasm/dist",
  "wasm_cli.bc.wasm.js"
);
const source = fs.readFileSync(path.join(__dirname, "posix-kernel.wast"), "utf8");

globalThis.waste_exit_code = 0;
process.argv = [process.execPath, loaderPath, "--schedule", "-q", "50", "-e", source];
require.main.filename = loaderPath;

(async () => {
  const completion = eval(fs.readFileSync(loaderPath, "utf8"));
  await completion;
  if (globalThis.waste_exit_code !== 0)
    throw new Error(`interpreter exited with ${globalThis.waste_exit_code}`);
  console.log(`POSIX kernel (${mode}): pass`);
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
