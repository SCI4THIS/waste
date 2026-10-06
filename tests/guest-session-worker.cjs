#!/usr/bin/env node
"use strict";
const {treeVfs, packageVfs, stageVfs} = require("./vfs-package.cjs");
// The production worker consumes exactly the native/export-probe contract.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {createHash} = require("node:crypto");
const {Worker} = require("node:worker_threads");
const {readOfflinePackage} = require("./offline-html-package.cjs");
const page = readOfflinePackage(process.argv[2]);
const scenario = JSON.parse(fs.readFileSync(process.argv[3]));
const manifest = JSON.parse(page.read("vfs-manifest.json"));
if (scenario.fixture === "src/vfs/usr/share/waste/launch.wast")
  assert.deepEqual(page.read("launch.wast"), fs.readFileSync(scenario.fixture));
const vfs = packageVfs(page);
const sha256 = bytes => createHash("sha256").update(bytes).digest("hex");
const buffer = bytes => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
let output = "", done, ready = 0, pid, waitKind, diagnosticResults, controlFailure;
let expectedControlErrors = 0;
const suiteContext = vm.createContext({setTimeout, clearTimeout, performance,
  TextDecoder, TextEncoder, Uint8Array, DataView});
const suiteSource = page.read("root/waste/app/test-suite.js").toString();
assert.equal(suiteSource, fs.readFileSync("src/html-rt/src/test-suite.js", "utf8"),
  "package contains the production suite controller");
vm.runInContext(suiteSource, suiteContext);
const workerSource = page.read("root/waste/app/worker.js").toString();
const suite = new suiteContext.WasteTestSuite({wasmBytes:page.read("waste-wast.wasm"),
  vfs, expectedFailures:fs.readFileSync("tests/browser-corpus-expected-failures.txt", "utf8"),
  createWorker() {
    const worker = new Worker(`const {parentPort} = require('node:worker_threads');
      global.self = {postMessage: data => parentPort.postMessage(data)};
      parentPort.on('message', data => self.onmessage({data}));
${workerSource}`, {eval:true});
    const adapter = {postMessage:data => worker.postMessage(data), terminate:() => worker.terminate()};
    worker.on("message", data => adapter.onmessage?.({data}));
    worker.on("error", error => adapter.onerror?.({message:error.message}));
    return adapter;
  }});
const self = {postMessage(message) {
  if (message.type === "control-error") {
    if (expectedControlErrors && message.operation === "advance-clock") expectedControlErrors--;
    else controlFailure = message;
  }
  if (message.type === "guest-suite-request") {
    suite.guestCommand(message.request).then(bytes => self.onmessage({data:{
      type:"guest-suite-response", id:message.id, bytes}}));
  }
  if (message.type === "guest-suite-cancel") suite.cancel();
  if (message.type === "output") {
    if (message.text.startsWith("WASTE_DONE_RESULTS="))
      diagnosticResults = JSON.parse(message.text.slice("WASTE_DONE_RESULTS=".length));
    else if (!message.text.startsWith("WASTE_TRANSITION_EVIDENCE="))
      output += message.text;
  }
  if (message.type === "started" || message.type === "io-ready") {
    ready++; pid = message.pid; waitKind = message.waitKind;
  }
  if (message.type === "done") done = message;
}};
vm.runInContext(page.read("root/waste/app/worker.js").toString(), vm.createContext({self,
  WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, Promise, Math,
  Number, String, Date, Error, setTimeout, clearTimeout, console,
  atob: value => Buffer.from(value, "base64").toString("binary")}));
const wait = async predicate => {
  const deadline = Date.now() + 60000;
  while (!predicate()) {
    if (done || controlFailure || Date.now() >= deadline)
      throw Error(JSON.stringify(controlFailure || done) + "\n" + output);
    await new Promise(resolve => setTimeout(resolve, 10));
  }
};
(async () => {
  if (scenario.clockRealtimeNs !== undefined || scenario.clockMonotonicNs !== undefined) {
    const low = v => Number(BigInt(v) & 0xffffffffn) >>> 0;
    const high = v => Number(BigInt(v) >> 32n) >>> 0;
    const rt = scenario.clockRealtimeNs ?? 0, mt = scenario.clockMonotonicNs ?? 0;
    self.onmessage({data:{type:"set-clock", realtimeLow:low(rt), realtimeHigh:high(rt),
      monotonicLow:low(mt), monotonicHigh:high(mt)}});
  }
  self.onmessage({data:{type:"start", wasmBytes:buffer(page.read("waste-wast.wasm")),
    source:fs.readFileSync(scenario.fixture, "utf8"), vfs,
    vfsFiles:(scenario.files || []).map(file => ({path:file.path, mode:file.mode,
      bytes:buffer(fs.readFileSync(file.source)), mtimeSec:0, mtimeNsec:0})),
    vfsPaths:manifest.entries.map(entry => entry.path)}});
  let previous = 0, consumed = 0;
  for (const event of scenario.events) {
    await wait(() => ready > previous && output.indexOf(event.after || "", consumed) >= 0);
    assert((event.duringBatch ? [5] : [1, 2]).includes(waitKind));
    if (event.pid !== undefined) assert.equal(pid, event.pid);
    if (event.waitKind !== undefined) assert.equal(waitKind, event.waitKind);
    previous = ready; consumed = output.length;
    await new Promise(resolve => setTimeout(resolve, 30));
    for (const rejected of event.rejectMonotonicNs || []) {
      const value = BigInt(rejected);
      expectedControlErrors++;
      self.onmessage({data:{type:"advance-clock", low:Number(value & 0xffffffffn),
        high:Number(value >> 32n)}});
      assert.equal(expectedControlErrors, 0, "worker accepted an invalid clock event");
    }
    assert(!event.eof, "Worker EOF is not part of the interactive Bash contract");
    const beforeOutput = output;
    if (event.monotonicNs !== undefined) {
      const value = BigInt(event.monotonicNs);
      self.onmessage({data:{type:"advance-clock", low:Number(value & 0xffffffffn),
        high:Number(value >> 32n)}});
    }
    else if (event.resize)
      self.onmessage({data:{type:"resize", columns:event.resize[0], rows:event.resize[1]}});
    else if (event.signal !== undefined)
      self.onmessage({data:{type:"signal", signal:event.signal,
        ...(event.signalPid !== undefined ? {pid: event.signalPid} : {}),
        ...(event.signalPgid !== undefined ? {pgid: event.signalPgid} : {})}});
    else self.onmessage({data:{type:"input", bytes:event.text === undefined ?
      event.bytes : Array.from(new TextEncoder().encode(event.text))}});
    if (event.stillWaiting) {
      await wait(() => ready > previous);
      assert.equal(output, beforeOutput, "guest advanced before its wait was satisfied");
      assert.equal(waitKind, event.waitKind);
    }
  }
  await wait(() => done);
  assert.equal(done.ok, (scenario.expectedPassed ?? scenario.assertions) === scenario.assertions, JSON.stringify(done));
  assert.equal(done.exited, scenario.exited ?? true, JSON.stringify(done));
  assert.equal(done.total, scenario.assertions);
  assert.equal(done.passed, scenario.expectedPassed ?? done.total);
  if (!done.ok) {
    // This is a separately posted worker diagnostic, not guest transcript
    // normalization. Check it and retain it beside the unmodified output.
    // done.results comes from the worker VM's separate JavaScript realm.
    assert.equal(JSON.stringify(diagnosticResults), JSON.stringify(done.results));
    assert.equal(done.results.filter(result => !result.pass).length, done.total - done.passed);
    if (scenario.expectedError)
      assert(done.results.some(result => !result.pass && result.error === scenario.expectedError));
  }
  assert.equal(done.exitStatus, scenario.exitStatus);
  if (scenario.output !== undefined) assert.equal(output, scenario.output);
  for (const marker of scenario.contains || []) assert(output.includes(marker), output);
  console.log(JSON.stringify({runtime:"production-worker-session", passed:done.passed,
    total:done.total, exitStatus:done.exitStatus,
    results:done.results,
    manifestSha256:sha256(Buffer.from(JSON.stringify(manifest))), output,
    ...(diagnosticResults ? {diagnosticResults} : {})}));
})().catch(error => {
  console.error(error); self.onmessage({data:{type:"stop"}}); process.exitCode = 1;
});
