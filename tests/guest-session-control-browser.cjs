#!/usr/bin/env node
"use strict";
const {treeVfs, packageVfs, stageVfs} = require("./vfs-package.cjs");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {readOfflinePackage} = require("./offline-html-package.cjs");
const wasm = fs.readFileSync(process.argv[2] || "build/html-rt/waste-wast.wasm");
const vfs = treeVfs(process.argv[3] || "src/vfs");
const fixtures = ["loop", "linked", "fork", "tail", "start", "start-negative"];
const bytes = value => value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength);
(async () => {
  let exp, clock = 0;
  const {instance} = await WebAssembly.instantiate(wasm, {waste_host: {
    strtod: () => 0, strtof: () => 0, posix_open: () => -1,
    posix_close: () => 0, posix_read: () => -1, posix_write: (_fd, _p, n) => n,
    wall_clock_ms: () => ++clock
  }});
  exp = instance.exports;
  const allocate = data => {
    const p = exp.waste_wast_alloc(data.length);
    assert(p);
    new Uint8Array(exp.memory.buffer, p, data.length).set(data);
    return p;
  };
  function run(source, timeout, cancel) {
    stageVfs(exp, vfs);
    assert.equal(exp.waste_wast_set_execution_limits(timeout, cancel), 0);
    const s = allocate(source);
    try { assert.equal(exp.waste_wast_run_script(s, source.length), 0); }
    finally { exp.waste_wast_free(s); }
  }
  for (const fixture of fixtures) {
    for (const reason of [1, 2]) {
      run(fs.readFileSync(`tests/guest-session-control-${fixture}.wast`),
        reason === 1 ? 10 : 100, reason === 2 ? 10 : 0);
      assert.equal(exp.waste_wast_execution_stop_reason(), reason, fixture);
      assert.equal(exp.waste_wast_results_passed(), 0, fixture);
      assert.equal(exp.waste_wast_results_total(), 1, fixture);
      assert.equal(exp.waste_wast_guest_exited(), 0);
      assert.equal(exp.waste_wast_guest_exit_status(), 0);
      // Reuse this Wasm instance: controls and aborted stores must not leak
      // into a later isolated session.
      run(fs.readFileSync("tests/guest-session-linked-fork.wast"), 0, 0);
      assert.equal(exp.waste_wast_execution_stop_reason(), 0);
      assert.equal(exp.waste_wast_results_passed(), 3);
    }
  }
  assert.equal(exp.waste_wast_set_execution_limits(3600001, 0), -1);
  console.log("PASS browser execution controls: 12 interrupts, expected-trap/invalid rejection, fresh-store recovery");

  if (!process.argv[4]) return;
  const page = readOfflinePackage(process.argv[4]);
  for (const fixture of ["loop", "io"]) {
    for (const reason of [1, 2]) {
      // The I/O fixture must finish three runnable assertions before it parks
      // on stdin. Give its setup a wider budget than the pure-loop probe.
      const budget = fixture === "io" ? 1000 : 100;
      let done;
      const self = {postMessage(message) { if (message.type === "done") done = message; }};
      vm.runInContext(page.read("root/waste/app/worker.js").toString(), vm.createContext({self,
        WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, Promise, Math,
        Number, String, Date, Error, setTimeout, clearTimeout, console,
        atob: value => Buffer.from(value, "base64").toString("binary")}));
      const source = fs.readFileSync(fixture === "io" ? "tests/guest-session-io.wast" :
        "tests/guest-session-control-loop.wast").toString();
      self.onmessage({data: {type:"start", wasmBytes:bytes(page.read("waste-wast.wasm")),
        source, vfs:packageVfs(page),
        executionLimits:{timeoutMs:reason === 1 ? budget : 1000,
          cancelAfterMs:reason === 2 ? budget : 0}}});
      const deadline = Date.now() + 15000;
      while (!done && Date.now() < deadline)
        await new Promise(resolve => setTimeout(resolve, 10));
      assert(done && !done.ok, fixture);
      assert.equal(done.timedOut, reason === 1, JSON.stringify(done));
      assert.equal(done.cancelled, reason === 2, JSON.stringify(done));
      assert.equal(done.exited, false);
      assert.equal(done.passed, fixture === "io" ? 3 : 0);
      assert.equal(done.total, fixture === "io" ? 4 : 1);
    }
  }
  console.log("PASS production worker controls: runnable and blocked deadline/cancellation");
})().catch(error => { console.error(error); process.exitCode = 1; });
