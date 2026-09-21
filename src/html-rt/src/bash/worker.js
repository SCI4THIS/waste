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
let engineReady = false;
let pendingResize = null;
let pendingInput = [];
let pendingInputBytes = 0;
let pendingSignals = [];
const pendingInputLimit = 4096;
const pendingSignalLimit = 16;

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

function enqueueInput(bytes) {
  if (!bytes.length) return;
  const ptr = exp.waste_wast_alloc(bytes.length);
  if (!ptr) throw new Error("C engine input allocation failed");
  new Uint8Array(engineMemory.buffer, ptr, bytes.length).set(bytes);
  exp.waste_wast_enqueue_input(ptr, bytes.length);
  if (ioResolve) { ioResolve(); ioResolve = null; }
  else ioPending = true;
}

function flushPendingEvents() {
  if (!engineReady) return;
  if (pendingResize) {
    exp.waste_wast_resize_terminal(pendingResize.columns, pendingResize.rows);
    pendingResize = null;
  }
  for (const bytes of pendingInput) enqueueInput(bytes);
  pendingInput = [];
  pendingInputBytes = 0;
  for (const signal of pendingSignals) exp.waste_wast_raise_signal(signal);
  if (pendingSignals.length) ioPending = true;
  pendingSignals = [];
}

async function run(wasmBuf, source, probeBuf, packagedFiles) {
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

  /* Submit the initial packaged namespace as metadata before the script
     creates its process store.  The engine owns the resulting pathname state;
     the browser-side tar map remains packaging input only. */
  const stageVfsPath = (path, kind, mode, size) => {
    if (!exp.waste_wast_stage_path) return;
    const pathBytes = new TextEncoder().encode(path);
    const pathPtr = exp.waste_wast_alloc(pathBytes.length);
    if (!pathPtr) throw new Error("C engine VFS path allocation failed");
    new Uint8Array(engineMemory.buffer, pathPtr, pathBytes.length).set(pathBytes);
    const staged = exp.waste_wast_stage_path(pathPtr, pathBytes.length,
                                              kind, mode, size >>> 0);
    exp.waste_wast_free?.(pathPtr);
    if (staged !== 0) throw new Error(`C engine VFS metadata failed for ${path}`);
  };
  stageVfsPath("/tmp", 2, 0o777, 0);
  stageVfsPath("/usr/bin", 2, 0o755, 0);
  if (packagedFiles && packagedFiles.length) {
    stageVfsPath("/usr/share", 2, 0o755, 0);
    stageVfsPath("/usr/share/waste", 2, 0o755, 0);
    for (const file of packagedFiles) {
      const pathBytes = new TextEncoder().encode(file.path);
      const fileBytes = new Uint8Array(file.bytes);
      const pathPtr = exp.waste_wast_alloc(pathBytes.length);
      const dataPtr = exp.waste_wast_alloc(fileBytes.length);
      if (!pathPtr || !dataPtr) throw new Error("C engine packaged-file allocation failed");
      new Uint8Array(engineMemory.buffer, pathPtr, pathBytes.length).set(pathBytes);
      new Uint8Array(engineMemory.buffer, dataPtr, fileBytes.length).set(fileBytes);
      const staged = exp.waste_wast_stage_file(pathPtr, pathBytes.length,
                                                dataPtr, fileBytes.length, file.mode);
      exp.waste_wast_free?.(pathPtr);
      exp.waste_wast_free?.(dataPtr);
      if (staged !== 0) throw new Error(`C engine packaged file staging failed for ${file.path}`);
    }
  }
  if (probeBuf && (!packagedFiles || !packagedFiles.some(file => file.path === "/bin/waste-probe")) && exp.waste_wast_stage_file) {
    const pathBytes = new TextEncoder().encode("/bin/waste-probe");
    const fileBytes = new Uint8Array(probeBuf);
    const pathPtr = exp.waste_wast_alloc(pathBytes.length);
    const dataPtr = exp.waste_wast_alloc(fileBytes.length);
    if (!pathPtr || !dataPtr) throw new Error("C engine VFS file allocation failed");
    new Uint8Array(engineMemory.buffer, pathPtr, pathBytes.length).set(pathBytes);
    new Uint8Array(engineMemory.buffer, dataPtr, fileBytes.length).set(fileBytes);
    const staged = exp.waste_wast_stage_file(pathPtr, pathBytes.length,
                                              dataPtr, fileBytes.length, 0o755);
    exp.waste_wast_free?.(pathPtr);
    exp.waste_wast_free?.(dataPtr);
    if (staged !== 0) throw new Error("C engine VFS file staging failed");
  } else if (probeBuf) {
    stageVfsPath("/bin/waste-probe", 1, 0o755,
                 new Uint8Array(probeBuf).byteLength);
  }

  const sourceBytes = new TextEncoder().encode(source);
  const scriptPtr = exp.waste_wast_alloc(sourceBytes.length);
  if (!scriptPtr) throw new Error("C engine script allocation failed");
  new Uint8Array(engineMemory.buffer, scriptPtr, sourceBytes.length).set(sourceBytes);

  let yielded = exp.waste_wast_run_script(scriptPtr, sourceBytes.length);
  if (exp.waste_wast_path_access) {
    const checkVfs = (path, mode) => {
      const pathBytes = new TextEncoder().encode(path);
      const pathPtr = exp.waste_wast_alloc(pathBytes.length);
      if (!pathPtr) throw new Error("C engine VFS check allocation failed");
      new Uint8Array(engineMemory.buffer, pathPtr, pathBytes.length).set(pathBytes);
      const result = exp.waste_wast_path_access(pathPtr, pathBytes.length, mode);
      exp.waste_wast_free?.(pathPtr);
      if (result !== 0) throw new Error(`C engine VFS check failed for ${path}`);
    };
    checkVfs("/tmp", 0);
    checkVfs("/usr/bin", 0);
    if (probeBuf) checkVfs("/bin/waste-probe", 1);
    const packagedPaths = (packagedFiles || []).map(file => file.path);
    for (const path of packagedPaths) checkVfs(path, 0);
    self.postMessage({type: "vfs", paths: ["/tmp", "/usr/bin",
      ...(probeBuf ? ["/bin/waste-probe"] : []), ...packagedPaths]});
  }
  engineReady = true;
  flushPendingEvents();
  self.postMessage({type: "started"});
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
    run(msg.wasmBytes, msg.source, msg.probeBytes, msg.vfsFiles || []).catch(error => {
      self.postMessage({type: "done", ok: false,
        error: error && (error.stack || error.message) || String(error)});
    });
  } else if (msg.type === "input") {
    const bytes = new Uint8Array(msg.bytes);
    if (!engineReady) {
      if (pendingInputBytes + bytes.length <= pendingInputLimit) {
        pendingInput.push(bytes);
        pendingInputBytes += bytes.length;
      }
    } else enqueueInput(bytes);
  } else if (msg.type === "signal") {
    if (!engineReady) {
      if (pendingSignals.length < pendingSignalLimit)
        pendingSignals.push(msg.signal);
    } else {
      exp.waste_wast_raise_signal(msg.signal);
      if (ioResolve) { ioResolve(); ioResolve = null; }
    }
  } else if (msg.type === "resize") {
    if (!engineReady) {
      pendingResize = {columns: msg.columns, rows: msg.rows};
    } else {
      exp.waste_wast_resize_terminal(msg.columns, msg.rows);
      if (ioResolve) { ioResolve(); ioResolve = null; }
    }
  } else if (msg.type === "stop") {
    terminated = true;
    if (ioResolve) { ioResolve(); ioResolve = null; }
  }
};
