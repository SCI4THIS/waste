"use strict";
globalThis.WASTE_CONFIG = Object.freeze({"BROWSER_RESULT_BYTES":256,"BROWSER_RESULT_ERROR_BYTES":192,"BROWSER_RESULT_ERROR_OFFSET":64,"BROWSER_RESULT_NAME_BYTES":63,"EXECUTION_MAX_PUMP_QUANTUM_MS":60000,"EXECUTION_MAX_TIMEOUT_MS":3600000,"GUEST_TEST_MAX_MEMORY_BYTES":33554432,"JSON_MAX_DEPTH":64,"LEGACY_BROWSER_TIMEOUT_BULK_MS":60000,"LEGACY_BROWSER_TIMEOUT_CORE_MS":35000,"LEGACY_BROWSER_TIMEOUT_DEFAULT_MS":60000,"LEGACY_BROWSER_TIMEOUT_SIMD_MS":50000,"LEGACY_NATIVE_TIMEOUT_DEFAULT_MS":30000,"NATIVE_EXEC_BYTES_MAX":16777216,"NATIVE_HOST_IO_REPLY_MAX":16,"NATIVE_SESSION_MAX_COMMANDS":1024,"NATIVE_SESSION_SOURCE_MAX_BYTES":67108864,"NATIVE_STAGED_FILE_MAX":64,"NATIVE_TERMINAL_COLUMNS":80,"NATIVE_TERMINAL_ROWS":24,"POSIX_KERNEL_FD_MAX":64,"PROCESS_INITIAL_MEMORY_PAGES":2,"PROCESS_INITIAL_TABLE_ENTRIES":1,"PROCESS_STDIO_BUFFER_BYTES":4096,"RENDER_TEST_REPLY_MAX_BYTES":65536,"SOURCE_SHEBANG_MAX_BYTES":4096,"SUITE_BROWSER_MAX_JOBS":8,"SUITE_BROWSER_UI_TIMEOUT_MS":60000,"SUITE_GUEST_DEFAULT_JOBS":2,"SUITE_GUEST_MAX_ARGS":64,"SUITE_GUEST_REPLY_MAX_BYTES":16777216,"SUITE_GUEST_REQUEST_MAX_BYTES":4096,"SUITE_NATIVE_MAX_JOBS":64,"SUITE_OUTPUT_MAX_BYTES":16777216,"SUITE_TIMEOUT_CORE_MS":15000,"SUITE_TIMEOUT_DEFAULT_MS":5000,"SUITE_TIMEOUT_HEAVY_MS":10000,"TERMINAL_INPUT_CAPACITY":4096,"VFS_INVENTORY_MAX_BYTES":2097152,"VFS_MAX_BYTES":134217728,"VFS_MAX_ENTRIES":2048,"VFS_PATH_MAX_BYTES":256,"VFS_RUNTIME_NODE_RESERVE":64,"WAST_MAX_DATA_SEGS":128,"WAST_MAX_ELEM_SEGS":128,"WAST_SETUP_ERROR_BYTES":256});
"use strict";
let guestSuitePending = 0, guestSuiteSequence = 0;
let renderTestPending = 0;

function decodeB64(text) {
  const raw = atob(text), bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
  return bytes;
}

/* Repository-owned browser-native fixtures still have a small compatibility
 * import ABI. Keep that mode inside this packaged worker so the Bash page's
 * installed-test controller covers the complete mounted manifest. */
async function runBrowserNative(testSpec) {
  if (testSpec.error) throw new Error(testSpec.error);
  const sharedMemory = new WebAssembly.Memory({initial: 1});
  let activeMemory = null;
  let nextFd = 3, nextPid = 2, forkNumber = 0;
  const descriptors = new Map(), children = new Map();
  const bytes = () => new Uint8Array(activeMemory.buffer);
  const view = () => new DataView(activeMemory.buffer);
  const writeI32 = (ptr, value) => view().setInt32(ptr, value, true);
  const cstring = ptr => {
    let end = ptr; const mem = bytes();
    while (end < mem.length && mem[end]) end++;
    return new TextDecoder().decode(mem.subarray(ptr, end));
  };
  const allocFd = object => { const fd = nextFd++; descriptors.set(fd, object); return fd; };
  const env = {
    getpid: () => 1, getppid: () => 0, getpgrp: () => 1,
    tcgetpgrp: () => 1, tcsetpgrp: () => 0, setpgid: () => 0,
    raise: () => 0, sleep: () => 0, exit: () => 0,
    open: (path, flags, mode) => {
      void cstring(path); void flags; void mode;
      return allocFd({data: new Uint8Array(), offset: 0});
    },
    close: fd => { descriptors.delete(fd); return 0; },
    write: (fd, ptr, count) => {
      const d = descriptors.get(fd); if (!d) return -1;
      const input = bytes().slice(ptr, ptr + count);
      if (d.pipe) {
        const grown = new Uint8Array(d.data.length + count);
        grown.set(d.data); grown.set(input, d.data.length); d.data = grown;
        return count;
      }
      const needed = d.offset + count;
      if (!d.data || d.data.length < needed) {
        const grown = new Uint8Array(needed); if (d.data) grown.set(d.data); d.data = grown;
      }
      d.data.set(input, d.offset); d.offset += count; return count;
    },
    read: (fd, ptr, count) => {
      const d = descriptors.get(fd); if (!d || !d.data) return -1;
      const n = Math.min(count, d.data.length - d.offset);
      bytes().set(d.data.subarray(d.offset, d.offset + n), ptr); d.offset += n; return n;
    },
    lseek: (fd, offset, whence) => {
      const d = descriptors.get(fd); if (!d) return -1n;
      const n = Number(offset); d.offset = whence === 0 ? n : whence === 1 ? d.offset + n : d.data.length + n;
      return BigInt(d.offset);
    },
    pipe: ptr => {
      const pipe = {data: new Uint8Array(), offset: 0, pipe: true};
      const readFd = allocFd(pipe), writeFd = allocFd(pipe);
      writeI32(ptr, readFd); writeI32(ptr + 4, writeFd); return 0;
    },
    dup: fd => { const d = descriptors.get(fd); return d ? allocFd(d) : -1; },
    fork: () => {
      const pid = nextPid++, number = ++forkNumber;
      children.set(pid, {number, waits: 0, signal: 0}); return pid;
    },
    kill: (pid, signal) => { const c = children.get(pid); if (c && signal !== 18) c.signal = signal; return 0; },
    killpg: (pid, signal) => { const c = children.get(pid); if (c) c.signal = signal; return 0; },
    waitpid: (pid, statusPtr, options) => {
      const c = children.get(pid); if (!c) return -1;
      c.waits++;
      let status;
      if (options & 2) status = (19 << 8) | 0x7f;
      else if (c.signal) status = c.signal;
      else status = (c.number === 1 ? 7 : c.number === 2 ? 3 : 0) << 8;
      writeI32(statusPtr, status); return pid;
    },
  };
  const imports = {env, spectest: {memory: sharedMemory}};
  const modules = [], named = new Map();
  for (const moduleSpec of testSpec.modules) {
    const {instance} = await WebAssembly.instantiate(decodeB64(moduleSpec.wasmB64), imports);
    modules.push(instance);
    if (moduleSpec.id) named.set(moduleSpec.id, instance);
  }
  const current = modules[modules.length - 1];
  const results = [];
  for (const step of testSpec.steps) {
    const instance = step.module ? named.get(step.module) : current;
    if (!instance) throw new Error("unknown module $" + step.module);
    activeMemory = instance.exports.__waste_memory || sharedMemory;
    const fn = instance.exports[step.func];
    if (typeof fn !== "function") throw new Error("export not found: " + step.func);
    const args = step.args.map(arg => arg.type === "i64" ? BigInt(arg.value) : arg.value);
    let actual;
    try { actual = fn(...args); }
    catch (error) { results.push({func: step.func, pass: false, error: String(error)}); continue; }
    const expected = step.expect;
    const pass = expected.length === 0 || (expected.length === 1 &&
      actual === (expected[0].type === "i64" ? BigInt(expected[0].value) : expected[0].value));
    results.push({func: step.func, pass,
      error: pass ? "" : "expected " + (expected[0]?.value) + ", got " + actual});
  }
  return results;
}


function roundShiftEven(value, shift) {
  if (shift <= 0) return value << BigInt(-shift);
  const amount = BigInt(shift);
  let rounded = value >> amount;
  const remainder = value - (rounded << amount);
  const halfway = 1n << (amount - 1n);
  if (remainder > halfway ||
      (remainder === halfway && (rounded & 1n) !== 0n))
    rounded++;
  return rounded;
}

function roundRatioEven(numerator, denominator) {
  let rounded = numerator / denominator;
  const remainder = numerator % denominator;
  const comparison = remainder * 2n - denominator;
  if (comparison > 0n ||
      (comparison === 0n && (rounded & 1n) !== 0n))
    rounded++;
  return rounded;
}

function watDecimalF32(text) {
  let source = text.toLowerCase();
  let negative = false;
  if (source[0] === "+" || source[0] === "-") {
    negative = source[0] === "-";
    source = source.slice(1);
  }
  const exponentAt = source.indexOf("e");
  const significand = exponentAt < 0 ? source : source.slice(0, exponentAt);
  const decimalExponent = exponentAt < 0 ? 0 :
    Number(source.slice(exponentAt + 1));
  const pointAt = significand.indexOf(".");
  const fractionDigits = pointAt < 0 ? 0 :
    significand.length - pointAt - 1;
  const digits = significand.replace(".", "");
  const magnitude = BigInt(digits || "0");
  if (magnitude === 0n) {
    const data = new DataView(new ArrayBuffer(4));
    data.setUint32(0, negative ? 0x80000000 : 0);
    return data.getFloat32(0);
  }
  const decimalScale = decimalExponent - fractionDigits;
  let numerator = magnitude;
  let denominator = 1n;
  if (decimalScale >= 0)
    numerator *= 10n ** BigInt(decimalScale);
  else
    denominator = 10n ** BigInt(-decimalScale);

  let unbiased = numerator.toString(2).length -
                 denominator.toString(2).length;
  if (unbiased >= 0 ? numerator < (denominator << BigInt(unbiased)) :
      (numerator << BigInt(-unbiased)) < denominator)
    unbiased--;

  let exponentField = 0;
  let fraction = 0n;
  if (unbiased >= -126) {
    const shift = 23 - unbiased;
    let rounded = shift >= 0 ?
      roundRatioEven(numerator << BigInt(shift), denominator) :
      roundRatioEven(numerator, denominator << BigInt(-shift));
    if (rounded === (1n << 24n)) {
      rounded >>= 1n;
      unbiased++;
    }
    if (unbiased > 127) {
      exponentField = 0xff;
    } else {
      exponentField = unbiased + 127;
      fraction = rounded - (1n << 23n);
    }
  } else {
    const units = roundRatioEven(numerator << 149n, denominator);
    if (units >= (1n << 23n)) {
      exponentField = 1;
      fraction = units - (1n << 23n);
    } else {
      fraction = units;
    }
  }
  const bits = (negative ? 0x80000000 : 0) |
    (exponentField << 23) | Number(fraction & 0x7fffffn);
  const data = new DataView(new ArrayBuffer(4));
  data.setUint32(0, bits >>> 0);
  return data.getFloat32(0);
}

function watHexFloat(text, asF32) {
  let source = text.toLowerCase();
  let negative = false;
  if (source[0] === "+" || source[0] === "-") {
    negative = source[0] === "-";
    source = source.slice(1);
  }
  const exponentAt = source.indexOf("p");
  const significand = exponentAt < 0 ? source : source.slice(0, exponentAt);
  const exponent = exponentAt < 0 ? 0 : Number(source.slice(exponentAt + 1));
  const pointAt = significand.indexOf(".");
  const fractionDigits = pointAt < 0 ? 0 : significand.length - pointAt - 1;
  const digits = significand.replace(/^0x/, "").replace(".", "");
  const magnitude = BigInt("0x" + (digits || "0"));
  const signShift = asF32 ? 31n : 63n;
  if (magnitude === 0n) {
    if (asF32) {
      const data = new DataView(new ArrayBuffer(4));
      data.setUint32(0, negative ? 0x80000000 : 0);
      return data.getFloat32(0);
    }
    const data = new DataView(new ArrayBuffer(8));
    data.setBigUint64(0, negative ? (1n << signShift) : 0n);
    return data.getFloat64(0);
  }

  const precision = asF32 ? 24 : 53;
  const bias = asF32 ? 127 : 1023;
  const maximumExponent = asF32 ? 127 : 1023;
  const minimumNormal = asF32 ? -126 : -1022;
  const minimumSubnormal = asF32 ? -149 : -1074;
  const fractionBits = BigInt(precision - 1);
  const bitLength = magnitude.toString(2).length;
  const scale = exponent - fractionDigits * 4;
  let unbiased = bitLength - 1 + scale;
  let exponentField = 0;
  let fraction = 0n;

  if (unbiased >= minimumNormal) {
    let rounded = roundShiftEven(magnitude, bitLength - precision);
    if (rounded === (1n << BigInt(precision))) {
      rounded >>= 1n;
      unbiased++;
    }
    if (unbiased > maximumExponent) {
      exponentField = asF32 ? 0xff : 0x7ff;
    } else {
      exponentField = unbiased + bias;
      fraction = rounded - (1n << fractionBits);
    }
  } else {
    const unitShift = scale - minimumSubnormal;
    const units = unitShift >= 0 ?
      magnitude << BigInt(unitShift) :
      roundShiftEven(magnitude, -unitShift);
    const normalUnit = 1n << fractionBits;
    if (units >= normalUnit) {
      exponentField = 1;
      fraction = units - normalUnit;
    } else {
      fraction = units;
    }
  }

  if (asF32) {
    const bits = (negative ? 0x80000000 : 0) |
      (exponentField << 23) | Number(fraction & 0x7fffffn);
    const data = new DataView(new ArrayBuffer(4));
    data.setUint32(0, bits >>> 0);
    return data.getFloat32(0);
  }
  const bits = (negative ? (1n << signShift) : 0n) |
    (BigInt(exponentField) << 52n) | (fraction & 0xfffffffffffffn);
  const data = new DataView(new ArrayBuffer(8));
  data.setBigUint64(0, bits);
  return data.getFloat64(0);
}

function watFloat(text, asF32) {
  const unsigned = text[0] === "+" || text[0] === "-" ? text.slice(1) : text;
  if (unsigned.toLowerCase().startsWith("0x"))
    return watHexFloat(text, asF32);
  if (asF32) return watDecimalF32(text);
  const value = Number(text);
  return value;
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
let pendingClocks = null;
const pendingInputLimit = WASTE_CONFIG.TERMINAL_INPUT_CAPACITY;
const pendingSignalLimit = 16;

function wakeIO() {
  if (ioResolve) { ioResolve(); ioResolve = null; }
  else ioPending = true;
}

function controlError(operation) {
  self.postMessage({type: "control-error", operation,
    error: "Invalid or unavailable terminal control event"});
}

function waitForIO(pollMs) {
  if (ioPending) {
    ioPending = false;
    return Promise.resolve();
  }
  return new Promise(resolve => {
    let timer = null;
    const finish = () => {
      if (timer !== null) clearTimeout(timer);
      if (ioResolve === finish) ioResolve = null;
      resolve();
    };
    ioResolve = finish;
    if (pollMs) timer = setTimeout(finish, pollMs);
  });
}

const decoder = new TextDecoder();
let pendingOutput = "";

function flushOutput() {
  if (!pendingOutput) return;
  self.postMessage({type: "output", text: pendingOutput});
  pendingOutput = "";
}

function posixRead(fd, ptr, count) {
  /* Interactive reads are owned by the engine kernel.  This import remains
     only for noninteractive sandboxes and never uses the old -2 protocol. */
  return terminated ? 0 : -1;
}

function posixWrite(fd, ptr, count) {
  if (fd === 1 || fd === 2) {
    const bytes = new Uint8Array(engineMemory.buffer, ptr, count);
    pendingOutput += decoder.decode(bytes, {stream: true});
  }
  return count;
}

function enqueueInput(bytes) {
  if (guestSuitePending && bytes.includes(3)) {
    self.postMessage({type: "guest-suite-cancel", id: guestSuitePending});
    bytes = bytes.filter(value => value !== 3);
  }
  if (!bytes.length) return;
  const ptr = exp.waste_wast_alloc(bytes.length);
  if (!ptr) throw new Error("C engine input allocation failed");
  new Uint8Array(engineMemory.buffer, ptr, bytes.length).set(bytes);
  exp.waste_wast_enqueue_input(ptr, bytes.length);
  wakeIO();
}

function flushPendingEvents() {
  if (!engineReady) return;
  if (pendingResize) {
    if (exp.waste_wast_resize_terminal(pendingResize.columns, pendingResize.rows) === 0)
      wakeIO();
    else controlError("resize");
    pendingResize = null;
  }
  for (const bytes of pendingInput) enqueueInput(bytes);
  pendingInput = [];
  pendingInputBytes = 0;
  for (const queued of pendingSignals) {
    const entry = typeof queued === "number" ? {signal: queued, pid: 0} : queued;
    let ret;
    if (entry.pgid)
      ret = exp.waste_wast_raise_signal_pgid(entry.signal, entry.pgid);
    else
      ret = exp.waste_wast_raise_signal_pid(entry.signal, entry.pid);
    if (ret === 0 || (entry.pgid && ret > 0)) wakeIO();
    else controlError("signal");
  }
  pendingSignals = [];
}

/* Bytes come from the existing tar extractor (or direct staging fetches).
 * The C inventory validator owns guest metadata and content verification. */
function stageInstalledVfs(exp, vfs) {
  const submit = (bytes, call) => {
    bytes = new Uint8Array(bytes);
    const ptr = exp.waste_wast_alloc(bytes.length || 1);
    if (!ptr) throw new Error("Installed VFS allocation failed");
    try {
      new Uint8Array(exp.memory.buffer, ptr, bytes.length).set(bytes);
      if (call(ptr, bytes.length) !== 0) throw new Error("Invalid installed VFS files or metadata");
    } finally { exp.waste_wast_free(ptr); }
  };
  submit(vfs.inventory, exp.waste_wast_stage_vfs_inventory);
  for (const file of vfs.files)
    submit(file.bytes, (ptr, size) => exp.waste_wast_stage_vfs_file(file.index, ptr, size));
  if (exp.waste_wast_vfs_ready() !== 0) throw new Error("Incomplete installed VFS");
}

async function run(wasmBuf, source, probeBuf, packagedFiles, buildMtime, vfs, vfsPaths = [], executionLimits, suiteRequest) {
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
    wall_clock_ms: () => Date.now(),
  }};
  const {instance} = await WebAssembly.instantiate(wasmBytes, imports);
  exp = instance.exports;
  engineMemory = exp.memory;
  if (pendingClocks && exp.waste_wast_set_clock_realtime_ns &&
      exp.waste_wast_set_clock_monotonic_ns) {
    const applied =
      exp.waste_wast_set_clock_realtime_ns(pendingClocks.realtimeLow, pendingClocks.realtimeHigh) === 0 &&
      exp.waste_wast_set_clock_monotonic_ns(pendingClocks.monotonicLow, pendingClocks.monotonicHigh) === 0;
    pendingClocks = null;
    if (!applied) throw new Error("Deterministic clock override rejected");
  }
  if (!suiteRequest) {
    exp.waste_wast_enable_terminal();
    exp.waste_wast_enable_test_suite?.();
    exp.waste_wast_enable_render_test?.();
  }
  if (vfs) stageInstalledVfs(exp, vfs);
  if (buildMtime && exp.waste_wast_stage_build_mtime) {
    const seconds = buildMtime.sec;
    const staged = exp.waste_wast_stage_build_mtime(
      seconds >>> 0, Math.floor(seconds / 0x100000000) >>> 0,
      (buildMtime.nsec || 0) >>> 0);
    if (staged !== 0) throw new Error(`C engine build mtime failed: ${staged}`);
  }
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
  const stageVfsMtime = (seconds, nanoseconds = 0) => {
    if (!exp.waste_wast_stage_mtime) return;
    if (seconds === undefined) {
      const milliseconds = Date.now();
      seconds = Math.floor(milliseconds / 1000);
      nanoseconds = (milliseconds % 1000) * 1000000;
    }
    const low = seconds >>> 0;
    const high = Math.floor(seconds / 0x100000000) >>> 0;
    const staged = exp.waste_wast_stage_mtime(low, high, nanoseconds >>> 0);
    if (staged !== 0) throw new Error(`C engine VFS mtime failed: ${staged}`);
  };
  const stageVfsPath = (path, kind, mode, size, mtimeSec, mtimeNsec) => {
    if (!exp.waste_wast_stage_path) return;
    const pathBytes = new TextEncoder().encode(path);
    const pathPtr = exp.waste_wast_alloc(pathBytes.length);
    if (!pathPtr) throw new Error("C engine VFS path allocation failed");
    new Uint8Array(engineMemory.buffer, pathPtr, pathBytes.length).set(pathBytes);
    const staged = exp.waste_wast_stage_path(pathPtr, pathBytes.length,
                                              kind, mode, size >>> 0);
    exp.waste_wast_free?.(pathPtr);
    if (staged !== 0) throw new Error(`C engine VFS metadata failed for ${path}`);
    stageVfsMtime(mtimeSec, mtimeNsec);
  };
  const stageVfsSymlink = (path, target, mode, mtimeSec, mtimeNsec) => {
    if (!exp.waste_wast_stage_symlink)
      throw new Error("C engine VFS symlink staging is unavailable");
    const pathBytes = new TextEncoder().encode(path);
    const targetBytes = new TextEncoder().encode(target);
    const pathPtr = exp.waste_wast_alloc(pathBytes.length);
    const targetPtr = exp.waste_wast_alloc(targetBytes.length);
    if (!pathPtr || !targetPtr)
      throw new Error("C engine VFS symlink allocation failed");
    new Uint8Array(engineMemory.buffer, pathPtr, pathBytes.length).set(pathBytes);
    new Uint8Array(engineMemory.buffer, targetPtr, targetBytes.length).set(targetBytes);
    const staged = exp.waste_wast_stage_symlink(
      pathPtr, pathBytes.length, targetPtr, targetBytes.length, mode);
    exp.waste_wast_free?.(pathPtr);
    exp.waste_wast_free?.(targetPtr);
    if (staged !== 0) throw new Error(`C engine VFS symlink failed for ${path}`);
    stageVfsMtime(mtimeSec, mtimeNsec);
  };
  if (!vfs) stageVfsPath("/tmp", 2, 0o777, 0);
  /* The virtual shell runs as UID 0.  Its passwd home and initial cwd are
     /root, independent of the host directory that built the page. */
  if (!vfs) stageVfsPath("/root", 2, 0o755, 0);
  if (exp.waste_wast_stage_cwd) {
    const cwdBytes = new TextEncoder().encode("/root");
    const cwdPtr = exp.waste_wast_alloc(cwdBytes.length);
    if (!cwdPtr) throw new Error("C engine initial cwd allocation failed");
    new Uint8Array(engineMemory.buffer, cwdPtr, cwdBytes.length).set(cwdBytes);
    const staged = exp.waste_wast_stage_cwd(cwdPtr, cwdBytes.length);
    exp.waste_wast_free?.(cwdPtr);
    if (staged !== 0) throw new Error("C engine initial cwd staging failed");
  }
  if (!vfs) stageVfsPath("/usr", 2, 0o755, 0);
  if (!vfs) stageVfsPath("/usr/bin", 2, 0o755, 0);
  if (!vfs) stageVfsPath("/usr/lib", 2, 0o755, 0);
  if (!vfs) stageVfsPath("/lib", 2, 0o755, 0);
  if (packagedFiles && packagedFiles.length) {
    if (!vfs) {
      stageVfsPath("/usr/share", 2, 0o755, 0);
      stageVfsPath("/usr/share/waste", 2, 0o755, 0);
      stageVfsPath("/usr/share/licenses", 2, 0o755, 0);
      stageVfsPath("/usr/share/licenses/coreutils", 2, 0o755, 0);
    }
    for (const file of packagedFiles) {
      if (file.kind === 2) {
        stageVfsPath(file.path, 2, file.mode, 0, file.mtimeSec, file.mtimeNsec);
        continue;
      }
      if (file.kind === 3) {
        stageVfsSymlink(file.path, file.target, file.mode,
                        file.mtimeSec, file.mtimeNsec);
        continue;
      }
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
      if (staged !== 0) {
        throw new Error(`C engine packaged file staging failed for ${file.path}: ${staged}`);
      }
      stageVfsMtime(file.mtimeSec, file.mtimeNsec);
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
    stageVfsMtime();
  } else if (probeBuf) {
    stageVfsPath("/bin/waste-probe", 1, 0o755,
                 new Uint8Array(probeBuf).byteLength);
  }

  if (executionLimits) {
    const timeout = executionLimits.timeoutMs ?? 0;
    const cancel = executionLimits.cancelAfterMs ?? 0;
    if (![timeout, cancel].every(value => Number.isInteger(value) && value >= 0 && value <= WASTE_CONFIG.EXECUTION_MAX_TIMEOUT_MS) ||
        exp.waste_wast_set_execution_limits?.(timeout, cancel) !== 0)
      throw new Error("Invalid execution limits");
    const pump = executionLimits.pumpQuantumMs ?? 0;
    if (pump && (!Number.isInteger(pump) || pump < 0 || pump > WASTE_CONFIG.EXECUTION_MAX_PUMP_QUANTUM_MS ||
        !exp.waste_wast_set_pump_quantum_ms ||
        exp.waste_wast_set_pump_quantum_ms(pump) !== 0))
      throw new Error("Invalid pump quantum");
  }
  let scriptPtr, scriptLength;
  if (suiteRequest) {
    const readString = ptr => {
      const bytes = new Uint8Array(engineMemory.buffer);
      let end = ptr;
      while (end < bytes.length && bytes[end]) end++;
      return decoder.decode(bytes.subarray(ptr, end));
    };
    const count = exp.waste_wast_suite_load();
    const suiteError = () => readString(exp.waste_wast_suite_error_ptr());
    if (count < 0) throw new Error(suiteError());
    const fields = ["identity", "group", "path", "file", "mode", "skipReason"];
    const tests = Array.from({length: count}, (_, index) => {
      const test = Object.fromEntries(fields.map((field, offset) =>
        [field, readString(exp.waste_wast_suite_field(index, offset))]));
      test.expectedFailure = Boolean(exp.waste_wast_suite_expected_failure(index));
      return test;
    });
    if (suiteRequest.mode === "list") {
      self.postMessage({type: "suite-list", tests});
      return;
    }
    const index = tests.findIndex(test => test.identity === suiteRequest.identity);
    if (suiteRequest.mode !== "run" || index < 0) throw new Error("Unknown mounted test identity");
    scriptPtr = exp.waste_wast_suite_prepare(index);
    if (!scriptPtr) throw new Error(suiteError());
    scriptLength = exp.waste_wast_suite_source_length();
  } else {
    const sourceBytes = new TextEncoder().encode(source);
    scriptLength = sourceBytes.length;
    scriptPtr = exp.waste_wast_alloc(scriptLength);
    if (!scriptPtr) throw new Error("C engine script allocation failed");
    new Uint8Array(engineMemory.buffer, scriptPtr, scriptLength).set(sourceBytes);
  }
  const duration = executionLimits ? Math.min(...[
    executionLimits.timeoutMs, executionLimits.cancelAfterMs
  ].filter(value => value > 0)) : Infinity;
  let limitTimer = null;
  const wakeLimit = () => {
    if (ioResolve) { ioResolve(); ioResolve = null; }
    else ioPending = true;
    // The C deadline starts just after this timer is armed. Recheck if a
    // browser timer fires early; do not strand a still-blocked guest forever.
    limitTimer = setTimeout(wakeLimit, 10);
  };
  if (Number.isFinite(duration)) limitTimer = setTimeout(wakeLimit, duration);
  try {
  let yielded = exp.waste_wast_run_script(scriptPtr, scriptLength);
  flushOutput();
  if (!suiteRequest) {
    // The C runner tears down its per-session kernel before run_script returns.
    // Staging already validated the inventory; report its declared paths here
    // without calling kernel APIs after that store has been destroyed.
    const packagedPaths = [...vfsPaths, ...(packagedFiles || []).map(file => file.path)];
    self.postMessage({type: "vfs", paths: ["/tmp", "/root", "/usr/bin", "/bin/wat", "/bin/wast",
      ...(probeBuf ? ["/bin/waste-probe"] : []), ...packagedPaths]});
  }
  engineReady = true;
  flushPendingEvents();
  self.postMessage({type: "started", pid: exp.waste_wast_process_id?.(),
    waitKind: exp.waste_wast_wait_kind()});
  while (yielded) {
    const waitKind = exp.waste_wast_wait_kind();
    if (waitKind === 6) { /* EXEC_YIELD_PUMP: cooperative timeslice */
      /* Drain the event loop so any pending onmessage (notably a cancel
       * request) runs before we re-enter the engine.  No wait was published
       * because the engine is still runnable; this is the pump handshake. */
      await new Promise(resolve => setTimeout(resolve, 0));
      if (terminated) break;
      yielded = exp.waste_wast_resume();
      flushOutput();
      continue;
    }
    if (waitKind === 5) { /* EXEC_YIELD_HOST_IO */
      const ioKind = exp.waste_wast_host_io_kind();
      const vb = exp.waste_wast_host_io_verbose && exp.waste_wast_host_io_verbose();
      const pathPtr = exp.waste_wast_host_io_path_ptr();
      const pathLen = exp.waste_wast_host_io_path_len();
      const path = decoder.decode(new Uint8Array(engineMemory.buffer, pathPtr, pathLen));
      if (vb) console.log("[worker] host-io yield: kind=" + ioKind +
        " (1=UPLOAD, 2=DOWNLOAD) path=" + path + " waitKind=" + waitKind);
      if (ioKind === 3) { /* isolated mounted test batch */
        const ptr = exp.waste_wast_host_io_data_ptr(), len = exp.waste_wast_host_io_data_len();
        const request = new Uint8Array(engineMemory.buffer, ptr, len).slice();
        guestSuitePending = ++guestSuiteSequence;
        self.postMessage({type: "guest-suite-request", id: guestSuitePending, request}, [request.buffer]);
      } else if (ioKind === 4) { /* browser renderer diagnostic */
        renderTestPending = ++guestSuiteSequence;
        self.postMessage({type: "render-test-request", id: renderTestPending});
      } else if (ioKind === 2) { /* HOST_IO_DOWNLOAD */
        const dataPtr = exp.waste_wast_host_io_data_ptr();
        const dataLen = exp.waste_wast_host_io_data_len();
        const data = new Uint8Array(engineMemory.buffer, dataPtr, dataLen).slice();
        if (vb) console.log("[download] sending to main thread, name=" + path +
          " size=" + dataLen);
        self.postMessage({type: "host-download", name: path, bytes: data,
          verbose: vb}, [data.buffer]);
        exp.waste_wast_host_io_complete();
        ioPending = true; /* resume immediately — no browser response needed */
      } else if (ioKind === 1) { /* HOST_IO_UPLOAD */
        if (vb) console.log("[upload] sending request to main thread, destPath=" + path);
        self.postMessage({type: "host-upload-request", destPath: path, verbose: vb});
      }
    }
    // Another process's deadline can expire while the foreground shell reads.
    // The C scheduler publishes the earliest timer across the whole session.
    const waitTimeoutMs = (waitKind === 1 || waitKind === 2) && exp.waste_wast_wait_timeout_ms ?
      exp.waste_wast_wait_timeout_ms() : waitKind === 2 ? 10 : 0;
    do { await waitForIO(waitTimeoutMs); }
    while ((guestSuitePending || renderTestPending) && !terminated);
    if (terminated) break;
    yielded = exp.waste_wast_resume();
    flushOutput();
    if (yielded) self.postMessage({type: "io-ready", pid: exp.waste_wast_process_id?.(),
      waitKind: exp.waste_wast_wait_kind()});
  }

  flushOutput();

  if (!suiteRequest && exp.waste_wast_transition_evidence_ptr &&
      exp.waste_wast_transition_evidence_len) {
    const evidencePtr = exp.waste_wast_transition_evidence_ptr();
    const evidenceLength = exp.waste_wast_transition_evidence_len();
    const evidence = decoder.decode(new Uint8Array(
      engineMemory.buffer, evidencePtr, evidenceLength));
    self.postMessage({type: "output",
      text: `WASTE_TRANSITION_EVIDENCE=${evidence}\n`});
  }

  const total = exp.waste_wast_results_total();
  const passed = exp.waste_wast_results_passed();
  const resultsPtr = exp.waste_wast_results_ptr();
  const resultBytes = new Uint8Array(engineMemory.buffer);
  const nameDecoder = new TextDecoder("utf-8", {ignoreBOM: true});
  const results = [];
  for (let i = 0; i < total; i++) {
    const base = resultsPtr + i * WASTE_CONFIG.BROWSER_RESULT_BYTES;
    let funcEnd = 1;
    while (funcEnd < WASTE_CONFIG.BROWSER_RESULT_ERROR_OFFSET && resultBytes[base + funcEnd]) funcEnd++;
    let errorEnd = WASTE_CONFIG.BROWSER_RESULT_ERROR_OFFSET;
    while (errorEnd < WASTE_CONFIG.BROWSER_RESULT_BYTES && resultBytes[base + errorEnd]) errorEnd++;
    const nameStart = exp.waste_wast_result_name_ptr ? exp.waste_wast_result_name_ptr(i) : base + 1;
    const nameEnd = exp.waste_wast_result_name_len ? nameStart + exp.waste_wast_result_name_len(i) : base + funcEnd;
    results.push({
      pass: resultBytes[base] !== 0,
      func: nameDecoder.decode(resultBytes.subarray(nameStart, nameEnd)),
      error: decoder.decode(resultBytes.subarray(base + WASTE_CONFIG.BROWSER_RESULT_ERROR_OFFSET, base + errorEnd)),
    });
  }
  const setup = {total: exp.waste_wast_setup_total(),
    passed: exp.waste_wast_setup_passed(),
    complete: Boolean(exp.waste_wast_setup_complete()), failures: []};
  const phases = ["", "encode", "load", "definition", "retain"];
  for (let i = 0; i < exp.waste_wast_setup_failure_count(); i++) {
    const ptr = exp.waste_wast_setup_failure_ptr(i);
    const view = new DataView(engineMemory.buffer, ptr, 12 + WASTE_CONFIG.WAST_SETUP_ERROR_BYTES);
    const bytes = new Uint8Array(engineMemory.buffer, ptr + 12, WASTE_CONFIG.WAST_SETUP_ERROR_BYTES);
    const end = bytes.indexOf(0);
    setup.failures.push({line: view.getUint32(0, true),
      status: view.getInt32(4, true), phase: phases[view.getUint32(8, true)],
      error: decoder.decode(bytes.subarray(0, end < 0 ? bytes.length : end))});
  }
  const completed = Boolean(exp.waste_wast_script_completed());
  const ok = passed === total && setup.complete && setup.passed === setup.total && completed;
  if (!ok)
    self.postMessage({type: "output",
      text: `WASTE_DONE_RESULTS=${JSON.stringify(results)}\n`});
  self.postMessage({type: "done", ok, total, passed, results, setup, completed,
    error: suiteRequest && results.some(result => !result.pass && result.func === "(vfs)") ?
      "Cannot mount or stage batch execution image" : undefined,
    timedOut: exp.waste_wast_execution_stop_reason?.() === 1,
    cancelled: exp.waste_wast_execution_stop_reason?.() === 2,
    exited: Boolean(exp.waste_wast_guest_exited?.()),
    exitStatus: exp.waste_wast_guest_exit_status?.() || 0});
  } finally {
    if (limitTimer !== null) clearTimeout(limitTimer);
    exp.waste_wast_free?.(scriptPtr);
  }
}

self.onmessage = function(e) {
  const msg = e.data;
  if (msg.type === "browser-native-start") {
    runBrowserNative(msg.testSpec).then(results => {
      const passed = results.filter(result => result.pass).length;
      const setup = {total: msg.testSpec.modules.length,
        passed: msg.testSpec.modules.length, complete: true, failures: []};
      self.postMessage({type: "done", ok: passed === results.length,
        total: results.length, passed, results, setup, completed: true});
    }).catch(error => self.postMessage({type: "done", ok: false,
      error: error && (error.stack || error.message) || String(error)}));
  } else if (msg.type === "start") {
    run(msg.wasmBytes, msg.source, msg.probeBytes, msg.vfsFiles || [],
        msg.buildMtime, msg.vfs, msg.vfsPaths, msg.executionLimits, msg.suite).catch(error => {
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
    /* An optional positive pid routes the signal to a specific store process
       via waste_wast_raise_signal_pid; a positive pgid fans the signal across
       every live group member via waste_wast_raise_signal_pgid.  pid and pgid
       are mutually exclusive; omitting both preserves the legacy active-kernel
       queue, so backgrounded or process-group targets never steal the active
       continuation. */
    const pid = msg.pid === undefined ? 0 : msg.pid;
    const pgid = msg.pgid === undefined ? 0 : msg.pgid;
    if (!Number.isInteger(msg.signal) || msg.signal < 1 || msg.signal > 128 ||
        !Number.isInteger(pid) || pid < 0 || pid > 0x7fffffff ||
        !Number.isInteger(pgid) || pgid < 0 || pgid > 0x7fffffff ||
        (pid && pgid)) {
      controlError("signal");
      return;
    }
    if (!engineReady) {
      if (pendingSignals.length < pendingSignalLimit)
        pendingSignals.push({signal: msg.signal, pid, pgid});
      else controlError("signal");
    } else if (pgid) {
      if (exp.waste_wast_raise_signal_pgid(msg.signal, pgid) > 0) wakeIO();
      else controlError("signal");
    } else {
      if (exp.waste_wast_raise_signal_pid(msg.signal, pid) === 0) wakeIO();
      else controlError("signal");
    }
  } else if (msg.type === "resize") {
    if (!Number.isInteger(msg.columns) || !Number.isInteger(msg.rows) ||
        msg.columns < 1 || msg.columns > 65535 || msg.rows < 1 || msg.rows > 65535) {
      controlError("resize");
      return;
    }
    if (!engineReady) {
      pendingResize = {columns: msg.columns, rows: msg.rows};
    } else {
      if (exp.waste_wast_resize_terminal(msg.columns, msg.rows) === 0) wakeIO();
      else controlError("resize");
    }
  } else if (msg.type === "advance-clock") {
    if (!engineReady || !Number.isInteger(msg.low) || msg.low < 0 || msg.low > 0xffffffff ||
        !Number.isInteger(msg.high) || msg.high < 0 || msg.high > 0xffffffff ||
        exp.waste_wast_advance_clock_monotonic_ns(msg.low, msg.high) !== 0) {
      controlError("advance-clock");
    } else wakeIO();
  } else if (msg.type === "set-clock") {
    /* Scripted deterministic clocks for host-independent guest tests.  Values
       are split 32-bit low/high words of a u64 nanosecond value; both zero
       resets the override.  Must arrive before "start"; applied inside run()
       after instantiate so the subsequent engine init picks them up. */
    if (!Number.isInteger(msg.realtimeLow) || !Number.isInteger(msg.realtimeHigh) ||
        !Number.isInteger(msg.monotonicLow) || !Number.isInteger(msg.monotonicHigh)) {
      controlError("set-clock");
    } else {
      pendingClocks = {
        realtimeLow: msg.realtimeLow >>> 0, realtimeHigh: msg.realtimeHigh >>> 0,
        monotonicLow: msg.monotonicLow >>> 0, monotonicHigh: msg.monotonicHigh >>> 0,
      };
    }
  } else if (msg.type === "guest-suite-response") {
    if (!engineReady || msg.id !== guestSuitePending || exp.waste_wast_host_io_kind() !== 3) return;
    guestSuitePending = 0;
    const bytes = new Uint8Array(msg.bytes);
    const ptr = exp.waste_wast_alloc(bytes.length);
    if (!ptr) { exp.waste_wast_host_io_cancel(); wakeIO(); return; }
    new Uint8Array(engineMemory.buffer, ptr, bytes.length).set(bytes);
    const result = exp.waste_wast_test_suite_reply(ptr, bytes.length);
    exp.waste_wast_free(ptr);
    if (result !== 0) exp.waste_wast_host_io_cancel();
    wakeIO();
  } else if (msg.type === "render-test-response") {
    if (!engineReady || !renderTestPending || msg.id !== renderTestPending ||
        exp.waste_wast_host_io_kind() !== 4) return;
    renderTestPending = 0;
    const bytes = new Uint8Array(msg.bytes || []);
    if (bytes.length < 4 || bytes.length > WASTE_CONFIG.RENDER_TEST_REPLY_MAX_BYTES) {
      exp.waste_wast_host_io_cancel();
    } else {
      const ptr = exp.waste_wast_alloc(bytes.length);
      if (!ptr) exp.waste_wast_host_io_cancel();
      else {
        new Uint8Array(engineMemory.buffer, ptr, bytes.length).set(bytes);
        const result = exp.waste_wast_render_test_reply(ptr, bytes.length);
        exp.waste_wast_free(ptr);
        if (result !== 0) exp.waste_wast_host_io_cancel();
      }
    }
    wakeIO();
  } else if (msg.type === "host-upload-response") {
    if (!engineReady || terminated || exp.waste_wast_host_io_kind() !== 1) return;
    if (msg.cancelled) {
      exp.waste_wast_host_io_cancel();
    } else {
      let ptr = 0;
      try {
        const bytes = new Uint8Array(msg.bytes);
        ptr = exp.waste_wast_alloc(Math.max(1, bytes.length));
        if (!ptr) exp.waste_wast_host_io_cancel();
        else {
          new Uint8Array(engineMemory.buffer, ptr, bytes.length).set(bytes);
          if (exp.waste_wast_host_io_provide_upload(ptr, bytes.length) !== 0)
            exp.waste_wast_host_io_cancel();
        }
      } catch (error) {
        exp.waste_wast_host_io_cancel();
      } finally {
        // The engine owns a copy; release the temporary transfer buffer.
        if (ptr) exp.waste_wast_free(ptr);
      }
    }
    wakeIO();
  } else if (msg.type === "cancel") {
    renderTestPending = 0;
    /* Cooperative immediate cancellation.  The engine's pump yield returns
     * control here between opcodes; we flag the store so the next dispatch
     * safepoint reports EXEC_STOP_CANCELLED.  Without a pump quantum this
     * message only takes effect at the next natural yield. */
    if (guestSuitePending) self.postMessage({type: "guest-suite-cancel", id: guestSuitePending});
    guestSuitePending = 0;
    if (exp.waste_wast_request_cancel) exp.waste_wast_request_cancel();
    if (ioResolve) { ioResolve(); ioResolve = null; }
  } else if (msg.type === "stop") {
    renderTestPending = 0;
    if (guestSuitePending) self.postMessage({type: "guest-suite-cancel", id: guestSuitePending});
    guestSuitePending = 0;
    terminated = true;
    if (ioResolve) { ioResolve(); ioResolve = null; }
  }
};
