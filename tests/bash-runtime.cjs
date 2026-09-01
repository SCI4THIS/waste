"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {spawnSync} = require("node:child_process");

const root = path.resolve(__dirname, "..");
const mode = process.argv[2] === "threaded" ? "threaded" : "sequential";
const command = process.argv[3] || "echo WASTE_BASH_OK";
if (!/^[\x20-\x7e]*$/.test(command) || command.includes("\\") || command.includes('"'))
  throw new Error("smoke-test command must be printable ASCII without quotes or backslashes");
if (process.env.WASTE_BASH_SMOKE_CHILD !== "1") {
  const child = spawnSync(process.execPath, [__filename, mode, command], {
    cwd: root,
    encoding: "utf8",
    env: {...process.env, WASTE_BASH_SMOKE_CHILD: "1"},
  });
  process.stdout.write(child.stdout || "");
  process.stderr.write(child.stderr || "");
  const taskPassed = /WASTE_RESULT 0 0/.test(child.stdout || "");
  const outputPassed = command !== "echo WASTE_BASH_OK" ||
    (child.stdout || "").includes("WASTE_BASH_OK");
  if (child.status !== 0 || !taskPassed || !outputPassed) {
    console.error("bash runtime smoke test failed");
    process.exit(1);
  }
  console.log(`bash runtime (${mode}): pass`);
  process.exit(0);
}
const loaderPath = path.join(
  root,
  mode === "threaded" ? "build/ocaml-wasm/dist-threaded" : "build/ocaml-wasm/dist",
  "wasm_cli.bc.wasm.js"
);
const source = fs.readFileSync(path.join(root, "build/bash/bash-runtime.wast"), "utf8")
  .replace("__WASTE_BASH_COMMAND__", command);

globalThis.waste_exit_code = 0;
process.argv = [process.execPath, loaderPath, "-ca", "--schedule", "-q", "1000000", "--threads", "1"];
if (process.env.WASTE_TRACE === "1") process.argv.push("-t");
process.argv.push("-e", source);
require.main.filename = loaderPath;

(async () => {
  const completion = eval(fs.readFileSync(loaderPath, "utf8"));
  await completion;
  if (globalThis.waste_exit_code !== 0)
    throw new Error(`Bash exited with ${globalThis.waste_exit_code}`);
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
