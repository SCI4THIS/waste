#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const {TextDecoder, TextEncoder} = require("node:util");

const root = path.resolve(__dirname, "..");
const stagingDir = path.join(root, "src/html-rt/src/bash");

/* Load worker source, wasm, and launch script from staging files */
const workerSrc = fs.readFileSync(path.join(stagingDir, "worker.js"), "utf8");

/* Resolve waste-wast.wasm: staging symlink or build directory */
let wasmPath = path.join(stagingDir, "waste-wast.wasm");
if (!fs.existsSync(wasmPath)) {
  wasmPath = path.join(root, "build/html-rt/waste-wast.wasm");
}
const wasmBytes = fs.readFileSync(wasmPath);

/* Resolve launch.wast: staging symlink or build directory */
let launchPath = path.join(stagingDir, "launch.wast");
if (!fs.existsSync(launchPath)) {
  launchPath = path.join(root, "build/html-rt/bash-runtime.wast");
}
const launchSource = fs.readFileSync(launchPath, "utf8");

let output = "";
let promptSeen = false;
let commandSent = false;
let exitSent = false;
let finish;
const completion = new Promise(resolve => { finish = resolve; });

const self = {
  postMessage(message) {
    if (message.type === "output") {
      output += message.text;
      process.stdout.write(message.text);
      if (!promptSeen && /bash-[^\r\n]*[#$] ?/.test(output)) {
        promptSeen = true;
        setTimeout(() => {
          commandSent = true;
          self.onmessage({data: {
            type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "echo __C_ENGINE_BASH_OK__\n",
            )),
          }});
        }, 10);
      }
      if (commandSent && !exitSent && output.includes("__C_ENGINE_BASH_OK__")) {
        exitSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("exit\n")),
        }}), 10);
      }
    } else if (message.type === "done") {
      finish(message);
    }
  },
};

const context = vm.createContext({
  self,
  WebAssembly,
  Uint8Array,
  DataView,
  TextDecoder,
  TextEncoder,
  Promise,
  Math,
  Number,
  String,
  Error,
  setTimeout,
  clearTimeout,
  atob: text => Buffer.from(text, "base64").toString("binary"),
});
vm.runInContext(workerSrc, context, {filename: "c-engine-bash-worker.js"});
self.onmessage({data: {
  type: "start",
  wasmBytes: wasmBytes.buffer,
  source: launchSource,
}});

let timeoutId;
const timeout = new Promise(resolve => {
  timeoutId = setTimeout(
    () => resolve({error: "timed out waiting for Bash"}), 30000,
  );
});

Promise.race([completion, timeout]).then(result => {
  clearTimeout(timeoutId);
  const passed = promptSeen && commandSent && exitSent &&
    output.includes("__C_ENGINE_BASH_OK__") && result.ok;
  if (!passed) {
    console.error("\nC-engine Bash browser test failed:", result);
    process.exitCode = 1;
    return;
  }
  console.log(`\nC-engine Bash browser test passed (${result.passed}/${result.total})`);
});
