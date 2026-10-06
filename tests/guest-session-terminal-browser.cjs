#!/usr/bin/env node
"use strict";
const {packageVfs, stageVfs} = require("./vfs-package.cjs");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {readOfflinePackage} = require("./offline-html-package.cjs");
const page = readOfflinePackage(process.argv[2]);
const scenario = JSON.parse(fs.readFileSync("tests/guest-session-terminal-control.json"));
const buffer = bytes => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
const invalid = [
  ...[0, -1, 129, 2 ** 32 + 12, 12.5, "12", null].map(signal => ({type:"signal", signal})),
  ...[0, -1, 65536, 2 ** 32 + 120, 120.5, "120", null].map(columns =>
    ({type:"resize", columns, rows:40})),
  {type:"resize", columns:120, rows:0}, {type:"resize", columns:120, rows:65536},
  {type:"resize", columns:120, rows:40.5},
  ...[-1, 2 ** 32, 1.5, "1", null].map(low => ({type:"advance-clock", low, high:0})),
  ...[-1, 2 ** 32, 1.5, "1", null].map(high => ({type:"advance-clock", low:1, high})),
  {type:"advance-clock", low:1, high:0} // no frozen-clock capability
];
let done, ready = 0, output = "", errors = 0, selectedPid, waitKind;
const self = {postMessage(message) {
  if (message.type === "done") done = message;
  if (message.type === "control-error") errors++;
  if (message.type === "started" || message.type === "io-ready") {
    ready++; selectedPid = message.pid; waitKind = message.waitKind;
  }
  if (message.type === "output" && !message.text.startsWith("WASTE_")) output += message.text;
}};
vm.runInContext(page.read("waste/app/worker.js").toString(), vm.createContext({self,
  WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, Promise, Math,
  Number, String, Date, Error, setTimeout, clearTimeout, console,
  atob: value => Buffer.from(value, "base64").toString("binary")}));
const send = message => self.onmessage({data:message});
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function wait(predicate) {
  const deadline = Date.now() + 15000;
  while (!predicate()) {
    assert(!done && Date.now() < deadline, "worker did not reach expected boundary");
    await sleep(10);
  }
}
(async () => {
  let exp;
  ({instance: {exports: exp}} = await WebAssembly.instantiate(page.read("waste-wast.wasm"),
    {waste_host: {strtod: () => 0, strtof: () => 0, posix_open: () => -1,
      posix_close: () => 0, posix_read: () => -1, posix_write: (_fd, _p, n) => n,
      wall_clock_ms: () => Date.now()}}));
  const allocate = bytes => {
    const p = exp.waste_wast_alloc(bytes.length);
    assert(p);
    new Uint8Array(exp.memory.buffer, p, bytes.length).set(bytes);
    return p;
  };
  const stage = () => {
    stageVfs(exp, packageVfs(page));
    exp.waste_wast_enable_terminal();
  };
  stage();
  const source = fs.readFileSync(scenario.fixture), sourcePtr = allocate(source);
  assert.equal(exp.waste_wast_run_script(sourcePtr, source.length), 1);
  assert.equal(exp.waste_wast_advance_clock_monotonic_ns(1, 0), -1,
    "unconfigured guest accepted a clock event");
  for (const [columns, rows] of [[0, 40], [120, 0], [65536, 40], [120, 65536], [0xffffffff, 40]])
    assert.equal(exp.waste_wast_resize_terminal(columns, rows), -22);
  for (const signal of [0, 129, 0xffffffff])
    assert.equal(exp.waste_wast_raise_signal(signal), -22);
  for (const event of scenario.events) {
    assert.equal(exp.waste_wast_wait_kind(), event.waitKind);
    assert.equal(exp.waste_wast_process_id(), event.pid);
    if (event.resize) assert.equal(exp.waste_wast_resize_terminal(...event.resize), 0);
    else if (event.signal !== undefined) assert.equal(exp.waste_wast_raise_signal(event.signal), 0);
    else assert.equal(exp.waste_wast_enqueue_input(allocate(Buffer.from(event.bytes)), event.bytes.length), 0);
    assert.equal(exp.waste_wast_resume(), event === scenario.events.at(-1) ? 0 : 1);
  }
  exp.waste_wast_free(sourcePtr);
  assert.equal(exp.waste_wast_results_passed(), 9);
  assert.equal(exp.waste_wast_raise_signal(12), -22, "completed store accepted a signal");
  stage();
  const fresh = fs.readFileSync("tests/guest-session-linked-fork.wast"), freshPtr = allocate(fresh);
  assert.equal(exp.waste_wast_run_script(freshPtr, fresh.length), 0);
  exp.waste_wast_free(freshPtr);
  assert.equal(exp.waste_wast_results_passed(), 3);
  console.log("PASS browser terminal exports: 9 invalid/unavailable values rejected, 9 control assertions, same-instance fresh-store recovery");
  // Exercise validation and the bounded pending queue before instantiation.
  for (const message of invalid) send(message);
  for (let i = 0; i < 17; i++) send({type:"signal", signal:10});
  send({type:"resize", columns:120, rows:40});
  assert.equal(errors, invalid.length + 1);
  send({type:"start", wasmBytes:buffer(page.read("waste-wast.wasm")),
    source:fs.readFileSync(scenario.fixture, "utf8"),
    vfs:packageVfs(page)});
  // Queued resize must wake the first SELECT, without any terminal input.
  await wait(() => ready >= 2 && output.includes("SAME\n"));
  const beforeOutput = output;
  for (const message of invalid) send(message);
  await sleep(30);
  // SELECT's ordinary timer polls also publish io-ready. Rejection must leave
  // the guest at the same wait without output or completion progress.
  assert.equal(output, beforeOutput, "invalid controls advanced the guest");
  assert.equal(done, undefined);
  assert.equal(selectedPid, 1);
  assert.equal(waitKind, 2);
  assert.equal(errors, 2 * invalid.length + 1);
  let previous = 1, consumed = "RESIZE\n".length;
  for (const event of scenario.events.slice(1)) {
    await wait(() => ready > previous && output.indexOf(event.after || "", consumed) >= 0);
    previous = ready; consumed = output.length;
    await sleep(30);
    if (event.resize) send({type:"resize", columns:event.resize[0], rows:event.resize[1]});
    else if (event.signal !== undefined) send({type:"signal", signal:event.signal});
    else send({type:"input", bytes:event.bytes});
  }
  await wait(() => done);
  assert(done.ok && done.exited && !done.timedOut && !done.cancelled);
  assert.equal(done.passed, 9); assert.equal(done.total, 9);
  assert.equal(done.exitStatus, 0); assert.equal(output, scenario.output);
  console.log(`PASS worker terminal controls: queued resize wakeup, bounded signals, ${errors} invalid/overflow rejections, 9 assertions`);
})().catch(error => {
  console.error(error); send({type:"stop"}); process.exitCode = 1;
});
