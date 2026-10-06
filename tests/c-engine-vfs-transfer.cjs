#!/usr/bin/env node
"use strict";
const {packageVfs} = require("./vfs-package.cjs");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {readOfflinePackage} = require("./offline-html-package.cjs");
const page = readOfflinePackage(process.argv[2] || "build/html-rt/bash.html");
const manifest = JSON.parse(page.read("vfs-manifest.json"));
const payload = Buffer.from([0, 1, 127, 128, 255, 10, 65, 0]);
let transcript = "", done, ready = 0, uploads = 0, downloads = [];
const self = {postMessage(message) {
  if (message.type === "output") transcript += message.text;
  if (message.type === "io-ready" || message.type === "started") ready++;
  if (message.type === "done") done = message;
  if (message.type === "host-download") downloads.push(message);
  if (message.type === "host-upload-request") {
    uploads++;
    setTimeout(() => self.onmessage({data: uploads === 1 ?
      {type:"host-upload-response", bytes:payload} :
      {type:"host-upload-response", cancelled:true}}), 25);
  }
}};
const context = vm.createContext({self, WebAssembly, Uint8Array, DataView,
  TextDecoder, TextEncoder, Promise, Math, Number, String, Date, Error,
  setTimeout, clearTimeout, console, atob:s => Buffer.from(s, "base64").toString("binary")});
vm.runInContext(page.read("root/waste/app/worker.js").toString(), context);
const buffer = b => b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
const wait = async test => {
  const deadline = Date.now() + 15000;
  while (!test()) {
    if (done || Date.now() > deadline) throw Error(JSON.stringify(done) + "\n" + transcript);
    await new Promise(resolve => setTimeout(resolve, 10));
  }
};
async function command(source, tag, status) {
  const offset = transcript.length, previous = ready;
  self.onmessage({data:{type:"input", bytes:new TextEncoder().encode(
    source + `; printf '${tag}_%s__\\n' "$?"\n`)}});
  await wait(() => ready > previous && transcript.slice(offset).includes(`${tag}_${status}__`) &&
    /bash-[^\r\n]*[#$] ?/.test(transcript.slice(transcript.lastIndexOf(`${tag}_${status}__`))));
}
(async () => {
  self.onmessage({data:{type:"start", wasmBytes:buffer(page.read("waste-wast.wasm")),
    source:page.read("launch.wast").toString(), vfs:packageVfs(page),
    vfsPaths:manifest.entries.map(e => e.path)}});
  await wait(() => ready && /bash-[^\r\n]*[#$] ?/.test(transcript));
  await command("/bin/upload /tmp/roundtrip.bin", "__UPLOAD", 0);
  await command("/usr/bin/download /tmp/roundtrip.bin", "__DOWNLOAD", 0);
  assert.equal(downloads.length, 1);
  assert.equal(downloads[0].name, "roundtrip.bin");
  assert.deepEqual(Buffer.from(downloads[0].bytes), payload);
  await command("/usr/bin/upload /tmp/cancelled.bin", "__CANCEL", 1);
  await command("/bin/download /tmp/cancelled.bin", "__MISSING", 1);
  assert.equal(downloads.length, 1);
  await command("echo __TRANSFER_AFTER__", "__AFTER", 0);
  self.onmessage({data:{type:"input", bytes:new TextEncoder().encode("exit\n")}});
  await wait(() => done);
  assert.equal(done.ok, true);
  assert.equal(uploads, 2);
  console.log("PASS installed upload/download: binary roundtrip, cancellation, absent file, subsequent input and exit");
})().catch(e => { console.error(e); self.onmessage({data:{type:"stop"}}); process.exitCode = 1; });
