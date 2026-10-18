#!/usr/bin/env node
"use strict";
// Exercise automatic clipboard capability/lifecycle without a browser GUI.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const replies = [], fields = [], copied = [];
let apiMode = "success", execOk = true, resolveWrite, rejectWrite, focusRestored = 0;
const originalFocus = {focus(){focusRestored++;}};
const element = () => ({style:{},addEventListener(){},focus(){},setAttribute(){},select(){}});
const requester = {postMessage:m => replies.push(m)};
const document = {
  activeElement:originalFocus,
  querySelector:element,
  body:{appendChild(field){field.attached=true;}},
  createElement(tag){
    assert.equal(tag,"textarea");
    const field={...element(),remove(){this.removed=true;}};
    fields.push(field);return field;
  },
  execCommand(operation){
    assert.equal(operation,"copy");
    const field=fields.at(-1);
    assert(field.attached);
    assert.equal(field.className,"clipboard-buffer");
    assert.equal(field.readOnly,true);
    if(execOk)copied.push(field.value);
    return execOk;
  },
};
const clipboard={writeText(text){
  if(apiMode==="reject")return Promise.reject(Error("denied"));
  if(apiMode==="pending")return new Promise((resolve,reject)=>{
    resolveWrite=()=>{copied.push(text);resolve();}; rejectWrite=reject;
  });
  copied.push(text);return Promise.resolve();
}};
const context=vm.createContext({console,Uint8Array,TextDecoder,TextEncoder,
  URLSearchParams,location:{search:""},window:{addEventListener(){}},document,
  navigator:{clipboard},WasteTerminalModel:class{},WasteTerminalRenderer:class{},requester});
vm.runInContext(fs.readFileSync("src/html-rt/src/app.js","utf8"),context);
let id=0;
function request(text,bytes){
  context.payload=bytes || new TextEncoder().encode(text);
  return vm.runInContext(`worker=requester; requestHostClipboard(worker,{id:${++id},bytes:payload});`,context);
}
(async()=>{
  await request("café\n");
  assert.equal(copied.at(-1),"café\n");assert.equal(replies.at(-1).ok,true);
  assert.equal(fields.length,0); // Modern API must not create any UI.
  for(const text of ["","\ufeffBOM"]){await request(text);assert.equal(copied.at(-1),text);}
  await request("",new Uint8Array([0,128,255,65]));
  assert.equal(copied.at(-1),"\0\ufffd\ufffdA");
  apiMode="reject";
  await request("offline fallback");
  assert.equal(copied.at(-1),"offline fallback");assert.equal(replies.at(-1).ok,true);
  assert.equal(fields.at(-1).removed,true);assert.equal(fields.at(-1).value,"");
  assert.equal(focusRestored,1);
  context.navigator.clipboard=undefined;
  await request("");assert.equal(replies.at(-1).ok,true);
  context.navigator.clipboard=clipboard;
  execOk=false;
  await request("denied");assert.equal(replies.at(-1).ok,false);
  assert.equal(fields.at(-1).removed,true);assert.equal(fields.at(-1).value,"");
  document.execCommand=()=>{throw Error("unsupported");};
  await request("unsupported");assert.equal(replies.at(-1).ok,false);
  apiMode="pending";
  const old=request("stale");const oldCount=replies.length;
  vm.runInContext("worker={postMessage(){throw Error('stale worker');}}; cancelClipboard();",context);
  resolveWrite();await old;assert.equal(replies.length,oldCount);
  const cancelled=request("cancelled");
  vm.runInContext("cancelClipboard();",context);
  assert.equal(replies.at(-1).ok,false);
  const count=replies.length;rejectWrite(Error("denied"));await cancelled;
  assert.equal(replies.length,count);
  // Late completion of a cancelled operation must not clear a newer request.
  const first=request("first");const releaseFirst=resolveWrite;
  const second=request("second");const releaseSecond=resolveWrite;
  releaseFirst();await first;
  assert.equal(vm.runInContext("typeof cancelClipboard",context),"function");
  releaseSecond();await second;assert.equal(replies.at(-1).ok,true);
  console.log("PASS automatic clipboard: API, off-screen fallback, empty/UTF-8/BOM, failure, focus/cleanup and stale replies");
})().catch(error=>{console.error(error);process.exitCode=1;});
