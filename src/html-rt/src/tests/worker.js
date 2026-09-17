"use strict";

function decodeB64(text) {
  const raw = atob(text), bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
  return bytes;
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

async function runWastScript(wasmBytes, testSpec) {
  const wastBytes = new TextEncoder().encode(testSpec.wastText);
  if (wastBytes.length !== testSpec.sourceBytes) {
    throw new Error("embedded WAST length mismatch: " + wastBytes.length +
                    " != " + testSpec.sourceBytes);
  }
  let engineMemory = null;
  const decoder = new TextDecoder();
  const hostFloat = (ptr, length, asF32) => {
    const bytes = new Uint8Array(engineMemory.buffer, ptr, length);
    return watFloat(decoder.decode(bytes), asF32);
  };
  const imports = {waste_host: {
    strtod: (ptr, length) => hostFloat(ptr, length, false),
    strtof: (ptr, length) => hostFloat(ptr, length, true),
    posix_open: () => -1,
    posix_close: () => 0,
    posix_read: () => 0,
    posix_write: (descriptor, ptr, count) => count,
  }};
  const {instance} = await WebAssembly.instantiate(wasmBytes, imports);
  engineMemory = instance.exports.memory;
  const exp = instance.exports;

  const ptr = exp.waste_wast_alloc(wastBytes.length);
  new Uint8Array(exp.memory.buffer).set(wastBytes, ptr);

  try {
    exp.waste_wast_run_script(ptr, wastBytes.length);
  } catch (error) {
    throw new Error(String(error) + " at WAST line " +
                    exp.waste_wast_command_line());
  }

  const total = exp.waste_wast_results_total();
  const resultsPtr = exp.waste_wast_results_ptr();
  const mem = new Uint8Array(exp.memory.buffer);
  const results = [];

  for (let i = 0; i < total; i++) {
    const base = resultsPtr + i * 256;
    const pass = mem[base] !== 0;
    let funcEnd = 1;
    while (funcEnd < 64 && mem[base + funcEnd] !== 0) funcEnd++;
    const func = decoder.decode(mem.subarray(base + 1, base + funcEnd));
    let errEnd = 64;
    while (errEnd < 256 && mem[base + errEnd] !== 0) errEnd++;
    const error = pass ? "" : decoder.decode(mem.subarray(base + 64, base + errEnd));
    results.push({func, pass, error});
  }
  return results;
}

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
    results.push({
      func: step.func,
      pass,
      error: pass ? "" : "expected " + (expected[0]?.value) + ", got " + actual,
    });
  }
  return results;
}

let runtimePaused = false;
let pendingRuntimeSignal = 0;
let workerRunning = false;

async function workerControlPoint() {
  while (runtimePaused)
    await new Promise(resolve => setTimeout(resolve, 10));
}

self.onmessage = async function(e) {
  if (e.data.type === "control") {
    if (e.data.operation === 1) runtimePaused = true;
    else if (e.data.operation === 0) runtimePaused = false;
    else if (e.data.operation === 2) pendingRuntimeSignal = e.data.argument;
    return;
  }
  if (workerRunning) return;
  workerRunning = true;
  const {wasmBytes, testSpec} = e.data;
  try {
    await workerControlPoint();
    if (testSpec.error) throw new Error(testSpec.error);
    let results;
    if (testSpec.mode === "browser-native") {
      results = await runBrowserNative(testSpec);
    } else if (testSpec.mode === "wast-stream") {
      results = await runWastScript(wasmBytes, testSpec);
    } else {
      throw new Error("unsupported test mode: " + testSpec.mode);
    }
    self.postMessage({type: "done", file: testSpec.file, results});
  } catch (err) {
    self.postMessage({type: "error", error: String(err.stack || err)});
  } finally {
    workerRunning = false;
  }
};
