#!/usr/bin/env node
"use strict";
// Transport only: execute installed clipboard in production Bash/worker,
// return captured payloads through upload; WAST owns statuses/byte assertions.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {readOfflinePackage} = require("../../../html-rt/test/offline-html-package.cjs");
const {packageVfs} = require("../../../html-rt/test/vfs-package.cjs");
const page = readOfflinePackage(process.argv[2] || "build/html-rt/bash.html");
const manifest = JSON.parse(page.read("vfs-manifest.json"));
const buffer = b => b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
let output = "", done, ready = false, requests = 0;
const captures = [];
const self = {postMessage(message) {
  if (message.type === "output") output += message.text;
  if (message.type === "io-ready") ready = true;
  if (message.type === "done") done = message;
  if (message.type === "host-clipboard-request") {
    const id = message.id;
    requests++;
    if (requests <= 8) captures.push(new Uint8Array(message.bytes));
    // Wrong IDs must not acknowledge an active request. Actual reply is delayed.
    self.onmessage({data:{type:"host-clipboard-response", id:id + 1, ok:true}});
    setTimeout(() => {
      self.onmessage({data:{type:"host-clipboard-response", id, ok:requests <= 8}});
      self.onmessage({data:{type:"host-clipboard-response", id, ok:true}});
    }, 10);
  }
  if (message.type === "host-upload-request") {
    const bytes = captures.shift();
    assert(bytes, "upload without a clipboard capture");
    setTimeout(() => self.onmessage({data:{type:"host-upload-response", bytes}}), 10);
  }
}};
vm.runInContext(page.read("root/app/worker.js").toString(), vm.createContext({self,
  WebAssembly, Uint8Array, DataView, TextDecoder, TextEncoder, Promise, Math,
  Number, String, Date, Error, setTimeout, clearTimeout, console,
  atob:s => Buffer.from(s,"base64").toString("binary")}));
const wait = async predicate => {
  const deadline = Date.now() + 60000;
  while (!predicate()) {
    if (done || Date.now() > deadline) throw Error(JSON.stringify(done) + "\n" + output);
    await new Promise(resolve => setTimeout(resolve, 10));
  }
};
(async () => {
  self.onmessage({data:{type:"start", wasmBytes:buffer(page.read("waste-wast.wasm")),
    source:page.read("launch.wast").toString(), vfs:packageVfs(page),
    vfsPaths:manifest.entries.map(e => e.path),
    vfsFiles:[
      ["/tmp/clipboard-setup.sh", "browser/setup.sh"],
      ["/tmp/clipboard-installed.wast", "installed.wast"],
      ["/tmp/clipboard-copy.wast", "browser/copy.wast"],
    ].map(([path, name]) => ({path, mode:0o644,
      bytes:buffer(fs.readFileSync("src/aux/clipboard/test/system/" + name))}))}});
  await wait(() => ready && /# /.test(output));
  self.onmessage({data:{type:"input", bytes:new TextEncoder().encode(
    'status=0; . /tmp/clipboard-setup.sh; /bin/wast --verbose /tmp/clipboard-installed.wast || status=1; /bin/wast --verbose /tmp/clipboard-copy.wast || status=1; exit "$status"\n')}});
  await wait(() => done);
  assert.equal(done.ok, true, output);
  assert.equal(done.passed, 38, output);
  assert.equal(done.total, 38, output);
  assert.equal(requests, 9);
  assert.equal(captures.length, 0);
  console.log(JSON.stringify({runtime:"production-worker-session", passed:done.passed,
    total:done.total, clipboardRequests:requests, output}, null, 2));
})().catch(error => {
  console.error(error);
  self.onmessage({data:{type:"stop"}});
  process.exitCode = 1;
});
