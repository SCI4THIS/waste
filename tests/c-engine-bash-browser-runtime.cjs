#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const vm = require("node:vm");
const {TextDecoder, TextEncoder} = require("node:util");

const htmlPath = process.argv[2] || "build/c-engine/bash.html";
const html = fs.readFileSync(htmlPath, "utf8");
const payloadMatch = html.match(/^    const PAYLOAD = (.*);$/m);
const workerMatch = html.match(
  /    const workerProgram = String\.raw`([\s\S]*?)\n    `;/,
);
if (!payloadMatch || !workerMatch) {
  throw new Error(`could not extract C-engine Bash worker from ${htmlPath}`);
}

const payload = JSON.parse(payloadMatch[1]);
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
vm.runInContext(workerMatch[1], context, {filename: "c-engine-bash-worker.js"});
self.onmessage({data: {
  type: "start",
  wasmB64: payload.wasmB64,
  source: payload.launch,
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
