// Byte-oriented command host. The engine owns processes, descriptors and files.
const encoder = new TextEncoder();
const decoder = new TextDecoder();
const outcomes = ["configuring", "runnable", "waiting", "exited", "signaled",
  "trapped", "failed", "cancelled", "timed_out", "limit"];
const defaults = {maxInputBytes: 16 * 1024 * 1024, maxOutputBytes: 16 * 1024 * 1024,
  timeoutMs: 0, quantumMs: 16, maxMemoryPages: 2048, maxProcesses: 16,
  maxFilesystemBytes: 128 * 1024 * 1024, maxInstructions: 0n};
const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
function closeIterator(iterator) {
  // User-provided cleanup may throw synchronously. It must never bypass the
  // session's destruction or replace an already completed command result.
  if (iterator?.return) Promise.resolve().then(() => iterator.return()).catch(() => {});
}
function streamIterator(source) {
  const reader = source.getReader?.();
  return reader ? {next: () => reader.read(), async return() {
    try { await reader.cancel(); } finally { reader.releaseLock(); }
    return {done: true};
  }} : source[Symbol.asyncIterator]();
}

export class WasteError extends Error {
  constructor(message, code = 1) { super(message); this.name = "WasteError"; this.code = code; }
}
function bytes(value) {
  if (typeof value === "string") return encoder.encode(value);
  if (value instanceof Uint8Array) return value;
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  throw new TypeError("Expected text, Uint8Array or ArrayBuffer");
}
function string(value) {
  if (typeof value !== "string" || value.includes("\0")) throw new TypeError("Expected a string without NULs");
  return encoder.encode(value);
}
function assets(files = {}) {
  return Object.entries(files).map(([path, value]) => {
    const record = value && typeof value === "object" && "bytes" in value ? value : {bytes: value};
    return [path, {bytes: bytes(record.bytes).slice(), mode: record.mode ??
      (path.startsWith("/usr/bin/") || path.startsWith("/bin/") ? 0o755 : 0o644)}];
  });
}
function commandVector(executable, args = [], env) {
  if (!Array.isArray(args)) throw new TypeError("args must be an array");
  const path = string(executable);
  const entries = env === undefined ? [] : Array.isArray(env) ? env :
    Object.entries(env).map(([key, value]) => `${key}=${value}`);
  const values = [...args, ...entries].map(string);
  const size = 12 + path.length + values.reduce((n, value) => n + 4 + value.length, 0);
  if (size > 128 * 1024 || args.length > 126 || entries.length > 255) throw new RangeError("Command vectors exceed ABI limits");
  const record = new Uint8Array(size), view = new DataView(record.buffer);
  view.setUint32(0, args.length, true);
  view.setUint32(4, env === undefined ? 0xffffffff : entries.length, true);
  view.setUint32(8, path.length, true);
  record.set(path, 12);
  let at = 12 + path.length;
  for (const value of values) {
    view.setUint32(at, value.length, true); at += 4;
    record.set(value, at); at += value.length;
  }
  return record;
}

export async function createWaste({engine, files = {}, limits = {}}) {
  const module = engine instanceof WebAssembly.Module ? engine : await WebAssembly.compile(bytes(engine));
  const template = assets(files);
  let exp;
  const host = {
    monotonic_ns: () => BigInt(Math.floor(performance.now() * 1e6)),
    realtime_ns: () => BigInt(Date.now()) * 1000000n,
    random: (pointer, length) => {
      if (!globalThis.crypto?.getRandomValues) return -38;
      try {
        for (let at = 0; at < length; at += 65536)
          globalThis.crypto.getRandomValues(new Uint8Array(exp.memory.buffer, pointer + at, Math.min(65536, length - at)));
        return 0;
      } catch { return -5; }
    }
  };
  const instance = await WebAssembly.instantiate(module, {waste_command_host: host});
  exp = instance.exports;
  if ((exp.waste_command_version?.() >>> 16) !== 1) throw new WasteError("Incompatible command host ABI");
  const f = name => exp[`waste_command_${name}`];
  if ((f("features")() & 15) !== 15 || f("options_size")?.() !== 64 ||
      f("result_size")?.() !== 328 || f("file_info_size")?.() !== 32)
    throw new WasteError("Incompatible command features or record sizes");
  let busy = false, disposed = false;
  function allocation(size, callback) {
    const pointer = f("alloc")(Math.max(size, 1));
    if (!pointer) throw new WasteError("Cannot allocate host transfer buffer");
    try { return callback(pointer); } finally { f("free")(pointer); }
  }
  function error(code) {
    return allocation(256, pointer => {
      const status = f("error")(pointer, 256);
      const value = new Uint8Array(exp.memory.buffer, pointer, 256).slice();
      return new WasteError(decoder.decode(value.subarray(0, value.indexOf(0) < 0 ? 256 : value.indexOf(0))), status || -code);
    });
  }
  function checked(code) { if (code < 0) throw error(code); return code; }
  function transfer(value, callback) {
    return allocation(value.length, pointer => {
      new Uint8Array(exp.memory.buffer, pointer, value.length).set(value);
      return callback(pointer, value.length);
    });
  }
  function inspect(handle) {
    return allocation(328, pointer => {
      checked(f("inspect")(handle, pointer, 328));
      const copy = new Uint8Array(exp.memory.buffer, pointer, 328).slice();
      const view = new DataView(copy.buffer);
      const message = copy.subarray(48, 304);
      return {state: view.getUint32(4, true), wait: view.getUint32(8, true),
        exitCode: view.getInt32(12, true), signal: view.getUint32(16, true),
        stdoutBytes: view.getUint32(20, true), stderrBytes: view.getUint32(24, true),
        elapsedNs: view.getBigUint64(32, true), waitNs: view.getBigUint64(40, true),
        message: decoder.decode(message.subarray(0, message.indexOf(0) < 0 ? 256 : message.indexOf(0))),
        resources: {memoryPagesPeak: view.getUint32(304, true), filesystemBytesPeak: view.getUint32(308, true),
          processesPeak: view.getUint32(312, true), instructions: view.getBigUint64(320, true)}};
    });
  }
  function output(handle, fd, length) {
    return allocation(length, pointer => {
      const copied = checked(f("output")(handle, fd, 0, pointer, length));
      return new Uint8Array(exp.memory.buffer, pointer, copied).slice();
    });
  }
  function readFile(handle, path, maximum) {
    return transfer(string(path), (pathPointer, pathLength) => allocation(32, metadata => {
      checked(f("stat")(handle, pathPointer, pathLength, metadata));
      const view = new DataView(exp.memory.buffer, metadata, 32);
      const size = Number(view.getBigUint64(8, true));
      if (size > maximum) throw new WasteError("Collected files exceed byte limit");
      return allocation(size, pointer => {
        const count = checked(f("read_file")(handle, pathPointer, pathLength, 0, 0, pointer, size));
        if (count !== size) throw new WasteError("Output file changed during collection");
        return new Uint8Array(exp.memory.buffer, pointer, count).slice();
      });
    }));
  }

  return {
    abiVersion: {major: 1, minor: 0},
    features: f("features")(),
    dispose() {
      if (busy) throw new WasteError("Cannot dispose an active runtime; cancel its run first");
      disposed = true;
      template.length = 0;
      exp = undefined;
    },
    async run({executable, args = [], env, cwd = "/work", files: inputFiles = {},
      stdin = new Uint8Array(), outputPaths = [], signal, limits: overrides = {}}) {
      if (disposed || busy) throw new WasteError(disposed ? "Runtime is disposed" : "Runtime already has an active run");
      const vector = commandVector(executable, args, env);
      if (!Array.isArray(outputPaths)) throw new TypeError("outputPaths must be an array");
      outputPaths.forEach(string);
      const selected = {...defaults, ...limits, ...overrides};
      const record = new Uint8Array(64), view = new DataView(record.buffer);
      view.setUint32(0, 64, true);
      for (const [i, key] of ["maxInputBytes", "maxOutputBytes", "timeoutMs", "quantumMs", "maxMemoryPages", "maxProcesses", "maxFilesystemBytes"].entries()) {
        const value = selected[key];
        if (!Number.isInteger(value) || value < (key === "timeoutMs" ? 0 : 1) || value > 0xffffffff)
          throw new RangeError(`Invalid ${key}`);
        view.setUint32(4 + i * 4, value, true);
      }
      if (typeof selected.maxInstructions === "number" && !Number.isSafeInteger(selected.maxInstructions))
        throw new RangeError("maxInstructions must be a safe integer or BigInt");
      const instructionLimit = BigInt(selected.maxInstructions);
      if (instructionLimit < 0n || instructionLimit > 0xffffffffffffffffn) throw new RangeError("Invalid maxInstructions");
      view.setBigUint64(32, instructionLimit, true);
      let handle = 0, alive = true, inputError, iterator;
      const cancel = () => { if (alive && handle) checked(f("cancel")(handle)); };
      busy = true;
      try {
        handle = transfer(record, (pointer, length) => f("create")(1, 0, 15, pointer, length));
        if (!handle) throw error(-1);
        const directories = new Set(["/", "/bin", "/dev", "/usr", "/usr/bin", "/usr/lib", "/lib", "/root", "/tmp", "/work"]);
        function parents(path) {
          const parent = path.slice(0, path.lastIndexOf("/")) || "/";
          if (!directories.has(parent)) {
            parents(parent);
            transfer(string(parent), (pointer, length) => checked(f("mkdir")(handle, pointer, length, 0o755)));
            directories.add(parent);
          }
        }
        for (const [path, asset] of [...template, ...assets(inputFiles)]) {
          parents(path);
          transfer(string(path), (pathPointer, pathLength) => transfer(asset.bytes,
            (pointer, length) => checked(f("put_file")(handle, pathPointer, pathLength, pointer, length, asset.mode))));
        }
        transfer(string(cwd), (pointer, length) => checked(f("cwd")(handle, pointer, length)));
        const append = (value, eof) => transfer(bytes(value), (pointer, length) => checked(f("input")(handle, pointer, length, eof ? 1 : 0)));
        const streaming = stdin?.[Symbol.asyncIterator] || stdin?.getReader;
        if (!streaming) append(stdin, true);
        signal?.addEventListener("abort", cancel, {once: true});
        if (signal?.aborted) cancel();
        if (inspect(handle).state < 3) {
          const started = transfer(vector, (pointer, length) => f("start")(handle, pointer, length));
          if (started < 0 && inspect(handle).state < 3) throw error(started);
        }
        if (streaming && inspect(handle).state < 3) {
          iterator = streamIterator(stdin);
          // Never retain a destination memory view across await or engine calls.
          (async () => {
            try {
              while (alive) {
                const item = await iterator.next();
                if (!alive) break;
                if (item.done) { append(new Uint8Array(), true); break; }
                append(item.value, false);
                await pause(0);
              }
            } catch (failure) { if (alive) { inputError = failure; cancel(); } }
          })();
        }
        let result = inspect(handle);
        while (result.state < 3) {
          checked(f("step")(handle));
          result = inspect(handle);
          if (result.state < 3) await pause(result.wait === 1 ? 2 :
            result.wait === 2 ? Math.min(Number(result.waitNs / 1000000n), 16) : 0);
        }
        if (inputError) throw inputError;
        const outputFiles = Object.create(null), outputErrors = Object.create(null);
        let collected = 0;
        for (const path of [...new Set(outputPaths)]) {
          try {
            const value = readFile(handle, path, selected.maxOutputBytes - collected);
            outputFiles[path] = value; collected += value.length;
          } catch (failure) { outputErrors[path] = {code: failure.code, message: failure.message}; }
        }
        return {outcome: outcomes[result.state], exitCode: result.state === 3 ? result.exitCode : null,
          signal: result.signal || null, message: result.message,
          stdout: output(handle, 1, result.stdoutBytes), stderr: output(handle, 2, result.stderrBytes),
          outputFiles, outputErrors, elapsedNs: result.elapsedNs,
          resources: result.resources,
          engineMemoryBytes: exp.memory.buffer.byteLength};
      } finally {
        alive = false;
        try { signal?.removeEventListener("abort", cancel); }
        finally {
          closeIterator(iterator);
          try { if (handle) f("destroy")(handle); } finally { busy = false; }
        }
      }
    }
  };
}

// Worker jobs use separate engine instances from other runtimes. The factory
// hook allows an enclosing application to supply a bundled Blob worker for
// file:// operation, CSP integration, or a Node worker in host-boundary tests.
export async function createWasteWorker({engine, files = {}, limits = {}, workerFactory, workerSource} = {}) {
  let objectURL;
  const url = workerSource === undefined ? new URL("./waste.worker.mjs", import.meta.url) :
    (objectURL = URL.createObjectURL(new Blob([workerSource], {type: "text/javascript"})));
  // Bundled source is a classic script with no imports. In particular, this
  // avoids a module fetch from the opaque origin of a file:// Blob worker.
  const worker = workerFactory ? workerFactory(url) : new Worker(url, {type: workerSource === undefined ? "module" : "classic"});
  let disposed = false, active, nextId = 1, readyResolve, readyReject;
  const ready = new Promise((resolve, reject) => { readyResolve = resolve; readyReject = reject; });
  const listen = (name, callback) => worker.addEventListener ? worker.addEventListener(name, callback) :
    worker.on(name, value => callback(name === "message" ? {data: value} : value));
  function failAll(failure) {
    if (!disposed) Promise.resolve(worker.terminate()).catch(() => {});
    disposed = true;
    readyReject(failure);
    active?.reject(failure);
    active?.ack?.(false);
  }
  listen("error", event => failAll(new WasteError(event.message || "Worker failed")));
  listen("message", ({data: message}) => {
    if (!message || message.protocol !== 1) { failAll(new WasteError("Incompatible worker protocol")); return; }
    if (message.type === "ready" && message.id === 0) {
      if (objectURL) { URL.revokeObjectURL(objectURL); objectURL = undefined; }
      readyResolve(message); return;
    }
    if (message.type === "error" && message.id === 0) {
      readyReject(new WasteError(message.error.message, message.error.code)); return;
    }
    if (!active || message.id !== active.id) return;
    if (message.type === "result") {
      if (active.inputError) active.reject(active.inputError);
      else active.resolve(message.result);
    }
    else if (message.type === "input-ack" && message.sequence === active.sequence) {
      active.ack?.(true); active.ack = undefined;
    } else if (message.type === "error") active.reject(new WasteError(message.error.message, message.error.code));
  });
  try {
    worker.postMessage({protocol: 1, type: "init", id: 0, engine, files: Object.fromEntries(assets(files)), limits});
    await ready;
  } catch (failure) {
    if (objectURL) URL.revokeObjectURL(objectURL);
    await worker.terminate(); throw failure;
  }
  return {
    abiVersion: {major: 1, minor: 0},
    async dispose() {
      if (active) throw new WasteError("Cannot dispose an active runtime; cancel its run first");
      if (disposed) return;
      disposed = true;
      await worker.terminate();
    },
    async run({stdin = new Uint8Array(), signal, ...job}) {
      if (disposed || active) throw new WasteError(disposed ? "Runtime is disposed" : "Runtime already has an active run");
      if (!Number.isSafeInteger(nextId)) throw new WasteError("Worker request ID capacity exceeded");
      const id = nextId++, streaming = Boolean(stdin?.[Symbol.asyncIterator] || stdin?.getReader);
      let resolve, reject, iterator;
      const completion = new Promise((yes, no) => { resolve = yes; reject = no; });
      const current = {id, resolve, reject, sequence: 0};
      active = current;
      const cancel = () => { if (active === current) worker.postMessage({protocol: 1, type: "cancel", id}); };
      try {
        signal?.addEventListener("abort", cancel, {once: true});
        worker.postMessage({protocol: 1, type: "run", id, job: {...job, stdin: streaming ? undefined : bytes(stdin)}, streaming});
        if (signal?.aborted) cancel();
        if (streaming) {
          iterator = streamIterator(stdin);
          (async () => {
            try {
              while (active === current) {
                const item = await iterator.next();
                if (active !== current) break;
                const chunk = item.done ? new Uint8Array() : bytes(item.value).slice();
                const sequence = ++current.sequence;
                const ack = new Promise(yes => { current.ack = yes; });
                worker.postMessage({protocol: 1, type: "input", id, sequence, bytes: chunk, eof: Boolean(item.done)}, [chunk.buffer]);
                if (!await ack || item.done) break;
              }
            } catch (failure) { if (active === current) { current.inputError = failure; cancel(); } }
          })();
        }
        return await completion;
      } finally {
        try { signal?.removeEventListener("abort", cancel); }
        finally {
          current.ack?.(false);
          if (active === current) active = undefined;
          closeIterator(iterator);
        }
      }
    }
  };
}
