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
const executableExitProbe = process.argv.includes("--exec-exit");
const coreutilsTrueProbe = process.argv.includes("--coreutils-true");
const coreutilsFalseProbe = process.argv.includes("--coreutils-false");
const watProbe = process.argv.includes("--wat-probe");
const watShebangProbe = process.argv.includes("--wat-shebang-probe");
const watFailureProbe = process.argv.includes("--wat-failure-probe");
const watDirectProbe = process.argv.includes("--wat-direct-probe");
const wastProbe = process.argv.includes("--wast-probe");
const wastShebangProbe = process.argv.includes("--wast-shebang-probe");
const wastFailureProbe = process.argv.includes("--wast-failure-probe");
const wastDirectProbe = process.argv.includes("--wast-direct-probe");
const wastRepeatProbe = process.argv.includes("--wast-repeat-probe");
const runWatProbe = watProbe || watShebangProbe || watFailureProbe || watDirectProbe;
const runTextProbe = runWatProbe || wastProbe || wastShebangProbe ||
  wastFailureProbe || wastDirectProbe || wastRepeatProbe;
const watExpectedStatus = watFailureProbe || wastFailureProbe ? 126 : 0;
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
const builtProbePath = path.join(root, "build/cli-rt/waste-probe.wasm");
if (fs.existsSync(builtProbePath)) probePath = builtProbePath;
const probeBytes = (executableProbe || executableExitProbe) && fs.existsSync(probePath)
  ? fs.readFileSync(probePath) : null;
const coreutilsTruePath = path.join(stagingDir, "true.wasm");
const coreutilsFalsePath = path.join(stagingDir, "false.wasm");
const coreutilsTrueBytes = coreutilsTrueProbe && fs.existsSync(coreutilsTruePath)
  ? fs.readFileSync(coreutilsTruePath) : null;
const coreutilsFalseBytes = coreutilsFalseProbe && fs.existsSync(coreutilsFalsePath)
  ? fs.readFileSync(coreutilsFalsePath) : null;
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
let probeFdsSeen = false;
let probeAfterSeen = false;
let probeStatus0Seen = false;
let probeMissingSeen = false;
let probeStatus127Seen = false;
let probeSecondSeen = false;
let probeFinalStatusSeen = false;
let probeStatus7Seen = false;
let coreutilsTrueStatusRequested = false;
let coreutilsTrueStatusSeen = false;
let coreutilsFalseStatusRequested = false;
let coreutilsFalseStatusSeen = false;
let exitProbeStatusRequested = false;
let watStatusRequested = false;
let watAfterSeen = false;
let wastRepeatStage = 0;
let vfsSeen = false;
let finish;
const completion = new Promise(resolve => { finish = resolve; });

const self = {
  postMessage(message) {
    if (message.type === "vfs") {
      if (message.paths?.includes("/tmp") &&
          message.paths?.includes("/usr/bin") &&
          message.paths?.includes("/usr/share/waste/launch.wast") &&
          ((!coreutilsTrueProbe && !coreutilsFalseProbe) ||
            (coreutilsTrueProbe && message.paths?.includes("/usr/bin/true") &&
             message.paths?.includes("/bin/true")) ||
            (coreutilsFalseProbe && message.paths?.includes("/usr/bin/false") &&
             message.paths?.includes("/bin/false")))) vfsSeen = true;
    } else if (message.type === "output") {
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
      } else if (executableExitProbe) {
        /* The probe exits before it can print its success marker.  Ask Bash
         * for `$?` at the next prompt, then prove that the shell remains
         * usable after observing the nonzero status. */
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        if (commandSent && messageHasPrompt && !exitProbeStatusRequested) {
          exitProbeStatusRequested = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "printf '__C_ENGINE_EXEC_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 10);
        }
        if (output.includes("__C_ENGINE_EXEC_STATUS_7__") && !probeStatus7Seen) {
          probeStatus7Seen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "echo __C_ENGINE_EXEC_EXIT_AFTER__\n",
            ))}}), 10);
        }
        if (output.includes("__C_ENGINE_EXEC_EXIT_AFTER__") && !exitSent) {
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
      } else if (executableProbe) {
        if (output.includes("WASTE_PROBE_FDS_OK") && !probeFdsSeen)
          probeFdsSeen = true;
        if (output.includes("WASTE_PROBE_OK") && !probeSeen) {
          probeSeen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "printf '__C_ENGINE_EXEC_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 100);
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
      } else if (coreutilsTrueProbe || coreutilsFalseProbe) {
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        const statusRequested = coreutilsTrueProbe
          ? coreutilsTrueStatusRequested : coreutilsFalseStatusRequested;
        if (commandSent && messageHasPrompt && !statusRequested) {
          if (coreutilsTrueProbe) coreutilsTrueStatusRequested = true;
          else coreutilsFalseStatusRequested = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              coreutilsTrueProbe
                ? "printf '__C_ENGINE_COREUTILS_TRUE_STATUS_%s__\\n' \"$?\"\n"
                : "printf '__C_ENGINE_COREUTILS_FALSE_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 10);
        }
        if (coreutilsTrueProbe && output.includes("__C_ENGINE_COREUTILS_TRUE_STATUS_") &&
            !coreutilsTrueStatusSeen) {
          coreutilsTrueStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsFalseProbe && output.includes("__C_ENGINE_COREUTILS_FALSE_STATUS_") &&
            !coreutilsFalseStatusSeen) {
          coreutilsFalseStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
      } else if (wastRepeatProbe) {
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        if (commandSent && messageHasPrompt) {
          const commands = [
            "printf '__C_ENGINE_WAT_REPEAT_1_%s__\\n' \"$?\"\n",
            "/bin/wat /tmp/checks.wast\n",
            "printf '__C_ENGINE_WAT_REPEAT_2_%s__\\n' \"$?\"\n",
            "/bin/wat /tmp/checks.wast\n",
            "printf '__C_ENGINE_WAT_REPEAT_3_%s__\\n' \"$?\"\n",
            "/bin/wast /tmp/checks.wast\n",
            "printf '__C_ENGINE_WAST_REPEAT_1_%s__\\n' \"$?\"\n",
            "/tmp/checks-direct.wast\n",
            "printf '__C_ENGINE_WAST_REPEAT_2_%s__\\n' \"$?\"\n",
            "/tmp/wast-shebang.wast\n",
            "printf '__C_ENGINE_WAST_REPEAT_3_%s__\\n' \"$?\"\n",
            "echo __C_ENGINE_WAST_REPEAT_AFTER__\n",
            "exit\n",
          ];
          if (wastRepeatStage >= 1 && wastRepeatStage <= commands.length) {
            const command = commands[wastRepeatStage - 1];
            if (wastRepeatStage === commands.length) exitSent = true;
            wastRepeatStage++;
            setTimeout(() => self.onmessage({data: {type: "input",
              bytes: Array.from(new TextEncoder().encode(command))}}), 10);
          }
        }
      } else if (runTextProbe) {
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        if (commandSent && messageHasPrompt && !watStatusRequested) {
          watStatusRequested = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              `printf '__C_ENGINE_WAT_STATUS_%s__\\n' "$?"\n`,
            ))}}), 10);
        }
        if (output.includes(`__C_ENGINE_WAT_STATUS_${watExpectedStatus}__`) &&
            !watAfterSeen) {
          watAfterSeen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "echo __C_ENGINE_WAT_AFTER__\n",
            ))}}), 10);
        }
      }
      const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
      if (!promptSeen && messageHasPrompt) {
        promptSeen = true;
        setTimeout(() => {
          commandSent = true;
          if (wastRepeatProbe) wastRepeatStage = 1;
          self.onmessage({data: {
            type: "input",
            bytes: Array.from(new TextEncoder().encode(
              baselineMissingCommand
                ? "HOME_DIR=/home/a\n"
                : runTextProbe
                  ? (wastRepeatProbe ? "/bin/wat /tmp/checks.wast\n" :
                     watShebangProbe ? "/tmp/wat-shebang.wat\n" :
                     watFailureProbe ? "wat /tmp/wat-bad.wat\n" :
                     watDirectProbe ? "/tmp/wat-direct.wat\n" :
                     (wastProbe || wastShebangProbe || wastFailureProbe ||
                      wastDirectProbe || wastRepeatProbe) ?
                       (wastShebangProbe ? "/tmp/wast-shebang.wast\n" :
                         wastFailureProbe ? "/bin/wast /tmp/bad.wast\n" :
                         wastDirectProbe ? "/tmp/checks-direct.wast\n" :
                                           "/bin/wast /tmp/checks.wast\n") :
                     "wat /tmp/wat-probe.wat\n")
                : executableExitProbe
                  ? "WASTE_PROBE_EXIT=7 /bin/waste-probe\n"
                : executableProbe
                  ? "/bin/waste-probe one two\n"
                : coreutilsTrueProbe
                  ? "/bin/true\n"
                : coreutilsFalseProbe
                  ? "/bin/false\n"
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
          (runTextProbe ? output.includes("__C_ENGINE_WAT_AFTER__") :
                      output.includes("__C_ENGINE_BASH_OK__"))) {
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
  vfsFiles: [{
    path: "/usr/share/waste/launch.wast",
    bytes: asArrayBuffer(Buffer.from(launchSource, "utf8")),
    mode: 0o644,
  }, {
    path: "/bin/waste-probe",
    bytes: asArrayBuffer(probeBytes),
    mode: 0o755,
  }, ...(coreutilsTrueBytes ? [{
    path: "/usr/bin/true",
    bytes: asArrayBuffer(coreutilsTrueBytes),
    mode: 0o755,
  }, {
    path: "/bin/true",
    bytes: asArrayBuffer(coreutilsTrueBytes),
    mode: 0o755,
  }] : []), ...(coreutilsFalseBytes ? [{
    path: "/usr/bin/false",
    bytes: asArrayBuffer(coreutilsFalseBytes),
    mode: 0o755,
  }, {
    path: "/bin/false",
    bytes: asArrayBuffer(coreutilsFalseBytes),
    mode: 0o755,
  }] : []), {
    path: watShebangProbe ? "/tmp/wat-shebang.wat" :
      watFailureProbe ? "/tmp/wat-bad.wat" :
      watDirectProbe ? "/tmp/wat-direct.wat" :
      wastFailureProbe ? "/tmp/bad.wast" :
      wastShebangProbe ? "/tmp/wast-shebang.wast" :
      wastDirectProbe ? "/tmp/checks-direct.wast" :
      (wastProbe || wastRepeatProbe) ? "/tmp/checks.wast" : "/tmp/wat-probe.wat",
    bytes: asArrayBuffer(Buffer.from(
      (watShebangProbe
        ? '#!/bin/wat --probe\n(module (memory 1) (func (export "_start")))'
        : watFailureProbe
          ? '(module'
        : wastShebangProbe
          ? '#!/bin/wast\n(module)\n'
        : wastFailureProbe
          ? '(module'
        : (wastProbe || wastRepeatProbe)
          ? '(module)\n'
          : '(module (memory 1) (func (export "_start")))'), "utf8")),
    mode: 0o755,
  }, ...(wastRepeatProbe ? [{
    path: "/tmp/checks-direct.wast",
    bytes: asArrayBuffer(Buffer.from('(module)\n', "utf8")),
    mode: 0o755,
  }, {
    path: "/tmp/wast-shebang.wast",
    bytes: asArrayBuffer(Buffer.from('#!/bin/wast\n(module)\n', "utf8")),
    mode: 0o755,
  }] : [])],
  source: launchSource,
}});
/* The page sends its initial dimensions immediately after start.  Keep this
 * race in the fixture so the worker must queue resize until the engine-owned
 * terminal exists. */
self.onmessage({data: {type: "resize", columns: 80, rows: 24}});

let timeoutId;
const timeout = new Promise(resolve => {
  timeoutId = setTimeout(
    () => resolve({error: "timed out waiting for Bash"}), 30000,
  );
});

Promise.race([completion, timeout]).then(result => {
  clearTimeout(timeoutId);
  const passed = baselineMissingCommand
    ? promptSeen && commandSent && vfsSeen && environmentEchoSent && environmentEchoSeen &&
      missingLsSent && commandNotFoundSeen && statusSeen &&
      builtinSeen && genericMissingSeen && exitSent && !doneBeforeExit &&
      output.includes("__C_ENGINE_STATUS_127__") &&
      output.includes("__C_ENGINE_AFTER__") && result.ok
    : executableExitProbe
      ? promptSeen && commandSent && vfsSeen && exitProbeStatusRequested &&
        probeStatus7Seen && output.includes("__C_ENGINE_EXEC_EXIT_AFTER__") &&
        exitSent && !doneBeforeExit && result.ok
    : executableProbe
      ? promptSeen && commandSent && vfsSeen && probeFdsSeen && probeSeen && probeStatus0Seen &&
        probeMissingSeen && probeStatus127Seen && probeSecondSeen &&
        probeFinalStatusSeen && probeAfterSeen && exitSent && !doneBeforeExit &&
        result.ok
    : coreutilsTrueProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsTrueStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_TRUE_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsFalseProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsFalseStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_FALSE_STATUS_1__") &&
        exitSent && !doneBeforeExit && result.ok
    : wastRepeatProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        output.includes("__C_ENGINE_WAST_REPEAT_1_0__") &&
        output.includes("__C_ENGINE_WAST_REPEAT_2_0__") &&
        output.includes("__C_ENGINE_WAST_REPEAT_3_0__") &&
        output.includes("__C_ENGINE_WAST_REPEAT_AFTER__") && result.ok
    : runTextProbe
      ? promptSeen && commandSent && vfsSeen && watStatusRequested &&
        output.includes(`__C_ENGINE_WAT_STATUS_${watExpectedStatus}__`) &&
        output.includes("__C_ENGINE_WAT_AFTER__") && watAfterSeen &&
        exitSent && !doneBeforeExit && result.ok
      : promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        output.includes("__C_ENGINE_BASH_OK__") && result.ok;
  if (!passed) {
    console.error("\nC-engine Bash browser test failed:", result);
    console.error("Captured output:\n" + output);
    process.exitCode = 1;
    return;
  }
  if (baselineMissingCommand) {
    console.log("\nC-engine Bash command-not-found test passed");
  } else {
    console.log(`\nC-engine Bash browser test passed (${result.passed}/${result.total})`);
  }
});
