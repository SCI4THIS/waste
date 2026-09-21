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
let ioResolve = null;
let ioPending = false;
let terminated = false;

function waitForIO() {
  if (ioPending) {
    ioPending = false;
    return Promise.resolve();
  }
  return new Promise(resolve => { ioResolve = resolve; });
}

const decoder = new TextDecoder();

function posixRead(fd, ptr, count) {
  /* Interactive reads are owned by the engine kernel.  This import remains
     only for noninteractive sandboxes and never uses the old -2 protocol. */
  return terminated ? 0 : -1;
}

function posixWrite(fd, ptr, count) {
  if (fd === 1 || fd === 2) {
    const bytes = new Uint8Array(engineMemory.buffer, ptr, count);
    const text = decoder.decode(bytes, {stream: true});
    self.postMessage({type: "output", text});
  }
  return count;
}

async function run(wasmBuf, source, probeBuf) {
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
  exp.waste_wast_enable_terminal();
  if (probeBuf && exp.waste_wast_stage_executable) {
    const probeBytes = new Uint8Array(probeBuf);
    const probePtr = exp.waste_wast_alloc(probeBytes.length);
    if (!probePtr) throw new Error("C engine executable staging allocation failed");
    new Uint8Array(engineMemory.buffer, probePtr, probeBytes.length).set(probeBytes);
    const staged = exp.waste_wast_stage_executable(probePtr, probeBytes.length);
    exp.waste_wast_free?.(probePtr);
    if (staged !== 0) throw new Error("C engine executable staging failed");
  }

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
    run(msg.wasmBytes, msg.source, msg.probeBytes).catch(error => {
      self.postMessage({type: "done", ok: false,
        error: error && (error.stack || error.message) || String(error)});
    });
  } else if (msg.type === "input") {
    const bytes = new Uint8Array(msg.bytes);
    const ptr = exp.waste_wast_alloc(bytes.length);
    if (!ptr) throw new Error("C engine input allocation failed");
    new Uint8Array(engineMemory.buffer, ptr, bytes.length).set(bytes);
    exp.waste_wast_enqueue_input(ptr, bytes.length);
    if (ioResolve) { ioResolve(); ioResolve = null; }
    else ioPending = true;
  } else if (msg.type === "signal") {
    exp.waste_wast_raise_signal(msg.signal);
    if (ioResolve) { ioResolve(); ioResolve = null; }
  } else if (msg.type === "resize") {
    exp.waste_wast_resize_terminal(msg.columns, msg.rows);
    if (ioResolve) { ioResolve(); ioResolve = null; }
  } else if (msg.type === "stop") {
    terminated = true;
    if (ioResolve) { ioResolve(); ioResolve = null; }
  }
};
