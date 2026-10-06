#!/usr/bin/env node
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const {spawnSync} = require("node:child_process");
const {readOfflinePackage} = require("./offline-html-package.cjs");

const root = path.resolve(__dirname, "..");
const frontend = path.join(root, "src/html-rt/src");
const configuredLog = process.env.WASTE_FRONTEND_PACKAGING_LOG;
const logPath = configuredLog
  ? path.resolve(root, configuredLog)
  : path.join(root, "build/html-rt/frontend-packaging.log");
fs.mkdirSync(path.dirname(logPath), {recursive:true});
if (!fs.existsSync(logPath)) fs.writeFileSync(logPath, "", "utf8");
function logStep(message) {
  fs.appendFileSync(logPath,
    `${new Date().toISOString()} pid=${process.pid} ${message}\n`, "utf8");
}
function describeError(error) {
  return error && error.stack ? error.stack : String(error);
}
function assertBytesEqual(actual, expected, label) {
  assert.equal(actual.length, expected.length,
    `${label}: length ${actual.length} != ${expected.length}`);
  assert.equal(actual.equals(expected), true,
    `${label}: byte contents differ (${actual.length} bytes)`);
}
process.on("uncaughtExceptionMonitor", (error, origin) => {
  logStep(`UNCAUGHT ${origin}: ${describeError(error)}`);
});
process.on("exit", code => logStep(`EXIT code=${code}`));

logStep(`START frontend-packaging argv=${JSON.stringify(process.argv.slice(2))}`);
const context = vm.createContext({});
logStep("STEP load authored frontend loader");
vm.runInContext(fs.readFileSync(path.join(frontend, "loader.js"), "utf8"), context);
for (const [name, expected] of [
  ["launch.wast", "../../../build/html-rt/bash-runtime.wast"],
  ["payload.json", "../../../build/html-rt/tests/payload.json"],
  ["waste-wast.wasm", "../../../build/html-rt/waste-wast.wasm"],
  ["vfs-manifest.json", "../../vfs/.inventory.json"],
  ["browser-corpus-expected-failures.txt", "../../../tests/browser-corpus-expected-failures.txt"],
  ["../../../tests/example.wast", "../../../tests/example.wast"],
]) {
  logStep(`CHECK staging data path ${name}`);
  assert.equal(context.stagingDataPath(name), expected);
}
logStep("CHECK tarball.js resolves to the vendored tarballjs source");
assert.equal(fs.realpathSync(path.join(frontend, "tarball.js")),
  fs.realpathSync(path.join(root, "submodules/tarballjs/tarball.js")));
for (const old of ["tests/index.html", "tests/payload.json", "tests/waste-wast.wasm",
  "shared/loader.js", "bash/index.html", "bash/worker.js", "bash/launch.wast",
  "bash/waste-wast.wasm", "bash/terminal/model.js"]) {
  logStep(`CHECK obsolete staged source is absent: ${old}`);
  assert(!fs.existsSync(path.join(frontend, old)), `obsolete source staging: ${old}`);
}

const filename = process.argv[2] || "build/html-rt/bash.html";
logStep(`PAGE BEGIN target=bash file=${filename}`);
const archive = readOfflinePackage(path.resolve(root, filename));
logStep("PAGE ASSERT no external script/stylesheet target=bash");
assert(!/<script\b[^>]*\bsrc\s*=|<link\b[^>]*\brel\s*=\s*["']stylesheet/i.test(archive.html));
logStep("PAGE ASSERT staging disabled target=bash");
assert(archive.html.includes("is_staging: false"));
logStep("BASH ASSERT app sources are not inlined into HTML");
for (const name of ["app.js", "worker.js", "test-suite.js", "style.css",
  "terminal/model.js", "terminal/glf.js", "terminal/renderer.js"]) {
  assert(!archive.html.includes(fs.readFileSync(path.join(frontend, name), "utf8")));
  assertBytesEqual(archive.read("waste/app/" + name),
    fs.readFileSync(path.join(frontend, name)), `bash VFS app ${name}`);
}
logStep("PAGE ASSERT engine bytes target=bash file=waste-wast.wasm");
assertBytesEqual(archive.read("waste-wast.wasm"),
  fs.readFileSync(path.join(root, "build/html-rt/waste-wast.wasm")),
  "bash embedded waste-wast.wasm");
logStep("BASH CHECK embedded runtime, VFS manifest and corpus metadata");
assert(archive.html.includes("await loadWebappFiles()"));
assertBytesEqual(archive.read("browser-corpus-expected-failures.txt"),
  fs.readFileSync(path.join(root, "tests/browser-corpus-expected-failures.txt")),
  "bash embedded browser-corpus-expected-failures.txt");
assertBytesEqual(archive.read("launch.wast"),
  fs.readFileSync(path.join(root, "build/html-rt/bash-runtime.wast")),
  "bash embedded launch.wast");
const manifest = JSON.parse(archive.read("vfs-manifest.json"));
assert.deepEqual(manifest, JSON.parse(fs.readFileSync(path.join(root, "src/vfs/.inventory.json"))));
assert(!archive.files.has("vfs-image.bin"), "retired duplicate VFS bundle");
for (const e of manifest.entries) {
  if (e.role === "directory" || e.role === "interpreter") continue;
  logStep(`BASH CHECK embedded VFS bytes ${e.path}`);
  assertBytesEqual(archive.read(e.path.slice(1)),
    fs.readFileSync(path.join(root, "src/vfs", e.path.slice(1))),
    `bash embedded VFS ${e.path}`);
  assert(!archive.files.has(path.basename(e.path) + ".wasm"), "obsolete flat binary");
}
assert(!manifest.entries.some(e => e.path.includes("waste-wast.wasm") || e.path === "/worker.js"));
assert(archive.html.includes("Loading zlib-wasm"));
assert(archive.html.includes("WebAssembly.compile(zlibBytes)"));
logStep("PAGE PASS target=bash");
console.log("PASS bash: authored sources, embedded bytes, offline references and staging paths");

const payload = JSON.parse(fs.readFileSync(path.join(root, "build/html-rt/tests/payload.json"), "utf8"));
assert.equal(payload.tests.length, 296);
assert.equal(payload.tests.filter(test => !test.unsupported).length, 292);
assert.equal(new Set(payload.tests.map(test => test.path)).size, 296);
for (const test of payload.tests) {
  if (!test.spec.sourcePath) continue;
  logStep(`WORKER CORPUS CHECK ${test.path}`);
  const bytes = fs.readFileSync(path.resolve(root, test.spec.sourcePath));
  assert.equal(bytes.length, test.spec.sourceBytes, `stale length: ${test.path}`);
}
assert(fs.existsSync(path.join(frontend, "tests-worker.js")), "Node worker conformance harness remains available");

for (const [script, args] of [
  ["generate-c-engine-tests.py", ["--tests", "tests/diy-posix-test"]],
  ["generate-c-engine-bash-html.py", ["--launch", "build/html-rt/bash-runtime.wast"]],
]) {
  logStep(`GENERATOR GUARD BEGIN script=${script} args=${JSON.stringify(args)}`);
  const result = spawnSync("python3", ["src/html-rt/tools/" + script,
    "--wasm", "build/html-rt/waste-wast.wasm", ...args,
    "--output-dir", "src/html-rt/src"], {cwd: root, encoding: "utf8"});
  assert.equal(result.status, 2);
  assert.match(result.stderr, /--output-dir must be under repository build\//);
  logStep(`GENERATOR GUARD PASS script=${script} status=${result.status}`);
}
console.log("PASS generators: authored frontend cannot be overwritten by staging mode");

const scratch = fs.mkdtempSync(path.join(root, "build/html-rt/package-negative-"));
try {
  const output = path.join(scratch, "preserved.html");
  fs.writeFileSync(output, "existing page");
  const result = spawnSync("bash", ["src/html-rt/tools/build.sh", "tests",
    "--output", output], {cwd:root, encoding:"utf8"});
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /only 'bash' is supported; tests run from bash.html/);
  assert.equal(fs.readFileSync(output, "utf8"), "existing page");
} finally {
  logStep("PACKAGE GUARD CLEANUP scratch directory");
  fs.rmSync(scratch, {recursive: true, force: true});
}
console.log("PASS package guards: retired test-page target cannot replace an existing page");
logStep("PASS frontend-packaging all checks complete");
