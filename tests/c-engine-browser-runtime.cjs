"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const stagingDir = path.join(root, "src/html-rt/src/tests");
const payloadPath = process.argv[2] || path.join(stagingDir, "payload.json");
const requestedFiles = new Set(process.argv.slice(3));

/* Load payload — accept either payload.json directly or the legacy HTML path */
let payload, engineBytes;
if (payloadPath.endsWith(".json")) {
  payload = JSON.parse(fs.readFileSync(payloadPath, "utf8"));
  const wasmPath = path.join(root, "build/html-rt/waste-wast.wasm");
  engineBytes = new Uint8Array(fs.readFileSync(wasmPath));
} else {
  /* Legacy: parse monolithic HTML */
  const html = fs.readFileSync(payloadPath, "utf8");
  const payloadMatch = html.match(/^  const PAYLOAD = (.*);$/m);
  const workerMatch = html.match(/  const WORKER_SRC = String\.raw`([\s\S]*?)`;\n\n  \/\* ---- DOM helpers/);
  if (!payloadMatch || !workerMatch) throw new Error("cannot extract generated dashboard payload");
  payload = JSON.parse(payloadMatch[1]);
  engineBytes = Uint8Array.from(Buffer.from(payload.wasmB64, "base64"));
}

/* Load worker source */
const workerSrc = fs.readFileSync(
  path.join(stagingDir, "worker.js"), "utf8"
);

(async () => {
  let failed = 0;
  const tests = requestedFiles.size ? payload.tests.filter(test =>
    requestedFiles.has(test.file) ||
    requestedFiles.has(test.path || `${test.group}/${test.file}`)
  ) : payload.tests.filter(test => !test.unsupported);
  if (requestedFiles.size && tests.length === 0)
    throw new Error("no requested C-engine browser tests found");
  for (const test of tests) {
    /* For wast-stream tests, load source text from the original location */
    let testSpec = test.spec;
    if (testSpec.mode === "wast-stream" && !testSpec.wastB64 && !testSpec.wastText) {
      const wastPath = testSpec.sourcePath
        ? path.join(root, testSpec.sourcePath)
        : path.join(stagingDir, "wast", test.file);
      testSpec = Object.assign({}, testSpec, {
        wastText: fs.readFileSync(wastPath, "utf8"),
      });
    }

    let message;
    const self = {postMessage(value) { message = value; }};
    const context = vm.createContext({
      self, WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, BigInt, Error,
      String, Number, Math, Array, Map, Promise, atob,
    });
    vm.runInContext(workerSrc, context, {filename: "c-engine-worker.js"});
    await self.onmessage({data: {wasmBytes: engineBytes, testSpec}});
    const ok = message?.type === "done" &&
      message.results.every(result => result.pass);
    console.log(`${ok ? "PASS" : "FAIL"} ${test.path || `${test.group}/${test.file}`}`);
    if (!ok) {
      failed++;
      if (message?.type === "done") {
        const failures = message.results.map((result, index) =>
          ({index, ...result})).filter(result => !result.pass);
        console.error(JSON.stringify({type: message.type, file: message.file,
          resultCount: message.results.length, failureCount: failures.length,
          failures: failures.slice(0, 12)}, null, 2));
      } else {
        console.error(JSON.stringify(message, null, 2));
      }
    }
  }
  if (failed) throw new Error(`${failed} C-engine browser test(s) failed`);
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
