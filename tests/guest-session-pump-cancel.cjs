#!/usr/bin/env node
"use strict";
const {config, withConfig} = require("./runtime-config.cjs");
const {treeVfs} = require("./vfs-package.cjs");
// Pump + external cancellation contract through the production worker.js.
// Boots the shared worker in a VM, starts a spin fixture with a short
// pump quantum, posts a `cancel` message after seeing the wake marker,
// and asserts the worker reports a cancelled, non-ok result.  Closes the
// "immediate external cancellation during a running worker" gate that
// pure scheduled cancellation leaves open.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const wasmPath = process.argv[2] || "build/html-rt/waste-wast.wasm";
const vfsRoot = process.argv[3] || "src/vfs";
const workerPath = process.argv[4] || "src/html-rt/src/worker.js";
const fixturePath = "tests/guest-session-pump-cancel.wast";

const buffer = bytes => bytes.buffer.slice(bytes.byteOffset,
                                           bytes.byteOffset + bytes.byteLength);

let output = "", done;
const self = {postMessage(message) {
  if (message.type === "output") {
    if (!message.text.startsWith("WASTE_TRANSITION_EVIDENCE=") &&
        !message.text.startsWith("WASTE_DONE_RESULTS="))
      output += message.text;
  } else if (message.type === "done") {
    done = message;
  }
}};

vm.runInContext(withConfig(fs.readFileSync(workerPath, "utf8")),  vm.createContext({self,
  WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, Promise, Math,
  Number, String, Date, Error, setTimeout, clearTimeout, console,
  atob: value => Buffer.from(value, "base64").toString("binary")}));

const wait = async predicate => {
  const deadline = Date.now() + 15000;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error("timed out: output=" +
      JSON.stringify(output) + " done=" + JSON.stringify(done));
    await new Promise(resolve => setTimeout(resolve, 5));
  }
};

(async () => {
  self.onmessage({data: {type: "start",
    wasmBytes: buffer(fs.readFileSync(wasmPath)),
    source: fs.readFileSync(fixturePath, "utf8"),
    vfs: treeVfs(vfsRoot),
    vfsFiles: [], vfsPaths: [],
    executionLimits: {pumpQuantumMs: 10}}});
  await wait(() => output.includes("P"));
  const beforeCancel = Date.now();
  self.onmessage({data: {type: "cancel"}});
  await wait(() => done);
  const elapsed = Date.now() - beforeCancel;
  assert.equal(done.ok, false, "worker reported ok after cancel: " +
    JSON.stringify(done));
  assert.equal(done.cancelled, true, "worker did not flag cancellation: " +
    JSON.stringify(done));
  assert(elapsed < 2000, "cancel took too long: " + elapsed + "ms; " +
    "pump handshake not delivering onmessage");
  assert(!done.exited, "guest should not have exited cleanly after cancel: " +
    JSON.stringify(done));
  console.log(JSON.stringify({runtime: "production-worker-pump-cancel",
    passed: done.passed, total: done.total, cancelled: done.cancelled,
    ok: done.ok, elapsedMs: elapsed, output}));
})().catch(error => {
  console.error(error);
  self.onmessage({data: {type: "stop"}});
  process.exitCode = 1;
});
