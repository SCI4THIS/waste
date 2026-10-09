#!/usr/bin/env node
"use strict";
const {config, withConfig} = require("./runtime-config.cjs");

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const {TextDecoder, TextEncoder} = require("node:util");
const {readOfflinePackage} = require("./offline-html-package.cjs");

const root = path.resolve(__dirname, "..");
const vfsRoot = path.join(root, "src/vfs");
const stagingDir = path.join(vfsRoot, "usr/bin");
const {installedVfs, treeManifest, treeVfs, packageVfs, stageVfs} = require("./vfs-package.cjs");
const frontendDir = path.join(root, "src/html-rt/src");
const pagePath = process.argv.slice(2).find(arg => arg.endsWith(".html"));
const archive = pagePath ? readOfflinePackage(pagePath) : null;
const manifest = archive ? JSON.parse(archive.read("vfs-manifest.json"))
  : treeManifest(vfsRoot);
const entries = new Map(manifest.entries.map(e => [e.path, e]));
function assetName(filename) {
  if (filename.startsWith(vfsRoot + path.sep)) return path.relative(vfsRoot, filename).replaceAll(path.sep, "/");
  return path.basename(filename);
}
function hasAsset(filename) {
  return archive ? archive.files.has(assetName(filename)) : fs.existsSync(filename);
}
function readAsset(filename) {
  return archive ? archive.read(assetName(filename)) : fs.readFileSync(filename);
}
function assetMtimeMs(filename) {
  if (!archive) return fs.statSync(filename).mtimeMs;
  const e = entries.get("/" + assetName(filename));
  if (!e) return fs.statSync(filename).mtimeMs; // host bootstrap, never guest metadata
  return e.mtime_sec * 1000 + e.mtime_nsec / 1000000;
}
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
const expectedPipelineBinNames = fullPackageProbe
  ? manifest.entries.filter(e => e.path.startsWith("/bin/") && !e.path.slice(5).includes("/"))
      .map(e => e.path.slice(5)).sort()
  : ["wast", "waste-probe", "wat"];
const expectedRootNames = fullPackageProbe
  ? manifest.entries.filter(e => e.kind === 2 && /^\/[^/]+$/.test(e.path))
      .map(e => e.path.slice(1)).sort()
  : ["bin", "lib", "root", "tmp", "usr"];
const expectedBinNames = fullPackageProbe
  ? manifest.entries.filter(e => /^\/bin\/[^/]+$/.test(e.path))
      .map(e => e.path.slice(5)).sort()
  : ["wast", "waste-probe", "wat"];
function rootListingSeen(text) {
  return listingSeen(text, "__LS_ROOT_BEGIN__", expectedRootNames);
}
function binListingSeen(text) {
  return listingSeen(text, "__LS_BIN_BEGIN__", expectedBinNames);
}
function listingSeen(text, marker, expected) {
  const markerIndex = text.lastIndexOf(marker);
  if (markerIndex < 0) return false;
  const lines = text.slice(markerIndex + marker.length)
    .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "")
    .replace(/\r\n?/g, "\n").split("\n").filter(Boolean);
  return lines.slice(0, expected.length).join("\n") === expected.join("\n");
}
function markedOutput(text, startMarker, endMarker) {
  const start = text.lastIndexOf(startMarker);
  if (start < 0) return null;
  const contentStart = start + startMarker.length;
  const end = text.indexOf(endMarker, contentStart);
  if (end < 0) return null;
  return text.slice(contentStart, end)
    .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "")
    .replace(/\r\n?/g, "\n").trim();
}
function executableStartupSeen(text, expectedArgc) {
  const entry = text.lastIndexOf("WASTE_PROBE_ENTRY_OK");
  if (entry < 0) return false;
  const end = text.indexOf("WASTE_PROBE_OK", entry);
  if (end < 0) return false;
  const startup = text.slice(entry, end)
    .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "")
    .replace(/\r\n?/g, "\n");
  const argc = startup.match(/WASTE_PROBE_ARGC=(\d+)/);
  const argv0 = startup.match(/WASTE_PROBE_ARG0=([^\n]+)/);
  const envc = startup.match(/WASTE_PROBE_ENVC=(\d+)/);
  const pid = startup.match(/WASTE_PROBE_PID=(\d+)/);
  const cwd = startup.match(/WASTE_PROBE_CWD=([^\n]+)/);
  return Boolean(argc && Number(argc[1]) === expectedArgc && argv0 &&
    argv0[1] === "/bin/waste-probe" && envc && Number(envc[1]) > 0 &&
    pid && Number(pid[1]) > 0 && cwd && cwd[1] === "/root");
}
const sharedLibraryProbe = process.argv.includes("--shared-library");
const readlineEchoProbe = process.argv.includes("--readline-echo");
const readlineCompletionProbe = process.argv.includes("--readline-completion");
const readlineArrowProbe = process.argv.includes("--readline-arrow");
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
  {tag: "TRUE", command: "/usr/bin/true", status: 0},
  {tag: "FALSE", command: "/usr/bin/false", status: 1},
  {tag: "PWD", command: "/usr/bin/pwd", status: 0},
  {tag: "ECHO", command: "/usr/bin/echo MATRIX_ECHO_OUTPUT", status: 0},
  {tag: "PRINTF", command: "/usr/bin/printf 'MATRIX_PRINTF_OUTPUT\\n'", status: 0},
  {tag: "BASENAME", command: "/usr/bin/basename /alpha/MATRIX_BASENAME_OUTPUT", status: 0},
  {tag: "DIRNAME", command: "/usr/bin/dirname /alpha/beta/MATRIX_FILE", status: 0},
  {tag: "CAT", command: "/usr/bin/cat /tmp/matrix-cat.txt", status: 0},
  {tag: "CHMOD", command: "/usr/bin/chmod 600 /tmp/matrix-cat.txt", status: 0},
  {tag: "WC", command: "/usr/bin/wc -l -w -c /tmp/matrix-wc.txt", status: 0},
  {tag: "LS", command: "/usr/bin/ls -1 /bin /usr/bin", status: 0},
  {tag: "DATE", command: "/usr/bin/date -u +%Y", status: 0},
  {tag: "WAT", command: "wat /tmp/matrix.wat", status: 0},
  {tag: "WAST", command: "wast /tmp/matrix.wast", status: 0},
];
const matrixSteps = matrixCommands.flatMap(({tag, command}) => [
  command + "\n",
  `printf '__C_ENGINE_MATRIX_${tag}_STATUS_%s__\\n' "$?"\n`,
]).concat(["echo __C_ENGINE_MATRIX_AFTER__\n", "exit\n"]);

/* Keep the acceptance probe tied to the pathname/process imports in the Bash
 * artifact. */
const bashImports = WebAssembly.Module.imports(new WebAssembly.Module(
  readAsset(path.join(vfsRoot, "usr/bin/bash"))));
for (const name of ["stat", "lstat", "fstat", "eaccess", "faccessat", "fork"]) {
  if (!bashImports.some(entry => entry.kind === "function" && entry.name === name)) {
    throw new Error(`Installed Bash import inventory lost ${name}`);
  }
}

/* Load worker source, wasm, and launch script from staging files */
const workerSrc = archive ? archive.read("root/app/worker.js").toString("utf8")
  : withConfig(fs.readFileSync(path.join(frontendDir, "worker.js"), "utf8"));

const wasmPath = path.join(root, "build/html-rt/waste-wast.wasm");
const wasmBytes = readAsset(wasmPath);
const wasmMtimeMs = assetMtimeMs(wasmPath);
const buildMtime = {
  sec: Math.floor(wasmMtimeMs / 1000),
  nsec: Math.floor(wasmMtimeMs % 1000) * 1000000,
};
let probePath = path.join(stagingDir, "waste-probe");
const builtProbePath = path.join(root, "build/cli-rt/waste-probe.wasm");
if (!archive && hasAsset(builtProbePath)) probePath = builtProbePath;
const probeBytes = (executableProbe || executableExitProbe) && hasAsset(probePath)
  ? readAsset(probePath) : null;
const coreutilsTruePath = path.join(stagingDir, "true");
const coreutilsFalsePath = path.join(stagingDir, "false");
const coreutilsPwdPath = path.join(stagingDir, "pwd");
const coreutilsEchoPath = path.join(stagingDir, "echo");
const coreutilsBasenamePath = path.join(stagingDir, "basename");
const coreutilsPrintfPath = path.join(stagingDir, "printf");
const coreutilsDirnamePath = path.join(stagingDir, "dirname");
const coreutilsCatPath = path.join(stagingDir, "cat");
const coreutilsWcPath = path.join(stagingDir, "wc");
const coreutilsLsPath = path.join(stagingDir, "ls");
const coreutilsDatePath = path.join(stagingDir, "date");
const coreutilsTrueBytes = (coreutilsTrueProbe || coreutilsMatrixProbe) && hasAsset(coreutilsTruePath)
  ? readAsset(coreutilsTruePath) : null;
const coreutilsFalseBytes = (coreutilsFalseProbe || coreutilsMatrixProbe) && hasAsset(coreutilsFalsePath)
  ? readAsset(coreutilsFalsePath) : null;
const coreutilsPwdBytes = (coreutilsPwdProbe || coreutilsMatrixProbe) && hasAsset(coreutilsPwdPath)
  ? readAsset(coreutilsPwdPath) : null;
const coreutilsEchoBytes = (coreutilsEchoProbe || coreutilsMatrixProbe) && hasAsset(coreutilsEchoPath)
  ? readAsset(coreutilsEchoPath) : null;
const coreutilsBasenameBytes = (coreutilsBasenameProbe || coreutilsMatrixProbe) && hasAsset(coreutilsBasenamePath)
  ? readAsset(coreutilsBasenamePath) : null;
const coreutilsPrintfBytes = (coreutilsPrintfProbe || coreutilsMatrixProbe) && hasAsset(coreutilsPrintfPath)
  ? readAsset(coreutilsPrintfPath) : null;
const coreutilsDirnameBytes = (coreutilsDirnameProbe || coreutilsMatrixProbe) && hasAsset(coreutilsDirnamePath)
  ? readAsset(coreutilsDirnamePath) : null;
const coreutilsCatBytes = (coreutilsCatProbe || coreutilsLsProbe || coreutilsMatrixProbe ||
  pipelineProbe || heredocProbe || heredocStdoutProbe) &&
  hasAsset(coreutilsCatPath)
  ? readAsset(coreutilsCatPath) : null;
const coreutilsWcBytes = (coreutilsWcProbe || coreutilsMatrixProbe || pipelineProbe) && hasAsset(coreutilsWcPath)
  ? readAsset(coreutilsWcPath) : null;
const coreutilsLsBytes = (coreutilsLsProbe || coreutilsMatrixProbe || pipelineProbe || heredocProbe) && hasAsset(coreutilsLsPath)
  ? readAsset(coreutilsLsPath) : null;
const coreutilsDateBytes = coreutilsMatrixProbe && hasAsset(coreutilsDatePath)
  ? readAsset(coreutilsDatePath) : null;
const asArrayBuffer = bytes => bytes && bytes.buffer.slice(
  bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
const mountedVfs = fullPackageProbe || sharedLibraryProbe;
const vfs = mountedVfs ? archive ? packageVfs(archive) : treeVfs(vfsRoot) : null;
const fullPackageFiles = [];
const sharedLibraryFiles = [];
const launchPath = path.join(vfsRoot, "usr/share/waste/launch.wast");
const launchSource = archive ? archive.read("launch.wast").toString("utf8")
  : fs.readFileSync(launchPath, "utf8");

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
let probeStartupSeen = false;
let probeFinalStatusSeen = false;
let probeStatus7Seen = false;
let probeSecondOutputOffset = -1;
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
let readlineArrowSequenceSent = false;
let readlineArrowOutputOffset = 0;
let readlineArrowRedisplaySeen = false;
let readlineArrowEnterSent = false;
let readlineArrowHistoryResultSeen = false;
let readlineArrowAfterSeen = false;
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
let rogueScreenSeen = false;
let rogueArrowSent = false;
let rogueArrowDispatched = false;
let rogueArrowHandled = false;
let rogueTurnDispatched = false;
let rogueTurnHandled = false;
let rogueQuitSent = false;
let rogueContinueSent = false;
let rogueStatusRequested = false;
let rogueRun = 0;
let rogueFirstLifecycleSeen = false;
let rogueFirstStatusSeen = false;
let rogueSecondStatusSeen = false;
let roguePostInputSent = false;
let roguePostInputSeen = false;
let rogueCursorHiddenSeen = false;
let rogueCursorRestoredSeen = false;
let rogueLoadFailureSeen = false;
let sharedLayoutSeen = false;
let sharedEnvironmentSeen = false;
let lddNcursesSeen = false;
let lddStatusSeen = false;
let lddRequested = false;
let rogueLaunchSent = false;
let lddLoadFailureSeen = false;
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
            !coreutilsWcProbe && !coreutilsLsProbe && !coreutilsMatrixProbe &&
            !wastRepeatProbe && !(heredocProbe || heredocStdoutProbe) && !sharedLibraryProbe) ||
            (sharedLibraryProbe &&
             message.paths?.includes("/usr/bin/rogue") &&
             message.paths?.includes("/usr/bin/ldd") &&
             message.paths?.includes("/usr/lib/libncurses.so.wasm") &&
             message.paths?.includes("/lib/libncurses.so.wasm")) ||
            (coreutilsTrueProbe && message.paths?.includes("/usr/bin/true")) ||
            (coreutilsFalseProbe && message.paths?.includes("/usr/bin/false")) ||
            (coreutilsPwdProbe && message.paths?.includes("/usr/bin/pwd")) ||
            (coreutilsEchoProbe && message.paths?.includes("/usr/bin/echo")) ||
            (coreutilsBasenameProbe && message.paths?.includes("/usr/bin/basename")) ||
            (coreutilsPrintfProbe && message.paths?.includes("/usr/bin/printf")) ||
            (coreutilsDirnameProbe && message.paths?.includes("/usr/bin/dirname")) ||
            (coreutilsCatProbe && message.paths?.includes("/usr/bin/cat") &&
             message.paths?.includes("/usr/share/waste/cat-fixture.txt")) ||
            (heredocStdoutProbe && message.paths?.includes("/usr/bin/cat")) ||
            (heredocProbe && message.paths?.includes("/usr/bin/cat") &&
             message.paths?.includes("/usr/bin/ls")) ||
            (coreutilsWcProbe && message.paths?.includes("/usr/bin/wc") &&
             message.paths?.includes("/usr/share/waste/wc-fixture.txt")) ||
            (coreutilsLsProbe && message.paths?.includes("/usr/bin/ls")) ||
            (coreutilsMatrixProbe &&
             ["true", "false", "pwd", "echo", "printf", "basename",
              "dirname", "cat", "wc", "ls", "date"].every(name =>
                message.paths?.includes(`/usr/bin/${name}`)) &&
             message.paths?.includes("/bin/wat") &&
             message.paths?.includes("/bin/wast") &&
             message.paths?.includes("/tmp/matrix.wat") &&
             message.paths?.includes("/tmp/matrix.wast")) ||
            (wastRepeatProbe &&
             ["/bin/wat", "/bin/wast", "/tmp/checks.wast",
              "/tmp/checks-direct.wast", "/tmp/wast-shebang.wast"].every(path =>
                message.paths?.includes(path))) ||
            (pipelineProbe &&
             ["ls", "wc", "cat"].every(name =>
                message.paths?.includes(`/usr/bin/${name}`))))) vfsSeen = true;
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
          /(?:\r?\n|\r)__C_ENGINE_READLINE_RESULT__\r?\n/.test(output) &&
          !readlineResultSeen) {
        readlineResultSeen = true;
        exitSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input",
          bytes: Array.from(new TextEncoder().encode("exit\n")),
        }}), 10);
      }
      if (readlineCompletionProbe && !readlineTabSent &&
          output.includes("/usr/bin/pw")) {
        readlineTabSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: [9],
        }}), 10);
      }
      if (readlineCompletionProbe && readlineTabSent &&
          !readlineCompletionEnterSent && output.includes("/usr/bin/pwd")) {
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
      if (readlineArrowProbe && !readlineArrowSequenceSent &&
          /(?:\r?\n|\r)__C_ENGINE_ARROW_HISTORY__\r?\n/.test(output) &&
          /# /.test(message.text)) {
        readlineArrowSequenceSent = true;
        readlineArrowOutputOffset = output.length;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: [27, 91, 65],
        }}), 10);
      }
      if (readlineArrowProbe && readlineArrowSequenceSent &&
          !readlineArrowEnterSent &&
          output.slice(readlineArrowOutputOffset).includes(
            "echo __C_ENGINE_ARROW_HISTORY__")) {
        readlineArrowRedisplaySeen = true;
        readlineArrowEnterSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: [10],
        }}), 10);
      }
      if (readlineArrowProbe && readlineArrowEnterSent &&
          (output.match(/(?:\r?\n|\r)__C_ENGINE_ARROW_HISTORY__\r?\n/g) || []).length >= 2 &&
          !readlineArrowHistoryResultSeen) {
        readlineArrowHistoryResultSeen = true;
        exitSent = true;
        setTimeout(() => self.onmessage({data: {
          type: "input", bytes: Array.from(new TextEncoder().encode(
            "echo __C_ENGINE_ARROW_AFTER__\nexit\n")),
        }}), 10);
      }
      if (readlineArrowProbe && output.includes("__C_ENGINE_ARROW_AFTER__"))
        readlineArrowAfterSeen = true;
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
          rootListingSeen(output) &&
          binListingSeen(output) &&
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
      if (sharedLibraryProbe) {
        if (message.text.includes("\x1b[?25l")) rogueCursorHiddenSeen = true;
        if (rogueCursorHiddenSeen && message.text.includes("\x1b[?25h"))
          rogueCursorRestoredSeen = true;
        if (output.includes(
            "__C_ENGINE_ENV_/root|root|root|/root|/bin:/usr/bin|xterm__")) {
          sharedEnvironmentSeen = true;
        }
        if (/libncurses\s+=>\s+\/usr\/lib\/libncurses\.so\.wasm/.test(output)) {
          lddNcursesSeen = true;
        }
        if (output.includes("__C_ENGINE_LDD_STATUS_0__")) lddStatusSeen = true;
        if (!lddLoadFailureSeen && output.includes("__C_ENGINE_LDD_STATUS_126__")) {
          lddLoadFailureSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n")),
          }}), 10);
        }
        if (output.includes("__C_ENGINE_SHARED_LAYOUT_OK__") &&
            /d[rwx-]{9}[^\r\n]* \/usr(?:\r?\n|$)/.test(output) &&
            /-[rwx-]{9}[^\r\n]* \/usr\/bin\/rogue(?:\r?\n|$)/.test(output) &&
            /-[rwx-]{9}[^\r\n]* \/lib\/libncurses\.so\.wasm(?:\r?\n|$)/.test(output)) {
          sharedLayoutSeen = true;
        }
        const messageHasPrompt = /# /.test(message.text);
        if (sharedEnvironmentSeen && sharedLayoutSeen && messageHasPrompt &&
            !lddRequested) {
          lddRequested = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "ldd /usr/bin/rogue; " +
              "printf '__C_ENGINE_LDD_STATUS_%s__\\n' \"$?\"\n",
            ))}}), 10);
        }
        if (lddRequested && lddNcursesSeen && lddStatusSeen &&
            messageHasPrompt && !rogueLaunchSent) {
          rogueLaunchSent = true;
          rogueStatusRequested = true;
          rogueRun = 1;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "rogue; " +
              "printf '__C_ENGINE_ROGUE_1_STATUS_%s__\\n' \"$?\"\n",
            )),
          }}), 10);
        }
        if (!rogueLoadFailureSeen && output.includes("bash: rogue: errno 8")) {
          rogueLoadFailureSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (!rogueLoadFailureSeen &&
            (output.includes("Error opening terminal:") ||
             output.includes("Sorry, the screen must be at least 24x80"))) {
          rogueLoadFailureSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
        if (!rogueLoadFailureSeen && rogueLaunchSent && !rogueScreenSeen &&
            /\x1b\[[0-9;?]*[A-Za-z]/.test(message.text) &&
            !messageHasPrompt) {
          rogueScreenSeen = true;
          rogueArrowSent = true;
          setTimeout(() => {
            rogueArrowDispatched = true;
            self.onmessage({data: {type: "input", bytes: [27, 79, 65]}});
          }, 10);
        }
        if (rogueQuitSent && !rogueContinueSent &&
            output.includes("[Press return to continue]")) {
          rogueContinueSent = true;
          setTimeout(() => self.onmessage({data: {
            type: "input", bytes: [10],
          }}), 10);
        }
        if (output.includes("__C_ENGINE_ROGUE_1_STATUS_0__") &&
            !rogueFirstStatusSeen) {
          rogueFirstStatusSeen = true;
          rogueFirstLifecycleSeen = rogueScreenSeen && rogueArrowSent &&
            rogueArrowHandled && rogueTurnDispatched && rogueTurnHandled &&
            rogueQuitSent && rogueContinueSent;
          rogueRun = 2;
          rogueScreenSeen = false;
          rogueArrowSent = false;
          rogueArrowDispatched = false;
          rogueArrowHandled = false;
          rogueTurnDispatched = false;
          rogueTurnHandled = false;
          rogueQuitSent = false;
          rogueContinueSent = false;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "rogue; " +
              "printf '__C_ENGINE_ROGUE_2_STATUS_%s__\\n' \"$?\"\n",
            )),
          }}), 10);
        } else if (output.includes("__C_ENGINE_ROGUE_2_STATUS_0__") &&
                   !rogueSecondStatusSeen) {
          rogueSecondStatusSeen = true;
        }
        if (rogueSecondStatusSeen && messageHasPrompt &&
            !roguePostInputSent) {
          roguePostInputSent = true;
          const bytes = new TextEncoder().encode(
            "printf '__C_ENGINE_ROGUE_POST_INPUT_OK__\\n'\n");
          for (let i = 0; i < bytes.length; i++) {
            setTimeout(() => self.onmessage({data: {
              type: "input", bytes: [bytes[i]],
            }}), 10 + i * 2);
          }
        }
        if (output.includes("__C_ENGINE_ROGUE_POST_INPUT_OK__") &&
            !roguePostInputSeen) {
          roguePostInputSeen = true;
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        } else if (/__C_ENGINE_ROGUE_[12]_STATUS_(?!0__)\d+__/.test(output) &&
                   !exitSent) {
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
        }
      } else if (baselineMissingCommand) {
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
        const messageHasPrompt = /# /.test(message.text);
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
          probeStartupSeen = executableStartupSeen(output, 3);
        }
        const initialStatus = output.match(/__C_ENGINE_EXEC_INITIAL_STATUS_(\d+)__/);
        if (initialStatus && Number(initialStatus[1]) === 0 && !probeStatus0Seen) {
          probeStatus0Seen = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              `${genericMissingCommand}\n`,
            ))}}), 10);
        } else if (initialStatus && Number(initialStatus[1]) !== 0 && !exitSent) {
          exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode("exit\n"))}}), 10);
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
          probeSecondOutputOffset = output.length;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(
              "waste-probe again; printf '__C_ENGINE_EXEC_SECOND_STATUS_%s__\\n' \"$?\"; exit\n",
            ))}}), 10);
        }
        const secondStatus = output.match(/__C_ENGINE_EXEC_SECOND_STATUS_(\d+)__/);
        if (secondStatus && !probeFinalStatusSeen) {
          probeSecondSeen = probeSecondOutputOffset >= 0 &&
            output.slice(probeSecondOutputOffset).includes("WASTE_PROBE_OK");
          probeStartupSeen = probeStartupSeen &&
            executableStartupSeen(output.slice(probeSecondOutputOffset), 2);
          probeFinalStatusSeen = Number(secondStatus[1]) === 0;
          probeAfterSeen = probeSecondSeen && Number(secondStatus[1]) === 0;
          exitSent = true;
        }
      } else if (coreutilsMatrixProbe) {
        const messageHasPrompt = /# /.test(message.text);
        if (commandSent && messageHasPrompt && matrixStep < matrixSteps.length) {
          const command = matrixSteps[matrixStep++];
          if (command === "exit\n") exitSent = true;
          setTimeout(() => self.onmessage({data: {type: "input",
            bytes: Array.from(new TextEncoder().encode(command))}}), 10);
        }
      } else if (pipelineProbe) {
        const messageHasPrompt = /# /.test(message.text);
        const pipelineCommands = [
          "echo __PIPELINE_PIPE_BEGIN__; ls /bin | wc -l; echo __PIPELINE_PIPE_END__\n",
          "ls -1 /bin > /tmp/pipeline-list.txt; printf '__PIPELINE_REDIRECT_STATUS_%s__\\n' \"$?\"\n",
          "echo __PIPELINE_CAT_BEGIN__; cat /tmp/pipeline-list.txt; echo __PIPELINE_CAT_END__\n",
          "echo __PIPELINE_AFTER__\n",
          "exit\n",
        ];
        const pipeCountText = markedOutput(output,
          "__PIPELINE_PIPE_BEGIN__", "__PIPELINE_PIPE_END__");
        if (pipeCountText !== null &&
            Number(pipeCountText) === expectedPipelineBinNames.length)
          pipelineCountSeen = true;
        if (output.includes("__PIPELINE_REDIRECT_STATUS_0__"))
          pipelineRedirectDone = true;
        const redirectedListing = markedOutput(output,
          "__PIPELINE_CAT_BEGIN__", "__PIPELINE_CAT_END__");
        if (redirectedListing === expectedPipelineBinNames.join("\n"))
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
        const messageHasPrompt = /# /.test(message.text);
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
        const messageHasPrompt = /# /.test(message.text);
        if (commandSent && messageHasPrompt) {
          const commands = [
            "printf '__C_ENGINE_WAT_REPEAT_1_%s__\\n' \"$?\"\n",
            "wat /tmp/checks.wast\n",
            "printf '__C_ENGINE_WAT_REPEAT_2_%s__\\n' \"$?\"\n",
            "wat /tmp/checks.wast\n",
            "printf '__C_ENGINE_WAT_REPEAT_3_%s__\\n' \"$?\"\n",
            "wast /tmp/checks.wast\n",
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
        const messageHasPrompt = /# /.test(message.text);
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
      const messageHasPrompt = /# /.test(message.text);
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
                  ? "echo __PIPELINE_PIPE_BEGIN__; ls /bin | wc -l; echo __PIPELINE_PIPE_END__\n"
                : coreutilsMatrixProbe
                  ? matrixSteps[0]
                : runTextProbe
                  ? (wastRepeatProbe ? "wat /tmp/checks.wast\n" :
                     watShebangProbe ? "/tmp/wat-shebang.wat\n" :
                     watFailureProbe ? "wat /tmp/wat-bad.wat\n" :
                     watDirectProbe ? "/tmp/wat-direct.wat\n" :
                     (wastProbe || wastShebangProbe || wastFailureProbe ||
                      wastDirectProbe || wastRepeatProbe) ?
                       (wastShebangProbe ? "/tmp/wast-shebang.wast\n" :
                         wastFailureProbe ? "wast /tmp/bad.wast\n" :
                         wastDirectProbe ? "/tmp/checks-direct.wast\n" :
                                           "wast /tmp/checks.wast\n") :
                     "wat /tmp/wat-probe.wat\n")
                : sharedLibraryProbe
                  ? "printf '__C_ENGINE_ENV_%s|%s|%s|%s|%s|%s__\\n' " +
                    "\"$HOME\" \"$USER\" \"$LOGNAME\" \"$PWD\" \"$PATH\" \"$TERM\"; " +
                    "ls -ld /usr /usr/bin /usr/lib /lib /usr/bin/ldd " +
                    "/usr/bin/rogue /lib/libncurses.so.wasm " +
                    "/usr/lib/libncurses.so.wasm && " +
                    "echo __C_ENGINE_SHARED_''LAYOUT_OK__\n"
                : executableExitProbe
                  ? "WASTE_PROBE_EXIT=7 waste-probe\n"
                : executableProbe
                  ? "waste-probe one two; printf '__C_ENGINE_EXEC_INITIAL_STATUS_%s__\\n' \"$?\"\n"
                : coreutilsTrueProbe
                  ? "true\n"
                : coreutilsFalseProbe
                  ? "false\n"
                : coreutilsPwdProbe
                  ? "pwd\n"
                : coreutilsEchoProbe
                  ? "echo hello world\n"
                : coreutilsBasenameProbe
                  ? "basename /usr/local/file.txt\n"
                : coreutilsPrintfProbe
                  ? "printf '%d\\n' 42\n"
                : coreutilsDirnameProbe
                  ? "dirname /usr/local/file.txt\n"
                : coreutilsCatProbe
                  ? "cat -n /usr/share/waste/cat-fixture.txt\n"
                : coreutilsWcProbe
                  ? "wc -l -w -c /usr/share/waste/wc-fixture.txt\n"
                : coreutilsLsProbe
                  ? coreutilsLsCommand ? coreutilsLsCommand + "\n" : [
                      "echo __LS_ROOT_BEGIN__; ls -1 /",
                      "echo __LS_BIN_BEGIN__; ls -1 /bin",
                      "echo __LS_EMPTY_BEGIN__; ls -A /tmp/ls-empty; echo __LS_EMPTY_END__",
                      "echo __LS_DOT_BEGIN__; ls -la /tmp/ls-fixture; echo __LS_DOT_END__",
                      "echo __LS_HIDDEN_BEGIN__; ls -A1 /tmp/ls-fixture",
                      "echo __LS_LONG_BEGIN__; ls -l /tmp/ls-fixture",
                      "echo __LS_TTY_BEGIN__; ls /tmp/ls-fixture; echo __LS_TTY_END__",
                      "ls /tmp/ls-fixture > /tmp/ls-nontty.out",
                      "echo __LS_REDIRECT_BEGIN__; cat /tmp/ls-nontty.out; echo __LS_REDIRECT_END__",
                      "echo __LS_MULTI_BEGIN__; ls -1 /tmp/ls-empty /tmp/ls-fixture",
                      "ls /tmp/ls-missing; echo __LS_MISSING_STATUS_$?__",
                      "echo __LS_SECOND_COMMAND__",
                    ].join("\n") + "\n"
                  : heredocStdoutProbe
                    ? [
                        "cat <<EOF",
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
                        "cat > hello.txt <<EOF",
                        "Hello world!",
                        "EOF",
                        "echo __C_ENGINE_HEREDOC_BEGIN__",
                        "cat hello.txt",
                        "echo __C_ENGINE_HEREDOC_END__",
                        "echo __C_ENGINE_HEREDOC_MODE_BEGIN__",
                        "ls -l hello.txt",
                        "echo __C_ENGINE_HEREDOC_MODE_END__",
                        "exit",
                      ].join("\n") + "\n"
                  : readlineEchoProbe
                    ? "echo __C_ENGINE_READLINE_RESULT__"
                  : readlineCompletionProbe
                    ? "/usr/bin/pw"
                  : readlineArrowProbe
                    ? "echo __C_ENGINE_ARROW_HISTORY__\n"
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
    } else if (message.type === "io-ready" && sharedLibraryProbe &&
               rogueArrowDispatched && !rogueArrowHandled) {
      /* The image yielded for terminal input again after consuming the
       * complete application-cursor sequence, so it neither trapped nor
       * returned prematurely to Bash. */
      rogueArrowHandled = true;
      rogueTurnDispatched = true;
      setTimeout(() => self.onmessage({data: {
        type: "input", bytes: [46],
      }}), 10);
    } else if (message.type === "io-ready" && sharedLibraryProbe &&
               rogueTurnDispatched && !rogueTurnHandled) {
      /* Waiting in place always advances a game turn.  Reaching another
       * input wait proves Rogue's daemon callbacks survived a guaranteed
       * turn even when the preceding arrow happened to face a wall. */
      rogueTurnHandled = true;
      rogueQuitSent = true;
      setTimeout(() => self.onmessage({data: {
        type: "input", bytes: Array.from(new TextEncoder().encode("Qy")),
      }}), 10);
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
  buildMtime: mountedVfs ? undefined : buildMtime,
  vfs,
  vfsPaths: mountedVfs ? manifest.entries.map(e => e.path) : [],
  probeBytes: asArrayBuffer(probeBytes),
  vfsFiles: [...(mountedVfs ? [] : [{
    path: "/usr/bin/bash",
    bytes: asArrayBuffer(readAsset(path.join(vfsRoot, "usr/bin/bash"))),
    mode: 0o755,
  }, {
    path: "/usr/lib/libc.so.wasm",
    bytes: asArrayBuffer(readAsset(path.join(vfsRoot, "usr/lib/libc.so.wasm"))),
    mode: 0o644,
  }, {
    path: "/usr/share/waste/launch.wast",
    bytes: asArrayBuffer(Buffer.from(launchSource, "utf8")),
    mode: 0o644,
  }, {
    path: "/bin/waste-probe",
    bytes: asArrayBuffer(probeBytes),
    mode: 0o755,
  }]), ...fullPackageFiles, ...sharedLibraryFiles, ...(coreutilsTrueBytes ? [{
    path: "/usr/bin/true",
    bytes: asArrayBuffer(coreutilsTrueBytes),
    mode: 0o755,
  }] : []), ...(coreutilsFalseBytes ? [{
    path: "/usr/bin/false",
    bytes: asArrayBuffer(coreutilsFalseBytes),
    mode: 0o755,
  }] : []), ...(coreutilsPwdBytes ? [{
    path: "/usr/bin/pwd",
    bytes: asArrayBuffer(coreutilsPwdBytes),
    mode: 0o755,
  }] : []), ...(coreutilsEchoBytes ? [{
    path: "/usr/bin/echo",
    bytes: asArrayBuffer(coreutilsEchoBytes),
    mode: 0o755,
  }] : []), ...(coreutilsBasenameBytes ? [{
    path: "/usr/bin/basename",
    bytes: asArrayBuffer(coreutilsBasenameBytes),
    mode: 0o755,
  }] : []), ...(coreutilsPrintfBytes ? [{
    path: "/usr/bin/printf",
    bytes: asArrayBuffer(coreutilsPrintfBytes),
    mode: 0o755,
  }] : []), ...(coreutilsDirnameBytes ? [{
    path: "/usr/bin/dirname",
    bytes: asArrayBuffer(coreutilsDirnameBytes),
    mode: 0o755,
  }] : []), ...(coreutilsCatBytes ? [{
    path: "/usr/bin/cat",
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
  }] : []), ...(coreutilsDateBytes ? [{
    path: "/usr/bin/date",
    bytes: asArrayBuffer(coreutilsDateBytes),
    mode: 0o755,
  }] : []), ...(coreutilsWcBytes ? [{
    path: "/usr/bin/wc",
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
  const normalizedOutput = output
    .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "")
    .replace(/\r/g, "");
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
  const wastRepeatStatusesPass = [
    "WAT_REPEAT_1_0", "WAT_REPEAT_2_0", "WAT_REPEAT_3_0",
    "WAST_REPEAT_1_0", "WAST_REPEAT_2_0", "WAST_REPEAT_3_0",
  ].every(tag => output.includes(`__C_ENGINE_${tag}__`));
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
    : readlineArrowProbe
      ? promptSeen && commandSent && vfsSeen && readlineArrowSequenceSent &&
        readlineArrowRedisplaySeen && readlineArrowEnterSent &&
        readlineArrowHistoryResultSeen && readlineArrowAfterSeen &&
        exitSent && !doneBeforeExit && result.ok
    : heredocProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        /__C_ENGINE_HEREDOC_BEGIN__[\s\S]*cat hello\.txt[\s\S]*Hello world![\s\S]*__C_ENGINE_HEREDOC_END__/.test(output) &&
        output.includes("__C_ENGINE_HEREDOC_END__") &&
        /__C_ENGINE_HEREDOC_MODE_BEGIN__[\s\S]*-rw-r--r--[^\r\n]* hello\.txt[\s\S]*__C_ENGINE_HEREDOC_MODE_END__/.test(output) &&
        !output.includes("Jan  1  1970") &&
        !output.includes("cannot create temp file for here-document") && result.ok
    : heredocBuiltinProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        /# printf[^\r\n]*\r?\n(?:\x1b\[\?2004l\r)?Hello world!\r?\n/.test(output) &&
        !output.includes("cannot create temp file for here-document") && result.ok
    : heredocStdoutProbe
      ? promptSeen && commandSent && vfsSeen && exitSent && !doneBeforeExit &&
        /> EOF\r?\n(?:\x1b\[\?2004l\r)?Hello world!\r?\n/.test(output) &&
        !output.includes("cannot create temp file for here-document") && result.ok
    : executableExitProbe
      ? promptSeen && commandSent && vfsSeen && exitProbeStatusRequested &&
        probeStatus7Seen && output.includes("__C_ENGINE_EXEC_EXIT_AFTER__") &&
        exitSent && !doneBeforeExit && result.ok
    : executableProbe
      ? promptSeen && commandSent && vfsSeen && probeFdsSeen && probeSeen && probeStartupSeen && probeStatus0Seen &&
        probeMissingSeen && probeStatus127Seen && probeSecondSeen &&
        probeFinalStatusSeen && probeAfterSeen && exitSent && !doneBeforeExit &&
        result.ok
    : sharedLibraryProbe
      ? promptSeen && commandSent && vfsSeen && sharedLayoutSeen &&
        sharedEnvironmentSeen && lddNcursesSeen && lddStatusSeen && rogueScreenSeen &&
        lddRequested && rogueLaunchSent && rogueArrowSent && rogueArrowHandled &&
        rogueTurnDispatched && rogueTurnHandled &&
        rogueQuitSent && rogueContinueSent &&
        rogueStatusRequested && rogueRun === 2 && rogueFirstLifecycleSeen &&
        rogueFirstStatusSeen && rogueSecondStatusSeen &&
        roguePostInputSent && roguePostInputSeen &&
        rogueCursorHiddenSeen && rogueCursorRestoredSeen &&
        exitSent && !doneBeforeExit && result.ok
    : pipelineProbe
      ? promptSeen && commandSent && vfsSeen &&
        pipelineCountSeen && pipelineRedirectDone &&
        pipelineCatSeen && pipelineAfterSeen &&
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
        wastRepeatStatusesPass &&
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
