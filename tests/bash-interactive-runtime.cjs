"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {spawnSync} = require("node:child_process");

const root = path.resolve(__dirname, "..");
if (process.env.WASTE_BASH_INTERACTIVE_CHILD !== "1") {
  const startedAt = Date.now();
  const child = spawnSync(process.execPath, [__filename], {
    cwd: root,
    encoding: "utf8",
    env: {...process.env, WASTE_BASH_INTERACTIVE_CHILD: "1"},
  });
  process.stdout.write(child.stdout || "");
  process.stderr.write(child.stderr || "");
  if (child.status !== 0 || !/WASTE_RESULT 0 0/.test(child.stdout || "") ||
      !(child.stdout || "").includes("bash-5.2#")) {
    console.error("interactive Bash smoke test failed");
    process.exit(1);
  }
  console.log(`bash runtime (interactive): pass (${((Date.now() - startedAt) / 1000).toFixed(3)} s by Date)`);
  process.exit(0);
}

const loaderPath = path.join(root, "build/ocaml-wasm/dist-threaded/wasm_cli.bc.wasm.js");
const source = fs.readFileSync(path.join(root, "build/bash/bash-runtime.wast"), "utf8");
globalThis.waste_control_page = new Int32Array(4 + 256);
let sequence = 0;
for (const byte of new TextEncoder().encode("\nexit\n")) {
  sequence++;
  globalThis.waste_control_page[4 + (sequence & 255)] = (4 << 16) | byte;
}
globalThis.waste_control_page[0] = sequence;
globalThis.waste_exit_code = 0;
process.argv = [process.execPath, loaderPath, "-ca", "--schedule", "--control-page"];
if (process.env.WASTE_TRACE === "1") process.argv.push("-t");
process.argv.push("-q", "1000000", "--threads", "1", "-e", source);
require.main.filename = loaderPath;

(async () => {
  const completion = eval(fs.readFileSync(loaderPath, "utf8"));
  await completion;
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
