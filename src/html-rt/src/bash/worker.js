"use strict";

function decodeB64(text) {
  const raw = atob(text), bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
  return bytes;
}

function watFloat(text, asF32) {
  return asF32 ? Math.fround(Number(text)) : Number(text);
}

let exp = null;
let engineMemory = null;
let inputQueue = [];
let pendingSignal = -1;
let ioResolve = null;
let terminated = false;

function waitForIO() {
  return new Promise(resolve => { ioResolve = resolve; });
}

const decoder = new TextDecoder();

function posixRead(fd, ptr, count) {
  if (fd !== 0) return 0;
  if (terminated) return 0;
  if (pendingSignal >= 0) { pendingSignal = -1; return -1; }
  if (inputQueue.length > 0) {
    const input = inputQueue[0];
    const n = Math.min(input.length, count);
    new Uint8Array(engineMemory.buffer, ptr, n).set(input.subarray(0, n));
    if (n >= input.length) inputQueue.shift();
    else inputQueue[0] = input.subarray(n);
    return n;
  }
  return -2;
}

function posixWrite(fd, ptr, count) {
  if (fd === 1 || fd === 2) {
    const bytes = new Uint8Array(engineMemory.buffer, ptr, count);
    const text = decoder.decode(bytes, {stream: true});
    self.postMessage({type: "output", text});
  }
  return count;
}

async function run(wasmBuf, source) {
  const wasmBytes = new Uint8Array(wasmBuf);
  const hostFloat = (ptr, length, asF32) => {
    const bytes = new Uint8Array(engineMemory.buffer, ptr, length);
    return watFloat(decoder.decode(bytes), asF32);
  };
  const imports = {waste_host: {
    strtod: (ptr, length) => hostFloat(ptr, length, false),
    strtof: (ptr, length) => hostFloat(ptr, length, true),
    posix_open: () => -1,
    posix_close: () => 0,
    posix_read: posixRead,
    posix_write: posixWrite,
  }};
  const {instance} = await WebAssembly.instantiate(wasmBytes, imports);
  exp = instance.exports;
  engineMemory = exp.memory;

  const sourceBytes = new TextEncoder().encode(source);
  const scriptPtr = exp.waste_wast_alloc(sourceBytes.length);
  if (!scriptPtr) throw new Error("C engine script allocation failed");
  new Uint8Array(engineMemory.buffer, scriptPtr, sourceBytes.length).set(sourceBytes);

  self.postMessage({type: "started"});

  let yielded = exp.waste_wast_run_script(scriptPtr, sourceBytes.length);
  while (yielded) {
    await waitForIO();
    if (terminated) break;
    yielded = exp.waste_wast_resume();
  }

  const total = exp.waste_wast_results_total();
  const passed = exp.waste_wast_results_passed();
  const resultsPtr = exp.waste_wast_results_ptr();
  const resultBytes = new Uint8Array(engineMemory.buffer);
  const results = [];
  for (let i = 0; i < total; i++) {
    const base = resultsPtr + i * 256;
    let funcEnd = 1;
    while (funcEnd < 64 && resultBytes[base + funcEnd]) funcEnd++;
    let errorEnd = 64;
    while (errorEnd < 256 && resultBytes[base + errorEnd]) errorEnd++;
    results.push({
      pass: resultBytes[base] !== 0,
      func: decoder.decode(resultBytes.subarray(base + 1, base + funcEnd)),
      error: decoder.decode(resultBytes.subarray(base + 64, base + errorEnd)),
    });
  }
  self.postMessage({type: "done", ok: total === 0 || passed === total,
    total, passed, results});
}

self.onmessage = function(e) {
  const msg = e.data;
  if (msg.type === "start") {
    run(msg.wasmBytes, msg.source).catch(error => {
      self.postMessage({type: "done", ok: false,
        error: error && (error.stack || error.message) || String(error)});
    });
  } else if (msg.type === "input") {
    inputQueue.push(new Uint8Array(msg.bytes));
    if (ioResolve) { ioResolve(); ioResolve = null; }
  } else if (msg.type === "signal") {
    pendingSignal = msg.signal;
    if (ioResolve) { ioResolve(); ioResolve = null; }
  } else if (msg.type === "stop") {
    terminated = true;
    if (ioResolve) { ioResolve(); ioResolve = null; }
  }
};
