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
const coreutilsPwdProbe = process.argv.includes("--coreutils-pwd");
const coreutilsEchoProbe = process.argv.includes("--coreutils-echo");
const coreutilsBasenameProbe = process.argv.includes("--coreutils-basename");
const coreutilsPrintfProbe = process.argv.includes("--coreutils-printf");
const coreutilsDirnameProbe = process.argv.includes("--coreutils-dirname");
const coreutilsCatProbe = process.argv.includes("--coreutils-cat");
const coreutilsWcProbe = process.argv.includes("--coreutils-wc");
const coreutilsLsProbe = process.argv.includes("--coreutils-ls");
const coreutilsMatrixProbe = process.argv.includes("--coreutils-matrix");
const fullPackageProbe = process.argv.includes("--full-package");
const readlineEchoProbe = process.argv.includes("--readline-echo");
const readlineCompletionProbe = process.argv.includes("--readline-completion");
const heredocProbe = process.argv.includes("--heredoc");
const heredocBuiltinProbe = process.argv.includes("--heredoc-builtin");
const heredocStdoutProbe = process.argv.includes("--heredoc-stdout");
const pipelineProbe = process.argv.includes("--pipeline-probe");
const coreutilsLsCommand = process.env.WASTE_COREUTILS_LS_COMMAND || "";
const coreutilsLsExpected = process.env.WASTE_COREUTILS_LS_EXPECT || "";
const coreutilsLsExpectedCount = Number(
  process.env.WASTE_COREUTILS_LS_EXPECT_COUNT || "1");
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
const matrixCommands = [
  {tag: "TRUE", command: "/bin/true", status: 0},
  {tag: "FALSE", command: "/bin/false", status: 1},
  {tag: "PWD", command: "/bin/pwd", status: 0},
  {tag: "ECHO", command: "/bin/echo MATRIX_ECHO_OUTPUT", status: 0},
  {tag: "PRINTF", command: "/bin/printf 'MATRIX_PRINTF_OUTPUT\\n'", status: 0},
  {tag: "BASENAME", command: "/bin/basename /alpha/MATRIX_BASENAME_OUTPUT", status: 0},
  {tag: "DIRNAME", command: "/bin/dirname /alpha/beta/MATRIX_FILE", status: 0},
  {tag: "CAT", command: "/bin/cat /tmp/matrix-cat.txt", status: 0},
  {tag: "WC", command: "/bin/wc -l -w -c /tmp/matrix-wc.txt", status: 0},
  {tag: "LS", command: "/bin/ls -1 /bin", status: 0},
  {tag: "DATE", command: "/bin/date -u +%Y", status: 0},
  {tag: "WAT", command: "/bin/wat /tmp/matrix.wat", status: 0},
  {tag: "WAST", command: "/bin/wast /tmp/matrix.wast", status: 0},
];
const matrixSteps = matrixCommands.flatMap(({tag, command}) => [
  command + "\n",
  `printf '__C_ENGINE_MATRIX_${tag}_STATUS_%s__\\n' "$?"\n`,
]).concat(["echo __C_ENGINE_MATRIX_AFTER__\n", "exit\n"]);

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
const wasmMtimeMs = fs.statSync(wasmPath).mtimeMs;
const buildMtime = {
  sec: Math.floor(wasmMtimeMs / 1000),
  nsec: Math.floor(wasmMtimeMs % 1000) * 1000000,
};
let probePath = path.join(stagingDir, "waste-probe.wasm");
const builtProbePath = path.join(root, "build/cli-rt/waste-probe.wasm");
if (fs.existsSync(builtProbePath)) probePath = builtProbePath;
const probeBytes = (executableProbe || executableExitProbe) && fs.existsSync(probePath)
  ? fs.readFileSync(probePath) : null;
const coreutilsTruePath = path.join(stagingDir, "true.wasm");
const coreutilsFalsePath = path.join(stagingDir, "false.wasm");
const coreutilsPwdPath = path.join(stagingDir, "pwd.wasm");
const coreutilsEchoPath = path.join(stagingDir, "echo.wasm");
const coreutilsBasenamePath = path.join(stagingDir, "basename.wasm");
const coreutilsPrintfPath = path.join(stagingDir, "printf.wasm");
const coreutilsDirnamePath = path.join(stagingDir, "dirname.wasm");
const coreutilsCatPath = path.join(stagingDir, "cat.wasm");
const coreutilsWcPath = path.join(stagingDir, "wc.wasm");
const coreutilsLsPath = path.join(stagingDir, "ls.wasm");
const coreutilsTrueBytes = coreutilsTrueProbe && fs.existsSync(coreutilsTruePath)
  ? fs.readFileSync(coreutilsTruePath) : null;
const coreutilsFalseBytes = coreutilsFalseProbe && fs.existsSync(coreutilsFalsePath)
  ? fs.readFileSync(coreutilsFalsePath) : null;
const coreutilsPwdBytes = coreutilsPwdProbe && fs.existsSync(coreutilsPwdPath)
  ? fs.readFileSync(coreutilsPwdPath) : null;
const coreutilsEchoBytes = coreutilsEchoProbe && fs.existsSync(coreutilsEchoPath)
  ? fs.readFileSync(coreutilsEchoPath) : null;
const coreutilsBasenameBytes = coreutilsBasenameProbe && fs.existsSync(coreutilsBasenamePath)
  ? fs.readFileSync(coreutilsBasenamePath) : null;
const coreutilsPrintfBytes = coreutilsPrintfProbe && fs.existsSync(coreutilsPrintfPath)
  ? fs.readFileSync(coreutilsPrintfPath) : null;
const coreutilsDirnameBytes = coreutilsDirnameProbe && fs.existsSync(coreutilsDirnamePath)
  ? fs.readFileSync(coreutilsDirnamePath) : null;
const coreutilsCatBytes = (coreutilsCatProbe || coreutilsLsProbe || pipelineProbe) &&
  fs.existsSync(coreutilsCatPath)
  ? fs.readFileSync(coreutilsCatPath) : null;
const coreutilsWcBytes = (coreutilsWcProbe || pipelineProbe) && fs.existsSync(coreutilsWcPath)
  ? fs.readFileSync(coreutilsWcPath) : null;
const coreutilsLsBytes = (coreutilsLsProbe || pipelineProbe) && fs.existsSync(coreutilsLsPath)
  ? fs.readFileSync(coreutilsLsPath) : null;
const asArrayBuffer = bytes => bytes && bytes.buffer.slice(
  bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
const fullPackageFiles = fullPackageProbe
  ? ["true", "false", "pwd", "echo", "printf", "basename", "dirname", "cat", "wc", "ls", "date"]
      .flatMap(name => {
        const sourcePath = path.join(stagingDir, `${name}.wasm`);
        const bytes = fs.readFileSync(sourcePath);
        const mtimeMs = fs.statSync(sourcePath).mtimeMs;
        return ["/usr/bin/", "/bin/"].map(prefix => ({
          path: prefix + name,
          bytes: asArrayBuffer(bytes),
          mode: 0o755,
          mtimeSec: Math.floor(mtimeMs / 1000),
          mtimeNsec: Math.floor(mtimeMs % 1000) * 1000000,
        }));
      })
      .concat([
        "/usr/share/waste/coreutils-provenance.json",
        "/usr/share/waste/coreutils-source-package.json",
        "/usr/share/waste/waste-interpreters.json",
        "/usr/share/licenses/coreutils/COPYING",
      ].map(filePath => ({
        path: filePath,
        bytes: asArrayBuffer(Buffer.from("full-package regression fixture\n", "utf8")),
        mode: 0o644,
      })))
  : [];

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
let coreutilsPwdStatusRequested = false;
let coreutilsPwdStatusSeen = false;
let coreutilsPwdRootSeen = false;
let coreutilsEchoStatusRequested = false;
let coreutilsEchoStatusSeen = false;
let coreutilsEchoOutputSeen = false;
let coreutilsBasenameStatusRequested = false;
let coreutilsBasenameStatusSeen = false;
let coreutilsBasenameOutputSeen = false;
let coreutilsPrintfStatusRequested = false;
let coreutilsPrintfStatusSeen = false;
let coreutilsPrintfOutputSeen = false;
let coreutilsDirnameStatusRequested = false;
let coreutilsDirnameStatusSeen = false;
let coreutilsDirnameOutputSeen = false;
let coreutilsCatStatusRequested = false;
let coreutilsCatStatusSeen = false;
let coreutilsCatOutputSeen = false;
let coreutilsWcStatusRequested = false;
let coreutilsWcStatusSeen = false;
let coreutilsWcOutputSeen = false;
let coreutilsLsStatusRequested = false;
let coreutilsLsStatusSeen = false;
let coreutilsLsOutputSeen = false;
let readlineEchoVisibleBeforeEnter = false;
let readlineEnterSent = false;
let readlineResultSeen = false;
let readlineTabSent = false;
let readlineCompletionSeen = false;
let readlineCompletionEnterSent = false;
let readlineCompletionResultSeen = false;
let exitProbeStatusRequested = false;
let watStatusRequested = false;
let watAfterSeen = false;
let wastRepeatStage = 0;
let matrixStep = 0;
let pipelineStep = 0;
let pipelineCountSeen = false;
let pipelineRedirectDone = false;
let pipelineCatSeen = false;
let pipelineAfterSeen = false;
let vfsSeen = false;
let finish;
const completion = new Promise(resolve => { finish = resolve; });

const self = {
  postMessage(message) {
    if (message.type === "vfs") {
      if (message.paths?.includes("/tmp") &&
          message.paths?.includes("/root") &&
          message.paths?.includes("/usr/bin") &&
          message.paths?.includes("/usr/share/waste/launch.wast") &&
          ((!coreutilsTrueProbe && !coreutilsFalseProbe && !coreutilsPwdProbe &&
            !coreutilsEchoProbe && !coreutilsBasenameProbe &&
            !coreutilsPrintfProbe && !coreutilsDirnameProbe && !coreutilsCatProbe &&
            !coreutilsWcProbe && !coreutilsLsProbe && !coreutilsMatrixProbe) ||
            (coreutilsTrueProbe && message.paths?.includes("/usr/bin/true") &&
             message.paths?.includes("/bin/true")) ||
            (coreutilsFalseProbe && message.paths?.includes("/usr/bin/false") &&
             message.paths?.includes("/bin/false")) ||
            (coreutilsPwdProbe && message.paths?.includes("/usr/bin/pwd") &&
             message.paths?.includes("/bin/pwd")) ||
            (coreutilsEchoProbe && message.paths?.includes("/usr/bin/echo") &&
             message.paths?.includes("/bin/echo")) ||
            (coreutilsBasenameProbe && message.paths?.includes("/usr/bin/basename") &&
             message.paths?.includes("/bin/basename")) ||
            (coreutilsPrintfProbe && message.paths?.includes("/usr/bin/printf") &&
             message.paths?.includes("/bin/printf")) ||
            (coreutilsDirnameProbe && message.paths?.includes("/usr/bin/dirname") &&
             message.paths?.includes("/bin/dirname")) ||
            (coreutilsCatProbe && message.paths?.includes("/usr/bin/cat") &&
             message.paths?.includes("/bin/cat") &&
             message.paths?.includes("/usr/share/waste/cat-fixture.txt")) ||
            (coreutilsWcProbe && message.paths?.includes("/usr/bin/wc") &&
             message.paths?.includes("/bin/wc") &&
             message.paths?.includes("/usr/share/waste/wc-fixture.txt")) ||
            (coreutilsLsProbe && message.paths?.includes("/usr/bin/ls") &&
             message.paths?.includes("/bin/ls")) ||
            (coreutilsMatrixProbe &&
             ["true", "false", "pwd", "echo", "printf", "basename",
              "dirname", "cat", "wc", "ls", "date"].every(name =>
                message.paths?.includes(`/bin/${name}`)) &&
             message.paths?.includes("/bin/wat") &&
             message.paths?.includes("/bin/wast") &&
             message.paths?.includes("/tmp/matrix.wat") &&
             message.paths?.includes("/tmp/matrix.wast")) ||
            (pipelineProbe &&
             ["ls", "wc", "cat"].every(name =>
                message.paths?.includes(`/bin/${name}`))))) vfsSeen = true;
    } else if (message.type === "output") {
      output += message.text;
      process.stdout.write(message.text);
      if (readlineEchoProbe && !readlineEnterSent &&
          output.includes("echo __C_ENGINE_READLINE_RESULT__")) {
        readlineEchoVisibleBeforeEnter = true;
        readlineEnterSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: [10],
        }}), 10);
      }
      if (readlineEchoProbe && readlineEnterSent &&
          output.includes("\r\n__C_ENGINE_READLINE_RESULT__\r\n") &&
          !readlineResultSeen) {
        readlineResultSeen = true;
        exitSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("exit\n")),
        }}), 10);
      }
      if (readlineCompletionProbe && !readlineTabSent &&
          output.includes("/bin/pw")) {
        readlineTabSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: [9],
        }}), 10);
      }
      if (readlineCompletionProbe && readlineTabSent &&
          !readlineCompletionEnterSent && output.includes("/bin/pwd")) {
        readlineCompletionSeen = true;
        readlineCompletionEnterSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: [10],
        }}), 10);
      }
      if (readlineCompletionProbe && readlineCompletionEnterSent &&
          output.includes("/root") && !readlineCompletionResultSeen) {
        readlineCompletionResultSeen = true;
        exitSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("exit\n")),
        }}), 10);
      }
      if (coreutilsPwdProbe && output.includes("/root")) coreutilsPwdRootSeen = true;
      if (coreutilsEchoProbe && output.includes("hello world")) coreutilsEchoOutputSeen = true;
      if (coreutilsBasenameProbe && output.includes("file.txt")) coreutilsBasenameOutputSeen = true;
      if (coreutilsPrintfProbe && output.includes("42")) coreutilsPrintfOutputSeen = true;
      if (coreutilsDirnameProbe && output.includes("/usr/local")) coreutilsDirnameOutputSeen = true;
      if (coreutilsCatProbe && output.includes("WASTE_CAT_FIXTURE_7f3a")) coreutilsCatOutputSeen = true;
      if (coreutilsWcProbe &&
          /\b2\s+3\s+14\s+\/usr\/share\/waste\/wc-fixture\.txt\b/.test(output)) {
        coreutilsWcOutputSeen = true;
      }
      if (coreutilsLsProbe && coreutilsLsCommand && coreutilsLsExpected &&
          output.split(coreutilsLsExpected).length - 1 >= coreutilsLsExpectedCount) {
        coreutilsLsOutputSeen = true;
      } else if (coreutilsLsProbe &&
          /__LS_ROOT_BEGIN__\r?\n(?:bin\r?\nroot\r?\ntmp\r?\nusr|bin\s+root\s+tmp\s+usr)/.test(output) &&
          /__LS_BIN_BEGIN__[\s\S]*\r?\nls\r?\n[\s\S]*\r?\nwast\r?\nwaste-probe\r?\nwat/.test(output) &&
          /__LS_EMPTY_BEGIN__\r?\n__LS_EMPTY_END__/.test(output) &&
          /__LS_DOT_BEGIN__[\s\S]*[ \t]\.\r?\n[\s\S]*[ \t]\.\.\r?\n[\s\S]*__LS_DOT_END__/.test(output) &&
          /__LS_HIDDEN_BEGIN__\r?\n\.hidden\r?\nlink\r?\nvisible/.test(output) &&
          /__LS_LONG_BEGIN__[\s\S]*lrwxrwxrwx[^\r\n]*link -> \/tmp\/ls-fixture\/visible/.test(output) &&
          /__LS_TTY_BEGIN__\r?\nlink\s+visible\r?\n__LS_TTY_END__/.test(output) &&
          /__LS_REDIRECT_BEGIN__\r?\nlink\r?\nvisible\r?\n__LS_REDIRECT_END__/.test(output) &&
          output.includes("__LS_MULTI_BEGIN__") &&
          output.includes("/tmp/ls-empty:") &&
          output.includes("/tmp/ls-fixture:") &&
          output.includes("__LS_MISSING_STATUS_2__") &&
          output.includes("__LS_SECOND_COMMAND__")) {
        coreutilsLsOutputSeen = true;
      }
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
      } else if (coreutilsMatrixProbe) {
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        if (commandSent && messageHasPrompt && matrixStep < matrixSteps.length) {
          const command = matrixSteps[matrixStep++];
          if (command === "exit\n") exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(command))}}), 10);
        }
      } else if (pipelineProbe) {
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        const pipelineCommands = [
          "echo __PIPELINE_PIPE_BEGIN__; /bin/ls /bin | /bin/wc -l; echo __PIPELINE_PIPE_END__\n",
          "/bin/ls -1 /bin > /tmp/pipeline-list.txt; printf '__PIPELINE_REDIRECT_STATUS_%s__\\n' \"$?\"\n",
          "echo __PIPELINE_CAT_BEGIN__; /bin/cat /tmp/pipeline-list.txt; echo __PIPELINE_CAT_END__\n",
          "echo __PIPELINE_AFTER__\n",
          "exit\n",
        ];
        if (output.includes("__PIPELINE_PIPE_BEGIN__")) {
          const parts = output.split("__PIPELINE_PIPE_BEGIN__");
          const afterLast = parts[parts.length - 1];
          if (/\d+/.test(afterLast))
            pipelineCountSeen = true;
        }
        if (output.includes("__PIPELINE_REDIRECT_STATUS_0__"))
          pipelineRedirectDone = true;
        if (output.includes("__PIPELINE_CAT_BEGIN__") && output.includes("__PIPELINE_CAT_END__"))
          pipelineCatSeen = true;
        if (output.includes("__PIPELINE_AFTER__"))
          pipelineAfterSeen = true;
        if (commandSent && messageHasPrompt && pipelineStep < pipelineCommands.length) {
          const command = pipelineCommands[pipelineStep++];
          if (command === "exit\n") exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(command))}}), 10);
        }
      } else if (coreutilsTrueProbe || coreutilsFalseProbe || coreutilsPwdProbe ||
                 coreutilsEchoProbe || coreutilsBasenameProbe ||
                 coreutilsPrintfProbe || coreutilsDirnameProbe || coreutilsCatProbe ||
                 coreutilsWcProbe || coreutilsLsProbe) {
        const messageHasPrompt = /bash-[^\r\n]*[#$] ?/.test(message.text);
        const statusRequested = coreutilsTrueProbe ? coreutilsTrueStatusRequested :
          coreutilsFalseProbe ? coreutilsFalseStatusRequested :
          coreutilsPwdProbe ? coreutilsPwdStatusRequested :
          coreutilsEchoProbe ? coreutilsEchoStatusRequested :
          coreutilsBasenameProbe ? coreutilsBasenameStatusRequested :
          coreutilsPrintfProbe ? coreutilsPrintfStatusRequested :
          coreutilsDirnameProbe ? coreutilsDirnameStatusRequested :
          coreutilsCatProbe ? coreutilsCatStatusRequested :
          coreutilsWcProbe ? coreutilsWcStatusRequested :
          coreutilsLsStatusRequested;
        if (commandSent && messageHasPrompt && !statusRequested) {
          if (coreutilsTrueProbe) coreutilsTrueStatusRequested = true;
          else if (coreutilsFalseProbe) coreutilsFalseStatusRequested = true;
          else if (coreutilsPwdProbe) coreutilsPwdStatusRequested = true;
          else if (coreutilsEchoProbe) coreutilsEchoStatusRequested = true;
          else if (coreutilsBasenameProbe) coreutilsBasenameStatusRequested = true;
          else if (coreutilsPrintfProbe) coreutilsPrintfStatusRequested = true;
          else if (coreutilsDirnameProbe) coreutilsDirnameStatusRequested = true;
          else if (coreutilsCatProbe) coreutilsCatStatusRequested = true;
          else if (coreutilsWcProbe) coreutilsWcStatusRequested = true;
          else coreutilsLsStatusRequested = true;
          const tag = coreutilsTrueProbe ? "TRUE" :
            coreutilsFalseProbe ? "FALSE" :
            coreutilsPwdProbe ? "PWD" :
            coreutilsEchoProbe ? "ECHO" :
            coreutilsBasenameProbe ? "BASENAME" :
            coreutilsPrintfProbe ? "PRINTF" :
            coreutilsDirnameProbe ? "DIRNAME" :
            coreutilsCatProbe ? "CAT" :
            coreutilsWcProbe ? "WC" : "LS";
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              `printf '__C_ENGINE_COREUTILS_${tag}_STATUS_%s__\\n' \"$?\"\n`,
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
        if (coreutilsPwdProbe && output.includes("__C_ENGINE_COREUTILS_PWD_STATUS_") &&
            !coreutilsPwdStatusSeen) {
          coreutilsPwdStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsEchoProbe && output.includes("__C_ENGINE_COREUTILS_ECHO_STATUS_") &&
            !coreutilsEchoStatusSeen) {
          coreutilsEchoStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsBasenameProbe && output.includes("__C_ENGINE_COREUTILS_BASENAME_STATUS_") &&
            !coreutilsBasenameStatusSeen) {
          coreutilsBasenameStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsPrintfProbe && output.includes("__C_ENGINE_COREUTILS_PRINTF_STATUS_") &&
            !coreutilsPrintfStatusSeen) {
          coreutilsPrintfStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsDirnameProbe && output.includes("__C_ENGINE_COREUTILS_DIRNAME_STATUS_") &&
            !coreutilsDirnameStatusSeen) {
          coreutilsDirnameStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsCatProbe && output.includes("__C_ENGINE_COREUTILS_CAT_STATUS_") &&
            !coreutilsCatStatusSeen) {
          coreutilsCatStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsWcProbe && output.includes("__C_ENGINE_COREUTILS_WC_STATUS_") &&
            !coreutilsWcStatusSeen) {
          coreutilsWcStatusSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (coreutilsLsProbe && output.includes("__C_ENGINE_COREUTILS_LS_STATUS_") &&
            !coreutilsLsStatusSeen) {
          coreutilsLsStatusSeen = true;
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
          if (coreutilsMatrixProbe) matrixStep = 1;
          if (pipelineProbe) pipelineStep = 1;
          if (heredocProbe || heredocBuiltinProbe || heredocStdoutProbe)
            exitSent = true;
          const initialBytes = new TextEncoder().encode(
              baselineMissingCommand
                ? "HOME_DIR=/home/a\n"
                : pipelineProbe
                  ? "echo __PIPELINE_PIPE_BEGIN__; /bin/ls /bin | /bin/wc -l; echo __PIPELINE_PIPE_END__\n"
                : coreutilsMatrixProbe
                  ? matrixSteps[0]
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
                : coreutilsPwdProbe
                  ? "/bin/pwd\n"
                : coreutilsEchoProbe
                  ? "/bin/echo hello world\n"
                : coreutilsBasenameProbe
                  ? "/bin/basename /usr/local/file.txt\n"
                : coreutilsPrintfProbe
                  ? "/bin/printf '%d\\n' 42\n"
                : coreutilsDirnameProbe
                  ? "/bin/dirname /usr/local/file.txt\n"
                : coreutilsCatProbe
                  ? "/bin/cat -n /usr/share/waste/cat-fixture.txt\n"
                : coreutilsWcProbe
                  ? "/bin/wc -l -w -c /usr/share/waste/wc-fixture.txt\n"
                : coreutilsLsProbe
                  ? coreutilsLsCommand ? coreutilsLsCommand + "\n" : [
                      "echo __LS_ROOT_BEGIN__; /bin/ls -1 /",
                      "echo __LS_BIN_BEGIN__; /bin/ls -1 /bin",
                      "echo __LS_EMPTY_BEGIN__; /bin/ls -A /tmp/ls-empty; echo __LS_EMPTY_END__",
                      "echo __LS_DOT_BEGIN__; /bin/ls -la /tmp/ls-fixture; echo __LS_DOT_END__",
                      "echo __LS_HIDDEN_BEGIN__; /bin/ls -A1 /tmp/ls-fixture",
                      "echo __LS_LONG_BEGIN__; /bin/ls -l /tmp/ls-fixture",
                      "echo __LS_TTY_BEGIN__; /bin/ls /tmp/ls-fixture; echo __LS_TTY_END__",
                      "/bin/ls /tmp/ls-fixture > /tmp/ls-nontty.out",
                      "echo __LS_REDIRECT_BEGIN__; /bin/cat /tmp/ls-nontty.out; echo __LS_REDIRECT_END__",
                      "echo __LS_MULTI_BEGIN__; /bin/ls -1 /tmp/ls-empty /tmp/ls-fixture",
                      "/bin/ls /tmp/ls-missing; echo __LS_MISSING_STATUS_$?__",
                      "echo __LS_SECOND_COMMAND__",
                    ].join("\n") + "\n"
                  : heredocStdoutProbe
                    ? [
                        "/bin/cat <<EOF",
                        "Hello world!",
                        "EOF",
                        "exit",
                      ].join("\n") + "\n"
                  : heredocBuiltinProbe
                    ? [
                        "read value <<EOF",
                        "Hello world!",
                        "EOF",
                        "printf '%s\\n' \"$value\"",
                        "exit",
                      ].join("\n") + "\n"
                  : heredocProbe
                    ? [
                        "/bin/cat > hello.txt <<EOF",
                        "Hello world!",
                        "EOF",
                        "echo __C_ENGINE_HEREDOC_BEGIN__",
                        "/bin/cat hello.txt",
                        "echo __C_ENGINE_HEREDOC_END__",
                        "echo __C_ENGINE_HEREDOC_MODE_BEGIN__",
                        "/bin/ls -l hello.txt",
                        "echo __C_ENGINE_HEREDOC_MODE_END__",
                        "exit",
                      ].join("\n") + "\n"
                  : readlineEchoProbe
                    ? "echo __C_ENGINE_READLINE_RESULT__"
                  : readlineCompletionProbe
                    ? "/bin/pw"
                    : "echo __C_ENGINE_BASH_OK__\n");
          if (readlineEchoProbe || readlineCompletionProbe) {
            let index = 0;
            const sendNextKey = () => {
              if (index >= initialBytes.length) return;
              self.onmessage({data: {type: "input", bytes: [initialBytes[index++]]}});
              setTimeout(sendNextKey, 5);
            };
            sendNextKey();
          } else {
            self.onmessage({data: {
              type: "input", bytes: Array.from(initialBytes),
            }});
          }
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
  Date,
  Error,
  setTimeout,
  clearTimeout,
  atob: text => Buffer.from(text, "base64").toString("binary"),
});
vm.runInContext(workerSrc, context, {filename: "c-engine-bash-worker.js"});
self.onmessage({data: {
  type: "start",
  wasmBytes: asArrayBuffer(wasmBytes),
  buildMtime,
  probeBytes: asArrayBuffer(probeBytes),
  vfsFiles: [{
    path: "/usr/share/waste/launch.wast",
    bytes: asArrayBuffer(Buffer.from(launchSource, "utf8")),
    mode: 0o644,
  }, {
    path: "/bin/waste-probe",
    bytes: asArrayBuffer(probeBytes),
    mode: 0o755,
  }, ...fullPackageFiles, ...(coreutilsTrueBytes ? [{
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
  }] : []), ...(coreutilsPwdBytes ? [{
    path: "/usr/bin/pwd",
    bytes: asArrayBuffer(coreutilsPwdBytes),
    mode: 0o755,
  }, {
    path: "/bin/pwd",
    bytes: asArrayBuffer(coreutilsPwdBytes),
    mode: 0o755,
  }] : []), ...(coreutilsEchoBytes ? [{
    path: "/usr/bin/echo",
    bytes: asArrayBuffer(coreutilsEchoBytes),
    mode: 0o755,
  }, {
    path: "/bin/echo",
    bytes: asArrayBuffer(coreutilsEchoBytes),
    mode: 0o755,
  }] : []), ...(coreutilsBasenameBytes ? [{
    path: "/usr/bin/basename",
    bytes: asArrayBuffer(coreutilsBasenameBytes),
    mode: 0o755,
  }, {
    path: "/bin/basename",
    bytes: asArrayBuffer(coreutilsBasenameBytes),
    mode: 0o755,
  }] : []), ...(coreutilsPrintfBytes ? [{
    path: "/usr/bin/printf",
    bytes: asArrayBuffer(coreutilsPrintfBytes),
    mode: 0o755,
  }, {
    path: "/bin/printf",
    bytes: asArrayBuffer(coreutilsPrintfBytes),
    mode: 0o755,
  }] : []), ...(coreutilsDirnameBytes ? [{
    path: "/usr/bin/dirname",
    bytes: asArrayBuffer(coreutilsDirnameBytes),
    mode: 0o755,
  }, {
    path: "/bin/dirname",
    bytes: asArrayBuffer(coreutilsDirnameBytes),
    mode: 0o755,
  }] : []), ...(coreutilsCatBytes ? [{
    path: "/usr/bin/cat",
    bytes: asArrayBuffer(coreutilsCatBytes),
    mode: 0o755,
  }, {
    path: "/bin/cat",
    bytes: asArrayBuffer(coreutilsCatBytes),
    mode: 0o755,
  }, {
    path: "/usr/share/waste/cat-fixture.txt",
    bytes: asArrayBuffer(Buffer.from("WASTE_CAT_FIXTURE_7f3a\n", "utf8")),
    mode: 0o644,
  }] : []), ...(coreutilsLsBytes ? [{
    path: "/usr/bin/ls",
    bytes: asArrayBuffer(coreutilsLsBytes),
    mode: 0o755,
  }, {
    path: "/bin/ls",
    bytes: asArrayBuffer(coreutilsLsBytes),
    mode: 0o755,
  }, {
    path: "/tmp/ls-empty",
    kind: 2,
    mode: 0o755,
  }, {
    path: "/tmp/ls-fixture",
    kind: 2,
    mode: 0o755,
  }, {
    path: "/tmp/ls-fixture/.hidden",
    bytes: asArrayBuffer(Buffer.from("hidden\n", "utf8")),
    mode: 0o600,
  }, {
    path: "/tmp/ls-fixture/visible",
    bytes: asArrayBuffer(Buffer.from("visible\n", "utf8")),
    mode: 0o640,
  }, {
    path: "/tmp/ls-fixture/link",
    kind: 3,
    target: "/tmp/ls-fixture/visible",
    mode: 0o777,
  }] : []), ...(coreutilsWcBytes ? [{
    path: "/usr/bin/wc",
    bytes: asArrayBuffer(coreutilsWcBytes),
    mode: 0o755,
  }, {
    path: "/bin/wc",
    bytes: asArrayBuffer(coreutilsWcBytes),
    mode: 0o755,
  }, {
    path: "/usr/share/waste/wc-fixture.txt",
    bytes: asArrayBuffer(Buffer.from("one two\nthree\n", "utf8")),
    mode: 0o644,
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
  }] : []), ...(coreutilsMatrixProbe ? [{
    path: "/tmp/matrix.wat",
    bytes: asArrayBuffer(Buffer.from(
      '(module (memory 1) (func (export "_start")))', "utf8")),
    mode: 0o755,
  }, {
    path: "/tmp/matrix.wast",
    bytes: asArrayBuffer(Buffer.from('(module)\n', "utf8")),
    mode: 0o755,
  }, {
    path: "/tmp/matrix-cat.txt",
    bytes: asArrayBuffer(Buffer.from('MATRIX_CAT_OUTPUT\n', "utf8")),
    mode: 0o644,
  }, {
    path: "/tmp/matrix-wc.txt",
    bytes: asArrayBuffer(Buffer.from('one two\nthree\n', "utf8")),
    mode: 0o644,
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
  const normalizedOutput = output.replace(/\r/g, "");
  const matrixStatusesPass = matrixCommands.every(({tag, status}) =>
    output.includes(`__C_ENGINE_MATRIX_${tag}_STATUS_${status}__`));
  const matrixOutputsPass = normalizedOutput.includes("\n/root\n") &&
    normalizedOutput.includes("\nMATRIX_ECHO_OUTPUT\n") &&
    normalizedOutput.includes("\nMATRIX_PRINTF_OUTPUT\n") &&
    normalizedOutput.includes("\nMATRIX_BASENAME_OUTPUT\n") &&
    normalizedOutput.includes("\n/alpha/beta\n") &&
    normalizedOutput.includes("\nMATRIX_CAT_OUTPUT\n") &&
    /\n\s*2\s+3\s+14\s+\/tmp\/matrix-wc\.txt\n/.test(normalizedOutput) &&
    ["true", "false", "pwd", "echo", "printf", "basename", "dirname",
     "cat", "wc", "ls", "date", "wat", "wast"].every(name =>
      normalizedOutput.includes(`\n${name}\n`)) &&
    normalizedOutput.includes(`\n${new Date().getUTCFullYear()}\n`);
  const passed = baselineMissingCommand
    ? promptSeen && commandSent && vfsSeen && environmentEchoSent && environmentEchoSeen &&
      missingLsSent && commandNotFoundSeen && statusSeen &&
      builtinSeen && genericMissingSeen && exitSent && !doneBeforeExit &&
      output.includes("__C_ENGINE_STATUS_127__") &&
      output.includes("__C_ENGINE_AFTER__") && result.ok
    : readlineEchoProbe
      ? promptSeen && commandSent && vfsSeen && readlineEchoVisibleBeforeEnter &&
        readlineEnterSent && readlineResultSeen && exitSent && !doneBeforeExit && result.ok
    : readlineCompletionProbe
      ? promptSeen && commandSent && vfsSeen && readlineTabSent &&
        readlineCompletionSeen && readlineCompletionEnterSent &&
        readlineCompletionResultSeen && exitSent && !doneBeforeExit && result.ok
    : heredocProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        /bash-[^\r\n]*[#$] \/bin\/cat hello\.txt\r?\nHello world!\r?\n/.test(output) &&
        output.includes("__C_ENGINE_HEREDOC_END__") &&
        /__C_ENGINE_HEREDOC_MODE_BEGIN__[\s\S]*\r?\n-rw-r--r--[^\r\n]* hello\.txt\r?\n[\s\S]*__C_ENGINE_HEREDOC_MODE_END__/.test(output) &&
        !output.includes("Jan  1  1970") &&
        !output.includes("cannot create temp file for here-document") && result.ok
    : heredocBuiltinProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        /bash-[^\r\n]*[#$] printf[^\r\n]*\r?\nHello world!\r?\n/.test(output) &&
        !output.includes("cannot create temp file for here-document") && result.ok
    : heredocStdoutProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        /> EOF\r?\nHello world!\r?\n/.test(output) &&
        !output.includes("cannot create temp file for here-document") && result.ok
    : executableExitProbe
      ? promptSeen && commandSent && vfsSeen && exitProbeStatusRequested &&
        probeStatus7Seen && output.includes("__C_ENGINE_EXEC_EXIT_AFTER__") &&
        exitSent && !doneBeforeExit && result.ok
    : executableProbe
      ? promptSeen && commandSent && vfsSeen && probeFdsSeen && probeSeen && probeStatus0Seen &&
        probeMissingSeen && probeStatus127Seen && probeSecondSeen &&
        probeFinalStatusSeen && probeAfterSeen && exitSent && !doneBeforeExit &&
        result.ok
    : pipelineProbe
      ? promptSeen && commandSent && vfsSeen &&
        pipelineCountSeen && pipelineRedirectDone &&
        pipelineCatSeen && pipelineAfterSeen &&
        /__PIPELINE_PIPE_BEGIN__[\s\S]*\d+[\s\S]*__PIPELINE_PIPE_END__/.test(output) &&
        /__PIPELINE_CAT_BEGIN__[\s\S]*\r?\n[\s\S]*__PIPELINE_CAT_END__/.test(output) &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsMatrixProbe
      ? promptSeen && commandSent && vfsSeen && matrixStatusesPass &&
        matrixOutputsPass && output.includes("__C_ENGINE_MATRIX_AFTER__") &&
        matrixStep === matrixSteps.length && exitSent && !doneBeforeExit && result.ok
    : coreutilsTrueProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsTrueStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_TRUE_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsFalseProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsFalseStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_FALSE_STATUS_1__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsPwdProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsPwdRootSeen && coreutilsPwdStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_PWD_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsEchoProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsEchoOutputSeen &&
        coreutilsEchoStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_ECHO_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsBasenameProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsBasenameOutputSeen &&
        coreutilsBasenameStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_BASENAME_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsPrintfProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsPrintfOutputSeen &&
        coreutilsPrintfStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_PRINTF_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsDirnameProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsDirnameOutputSeen &&
        coreutilsDirnameStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_DIRNAME_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsCatProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsCatOutputSeen &&
        coreutilsCatStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_CAT_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsWcProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsWcOutputSeen &&
        coreutilsWcStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_WC_STATUS_0__") &&
        exitSent && !doneBeforeExit && result.ok
    : coreutilsLsProbe
      ? promptSeen && commandSent && vfsSeen && coreutilsLsOutputSeen &&
        coreutilsLsStatusSeen &&
        output.includes("__C_ENGINE_COREUTILS_LS_STATUS_0__") &&
        !output.includes("Jan  1  1970") &&
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
