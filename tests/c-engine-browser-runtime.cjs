"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const htmlPath = process.argv[2] || path.join(root, "build/c-engine/browser-tests-c-engine.html");
const html = fs.readFileSync(htmlPath, "utf8");
const pageScripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
if (pageScripts.length !== 1) throw new Error("expected exactly one generated page script");
new Function(pageScripts[0][1]);
const payloadMatch = html.match(/^  const PAYLOAD = (.*);$/m);
const workerMatch = html.match(/  const WORKER_SRC = String\.raw`([\s\S]*?)`;\n\n  \/\* ---- DOM helpers/);
if (!payloadMatch || !workerMatch) throw new Error("cannot extract generated dashboard payload");
const payload = JSON.parse(payloadMatch[1]);
const engineBytes = Uint8Array.from(Buffer.from(payload.wasmB64, "base64"));

(async () => {
  let failed = 0;
  for (const test of payload.tests) {
    let message;
    const self = {postMessage(value) { message = value; }};
    const context = vm.createContext({
      self, WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, BigInt, Error,
      String, Number, Math, Array, Map, Promise, atob,
    });
    vm.runInContext(workerMatch[1], context, {filename: "c-engine-worker.js"});
    await self.onmessage({data: {wasmBytes: engineBytes, testSpec: test.spec}});
    const ok = message?.type === "done" && message.results.length > 0 &&
      message.results.every(result => result.pass);
    console.log(`${ok ? "PASS" : "FAIL"} ${test.group}/${test.file}`);
    if (!ok) {
      failed++;
      console.error(JSON.stringify(message, null, 2));
    }
  }
  if (failed) throw new Error(`${failed} C-engine browser test(s) failed`);
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
