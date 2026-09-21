#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const {TextDecoder, TextEncoder} = require("node:util");

const root = path.resolve(__dirname, "..");
const stagingDir = path.join(root, "src/html-rt/src/bash");
const baselineMissingCommand = process.argv.includes("--missing-command");
const executableProbe = process.argv.includes("--exec-probe");
const genericMissingCommand = "waste-definitely-missing-command";

/* Keep the acceptance probe tied to the pathname/process imports in the Bash
 * artifact. */
const bashWat = fs.readFileSync(path.join(root, "examples/bash.wat"), "utf8");
for (const name of ["stat", "lstat", "fstat", "eaccess", "faccessat", "fork"]) {
  if (!bashWat.includes(`\"env\" \"${name}\"`)) {
    throw new Error(`Bash import inventory lost env.${name}`);
  }
}

/* Load worker source, wasm, and launch script from staging files */
const workerSrc = fs.readFileSync(path.join(stagingDir, "worker.js"), "utf8");

/* Resolve waste-wast.wasm: staging symlink or build directory */
let wasmPath = path.join(stagingDir, "waste-wast.wasm");
if (!fs.existsSync(wasmPath)) {
  wasmPath = path.join(root, "build/html-rt/waste-wast.wasm");
}
const wasmBytes = fs.readFileSync(wasmPath);
let probePath = path.join(stagingDir, "waste-probe.wasm");
if (!fs.existsSync(probePath)) probePath = path.join(root, "build/cli-rt/waste-probe.wasm");
const probeBytes = executableProbe && fs.existsSync(probePath) ? fs.readFileSync(probePath) : null;
const asArrayBuffer = bytes => bytes && bytes.buffer.slice(
  bytes.byteOffset, bytes.byteOffset + bytes.byteLength);

/* Resolve launch.wast: staging symlink or build directory */
let launchPath = path.join(stagingDir, "launch.wast");
if (!fs.existsSync(launchPath)) {
  launchPath = path.join(root, "build/html-rt/bash-runtime.wast");
}
const launchSource = fs.readFileSync(launchPath, "utf8");

let output = "";
let promptSeen = false;
let commandSent = false;
let environmentEchoSent = false;
let environmentEchoSeen = false;
let missingLsSent = false;
let exitSent = false;
let doneBeforeExit = false;
let baselineFailureSeen = false;
let commandNotFoundSeen = false;
let statusSeen = false;
let builtinSeen = false;
let genericMissingSent = false;
let genericMissingSeen = false;
let probeSent = false;
let probeSeen = false;
let probeAfterSeen = false;
let probeStatus0Seen = false;
let probeMissingSeen = false;
let probeStatus127Seen = false;
let probeSecondSeen = false;
let probeFinalStatusSeen = false;
let finish;
const completion = new Promise(resolve => { finish = resolve; });

const self = {
  postMessage(message) {
    if (message.type === "output") {
      output += message.text;
      process.stdout.write(message.text);
      if (baselineMissingCommand) {
        if (output.includes("/home/a")) environmentEchoSeen = true;
        if (output.includes("bash: ls: command not found")) {
          commandNotFoundSeen = true;
          if (!statusSeen) {
            statusSeen = true;
            setTimeout(() => self.onmessage({data: {
              type: "input",
              bytes: Array.from(new TextEncoder().encode(
                "printf '__C_ENGINE_STATUS_%s__\\n' \"$?\"\n",
              )),
            }}), 10);
          }
        }
        if (output.includes("__C_ENGINE_STATUS_127__") && !builtinSeen) {
          builtinSeen = true;
          setTimeout(() => self.onmessage({data: {
            type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "echo __C_ENGINE_AFTER__\n",
            )),
          }}), 10);
        }
        if (output.includes("__C_ENGINE_AFTER__") && !genericMissingSent) {
          genericMissingSent = true;
          setTimeout(() => self.onmessage({data: {
            type: "input",
            bytes: Array.from(new TextEncoder().encode(
              `${genericMissingCommand}\n`,
            )),
          }}), 10);
        }
        if (output.includes(`bash: ${genericMissingCommand}: command not found`) &&
            !genericMissingSeen) {
          genericMissingSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {
            type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n")),
          }}), 10);
        }
      } else if (executableProbe) {
        if (output.includes("WASTE_PROBE_OK") && !probeSeen) {
          probeSeen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "printf '__C_ENGINE_EXEC_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 10);
        }
        if (output.includes("__C_ENGINE_EXEC_STATUS_0__") && !probeStatus0Seen) {
          probeStatus0Seen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              `${genericMissingCommand}\n`,
            ))}}), 10);
        }
        if (output.includes(`bash: ${genericMissingCommand}: command not found`) &&
            !probeMissingSeen) {
          probeMissingSeen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "printf '__C_ENGINE_EXEC_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 10);
        }
        if (output.includes("__C_ENGINE_EXEC_STATUS_127__") && !probeStatus127Seen) {
          probeStatus127Seen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "/bin/waste-probe again\n",
            ))}}), 10);
        }
        if (output.includes("WASTE_PROBE_OK") && probeStatus127Seen &&
            !probeSecondSeen) {
          probeSecondSeen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "printf '__C_ENGINE_EXEC_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 10);
        }
        if (output.includes("__C_ENGINE_EXEC_STATUS_0__") &&
            probeSecondSeen && !probeFinalStatusSeen) {
          probeFinalStatusSeen = true;
          probeAfterSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
      }
      const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
      if (!promptSeen && messageHasPrompt) {
        promptSeen = true;
        setTimeout(() => {
          commandSent = true;
          self.onmessage({data: {
            type: "input",
            bytes: Array.from(new TextEncoder().encode(
              baselineMissingCommand
                ? "HOME_DIR=/home/a\n"
                : executableProbe
                  ? "/bin/waste-probe one two\n"
                  : "echo __C_ENGINE_BASH_OK__\n",
            )),
          }});
        }, 10);
      } else if (baselineMissingCommand && commandSent && messageHasPrompt &&
                 !environmentEchoSent) {
        environmentEchoSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("echo ${HOME_DIR}\n")),
        }}), 10);
      } else if (baselineMissingCommand && environmentEchoSeen &&
                 messageHasPrompt && !missingLsSent) {
        missingLsSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("ls\n")),
        }}), 10);
      }
      if (!baselineMissingCommand && commandSent && !exitSent &&
          output.includes("__C_ENGINE_BASH_OK__")) {
        exitSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("exit\n")),
        }}), 10);
      }
    } else if (message.type === "done") {
      if (!exitSent) doneBeforeExit = true;
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
  wasmBytes: asArrayBuffer(wasmBytes),
  probeBytes: asArrayBuffer(probeBytes),
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
  const passed = baselineMissingCommand
    ? promptSeen && commandSent && environmentEchoSent && environmentEchoSeen &&
      missingLsSent && commandNotFoundSeen && statusSeen &&
      builtinSeen && genericMissingSeen && exitSent && !doneBeforeExit &&
      output.includes("__C_ENGINE_STATUS_127__") &&
      output.includes("__C_ENGINE_AFTER__") && result.ok
    : executableProbe
      ? promptSeen && commandSent && probeSeen && probeStatus0Seen &&
        probeMissingSeen && probeStatus127Seen && probeSecondSeen &&
        probeFinalStatusSeen && probeAfterSeen && exitSent && !doneBeforeExit &&
        result.ok
      : promptSeen && commandSent && exitSent && !doneBeforeExit &&
        output.includes("__C_ENGINE_BASH_OK__") && result.ok;
  if (!passed) {
    console.error("\nC-engine Bash browser test failed:", result);
    process.exitCode = 1;
    return;
  }
  if (baselineMissingCommand) {
    console.log("\nC-engine Bash command-not-found test passed");
  } else {
    console.log(`\nC-engine Bash browser test passed (${result.passed}/${result.total})`);
  }
});
