#!/usr/bin/env node
"use strict";
// Browser capability boundary: simulated DOM/clipboard, no GUI launch.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const elements = new Map();
const element = () => ({style:{}, addEventListener(){}, focus(){}, select(){},
  showModal(){this.open=true;}, close(){this.open=false;}});
const get = key => { if (!elements.has(key)) elements.set(key,element()); return elements.get(key); };
const replies = [];
let copied = [], execOk = true, apiReject = false, release;
const requester = {postMessage:m => replies.push(m)};
const windowEvents = {};
const document = {querySelector:get, execCommand:operation => {
  assert.equal(operation,"copy");
  if (execOk) copied.push(get("#clipboard-text").value);
  return execOk;
}};
const context = vm.createContext({console, Uint8Array, TextDecoder, TextEncoder,
  URLSearchParams, location:{search:""}, window:{addEventListener(name,handler){windowEvents[name]=handler;}}, document,
  navigator:{clipboard:{writeText:async text => {
    if (apiReject) throw Error("denied");
    if (release) await new Promise(resolve => { release=resolve; });
    copied.push(text);
  }}}, WasteTerminalModel:class{}, WasteTerminalRenderer:class{}, requester});
vm.runInContext(fs.readFileSync("src/html-rt/src/app.js","utf8"),context);
let id = 0;
function request(text, bytes) {
  context.payload = bytes || new TextEncoder().encode(text);
  vm.runInContext(`worker=requester; requestHostClipboard(worker,{id:${++id},bytes:payload});`,context);
  assert.equal(get("#clipboard-dialog").open,true);
}
(async () => {
  request("café\n");
  // Dialog keys must retain normal browser behavior instead of entering Bash.
  windowEvents.keydown({target:{closest:selector => selector.includes("#clipboard-dialog")},key:"Enter"});
  assert.equal(replies.length,0);
  await get("#clipboard-copy").onclick();
  assert.deepEqual(copied,["café\n"]);
  assert.equal(replies.at(-1).ok,true);
  assert.equal(get("#clipboard-text").value,"");
  assert.equal(get("#clipboard-dialog").open,false);
  request(""); await get("#clipboard-copy").onclick();
  assert.equal(copied.at(-1),"");
  request("\ufeffBOM"); await get("#clipboard-copy").onclick();
  assert.equal(copied.at(-1),"\ufeffBOM");
  request("", new Uint8Array([0,128,255,65]));
  await get("#clipboard-copy").onclick();
  assert.equal(copied.at(-1),"\0\ufffd\ufffdA");
  request("cancel"); get("#clipboard-cancel").onclick();
  assert.equal(replies.at(-1).ok,false);
  request("escape"); let prevented=false;
  get("#clipboard-dialog").oncancel({preventDefault(){prevented=true;}});
  assert(prevented); assert.equal(replies.at(-1).ok,false);
  execOk=false; apiReject=true;
  request("retry"); const before=replies.length;
  await get("#clipboard-copy").onclick();
  assert.equal(replies.length,before);
  assert.equal(get("#clipboard-copy").disabled,false);
  apiReject=false; await get("#clipboard-copy").onclick();
  assert.equal(replies.at(-1).ok,true);
  request("stale"); const oldCount=replies.length;
  vm.runInContext("worker={postMessage(){throw Error('stale worker');}}; cancelClipboard();",context);
  assert.equal(replies.length,oldCount);
  // A late async clipboard completion cannot close or acknowledge a newer dialog.
  request("late"); release=true;
  const pending=get("#clipboard-copy").onclick();
  get("#clipboard-cancel").onclick();
  request("new"); release(); release=null; await pending;
  assert.equal(get("#clipboard-dialog").open,true);
  assert.equal(get("#clipboard-text").value,"new");
  get("#clipboard-cancel").onclick();
  console.log("PASS clipboard DOM adapter: user click, text/empty/BOM, cancel/Escape, denial/retry, stale worker and late completion");
})().catch(error => {console.error(error);process.exitCode=1;});
