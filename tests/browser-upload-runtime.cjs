#!/usr/bin/env node
"use strict";
// Exercise the authored DOM adapter and worker without launching a browser.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {config} = require("./runtime-config.cjs");
let input, reader, throwRead = false;
const replies = [];
const requester = {postMessage: message => replies.push(message)};
const element = () => ({addEventListener() {}, style: {}});
const app = vm.createContext({
  console, Uint8Array, URLSearchParams, location: {search: ""},
  window: {addEventListener() {}},
  document: {
    querySelector: element,
    createElement: () => (input = {
      files: [], events: {}, click() { this.clicked = true; },
      addEventListener(name, handler) { this.events[name] = handler; },
    }),
  },
  WasteTerminalModel: class {}, WasteTerminalRenderer: class {},
  FileReader: class {
    constructor() { reader = this; }
    readAsArrayBuffer() { if (throwRead) throw Error("unreadable file"); }
  },
  requester,
});
vm.runInContext(fs.readFileSync("src/html-rt/src/app.js", "utf8"), app);
function request() {
  replies.length = 0;
  vm.runInContext("worker = requester; requestHostUpload(worker, {destPath: '/tmp/upload'});", app);
  assert.equal(input.clicked, true);
}
function select() { input.files = [{name: "test", size: 3}]; input.onchange(); }
request(); select();
reader.result = new Uint8Array([0, 128, 255]).buffer;
reader.onload(); input.events.cancel();
assert.equal(replies.length, 1);
assert.deepEqual(Array.from(replies[0].bytes), [0, 128, 255]);
for (const cancel of [
  () => input.events.cancel(),
  () => input.onchange(),
  () => { select(); reader.onerror(); },
  () => { select(); reader.onabort(); },
  () => { throwRead = true; select(); throwRead = false; },
]) {
  request(); cancel(); input.events.cancel();
  assert.equal(replies.length, 1);
  assert.equal(replies[0].cancelled, true);
}
request(); select();
vm.runInContext("worker = {postMessage() { throw Error('stale reply'); }};", app);
reader.onload();
assert.equal(replies.length, 0);

let kind = 1, failedAlloc = false, provideResult = 0, throwProvide = false;
let allocated = [], freed = [], provided = [], cancelled = 0;
const memory = {buffer: new ArrayBuffer(1024)};
const api = {
  waste_wast_host_io_kind: () => kind,
  waste_wast_alloc: length => { allocated.push(length); return failedAlloc ? 0 : 16; },
  waste_wast_free: ptr => freed.push(ptr),
  waste_wast_host_io_cancel: () => cancelled++,
  waste_wast_host_io_provide_upload: (ptr, length) => {
    provided.push(Array.from(new Uint8Array(memory.buffer, ptr, length)));
    if (throwProvide) throw Error("copy failed");
    return provideResult;
  },
};
const self = {};
const worker = vm.createContext({self, WASTE_CONFIG: config, Uint8Array, TextDecoder,
  api, memory, console});
vm.runInContext(fs.readFileSync("src/html-rt/src/worker.js", "utf8"), worker);
vm.runInContext("exp = api; engineMemory = memory; engineReady = true;", worker);
function respond(message) {
  allocated = []; freed = []; provided = []; cancelled = 0;
  vm.runInContext("ioPending = false;", worker);
  self.onmessage({data: {type: "host-upload-response", ...message}});
}
respond({bytes: new Uint8Array([0, 128, 255])});
assert.deepEqual(provided, [[0, 128, 255]]);
assert.deepEqual(freed, [16]);
assert.equal(cancelled, 0);
assert.equal(vm.runInContext("ioPending", worker), true);
respond({bytes: new Uint8Array()});
assert.deepEqual(allocated, [1]);
assert.deepEqual(provided, [[]]);
assert.deepEqual(freed, [16]);
failedAlloc = true;
respond({bytes: [1]});
assert.equal(cancelled, 1);
assert.deepEqual(freed, []);
assert.deepEqual(provided, []);
failedAlloc = false;
provideResult = -1;
respond({bytes: [1]});
assert.equal(cancelled, 1);
assert.deepEqual(freed, [16]);
throwProvide = true;
respond({bytes: [1]});
assert.equal(cancelled, 1);
assert.deepEqual(freed, [16]);
respond({cancelled: true});
assert.equal(cancelled, 1);
assert.deepEqual(allocated, []);
kind = 0;
respond({bytes: [1]});
assert.deepEqual(allocated, []);
assert.equal(cancelled, 0);
assert.equal(vm.runInContext("ioPending", worker), false);
console.log("PASS upload adapters: bytes, cancellation, read errors, stale worker, buffer ownership, empty files and allocation failures");
