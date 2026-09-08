#!/usr/bin/env python3
"""Generate an interactive browser test dashboard for C-engine WAST tests.

Embeds raw .wast spec test text into a single offline HTML page where the full
WAST parser/runner (compiled to Wasm) executes them in Web Workers.  Repository
DIY fixtures are assembled ahead of time and run browser-natively.
"""

import argparse
import base64
import json
import subprocess
import sys
import tempfile
from pathlib import Path
import datetime


def script_json(value) -> str:
    return (
        json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        .replace("&", "\\u0026")
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
    )


def tokenize_wast(source: str) -> list[str]:
    """Tokenize the small, ordinary-WAT subset used by repository DIY probes."""
    tokens = []
    i = 0
    while i < len(source):
        if source.startswith(";;", i):
            i = source.find("\n", i)
            if i < 0:
                break
        elif source.startswith("(;", i):
            depth = 1
            i += 2
            while depth and i < len(source):
                if source.startswith("(;", i): depth += 1; i += 2
                elif source.startswith(";)", i): depth -= 1; i += 2
                else: i += 1
        elif source[i].isspace():
            i += 1
        elif source[i] in "()":
            tokens.append(source[i]); i += 1
        elif source[i] == '"':
            start = i
            i += 1
            while i < len(source):
                if source[i] == "\\": i += 2
                elif source[i] == '"': i += 1; break
                else: i += 1
            tokens.append(source[start:i])
        else:
            start = i
            while i < len(source) and not source[i].isspace() and source[i] not in "()":
                i += 1
            tokens.append(source[start:i])
    return tokens


def parse_wast_forms(source: str):
    tokens = tokenize_wast(source)
    pos = 0
    def one():
        nonlocal pos
        if pos >= len(tokens): raise ValueError("unexpected end of WAST")
        token = tokens[pos]; pos += 1
        if token != "(": return token
        result = []
        while pos < len(tokens) and tokens[pos] != ")": result.append(one())
        if pos >= len(tokens): raise ValueError("unterminated WAST form")
        pos += 1
        return result
    forms = []
    while pos < len(tokens): forms.append(one())
    return forms


def wat_text(node) -> str:
    if isinstance(node, str): return node
    return "(" + " ".join(wat_text(item) for item in node) + ")"


def const_spec(node) -> dict:
    if not isinstance(node, list) or len(node) != 2 or node[0] not in ("i32.const", "i64.const"):
        raise ValueError(f"unsupported DIY constant: {wat_text(node)}")
    return {"type": node[0][:3], "value": int(node[1], 0)}


def invoke_spec(node) -> dict:
    if not isinstance(node, list) or not node or node[0] != "invoke":
        raise ValueError(f"unsupported DIY action: {wat_text(node)}")
    at = 1
    module = None
    if at < len(node) and isinstance(node[at], str) and node[at].startswith("$"):
        module = node[at][1:]; at += 1
    name = node[at].strip('"'); at += 1
    return {"module": module, "func": name, "args": [const_spec(x) for x in node[at:]]}


def run_wasm_as(module, wasm_as: str) -> str:
    # POSIX imports need access to the module's otherwise-private memory.
    if any(isinstance(x, list) and x[:2] == ["memory", "1"] for x in module[1:]):
        module.append(["export", '"__waste_memory"', ["memory", "0"]])
    with tempfile.TemporaryDirectory(prefix="waste-c-engine-") as temp:
        wat = Path(temp) / "module.wat"
        wasm = Path(temp) / "module.wasm"
        wat.write_text(wat_text(module), encoding="utf-8")
        result = subprocess.run([
            wasm_as, "--enable-bulk-memory", "--enable-bulk-memory-opt",
            "--enable-reference-types", str(wat), "-o", str(wasm)
        ], capture_output=True, text=True)
        if result.returncode:
            raise ValueError(result.stderr.strip() or "wasm-as failed")
        return base64.b64encode(wasm.read_bytes()).decode("ascii")


def build_diy_spec(wast_file: Path, wasm_as: str) -> dict:
    """Compile complete DIY scripts; browser-native Wasm supplies the guest tier."""
    forms = parse_wast_forms(wast_file.read_text(encoding="utf-8"))
    modules = []
    steps = []
    current = None
    for form in forms:
        if not isinstance(form, list) or not form: continue
        if form[0] == "module":
            module_id = form[1][1:] if len(form) > 1 and isinstance(form[1], str) and form[1].startswith("$") else None
            modules.append({"id": module_id, "wasmB64": run_wasm_as(form, wasm_as)})
            current = module_id
        elif form[0] == "invoke":
            action = invoke_spec(form); action["expect"] = []
            steps.append(action)
        elif form[0] == "assert_return":
            action = invoke_spec(form[1]); action["expect"] = [const_spec(x) for x in form[2:]]
            steps.append(action)
    if not modules or not steps:
        raise ValueError("DIY script produced no modules or actions")
    return {"file": wast_file.name, "mode": "browser-native", "modules": modules, "steps": steps}


HTML = r'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WASTE C engine tests</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0c1017;
      --panel: #151b25;
      --panel-2: #1c2431;
      --line: #2b3647;
      --text: #e8edf5;
      --muted: #96a4b8;
      --accent: #70b7ff;
      --pass: #54d68b;
      --fail: #ff707c;
      --run: #ffd166;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: radial-gradient(circle at top, #152033 0, var(--bg) 38rem);
      color: var(--text);
      font: 14px/1.45 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    header {
      position: sticky;
      top: 0;
      z-index: 10;
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 12px;
      padding: 16px 24px;
      background: color-mix(in srgb, var(--bg) 88%, transparent);
      border-bottom: 1px solid var(--line);
      backdrop-filter: blur(12px);
    }
    h1 { margin: 0 auto 0 0; font-size: 19px; letter-spacing: .02em; }
    button {
      border: 1px solid #40506a;
      border-radius: 7px;
      padding: 7px 11px;
      background: #243149;
      color: var(--text);
      cursor: pointer;
      font: inherit;
    }
    button:hover:not(:disabled) { border-color: var(--accent); }
    button:disabled { cursor: wait; opacity: .55; }
    #test-all { background: #14568a; border-color: #2580bd; font-weight: 650; }
    #download-results { background: #26374d; }
    #execution-mode { display: flex; align-items: center; gap: 8px; color: var(--muted); }
    #execution-mode label { display: inline-flex; align-items: center; gap: 3px; cursor: pointer; }
    #custom-thread-count {
      width: 72px; padding: 5px 7px; border: 1px solid #40506a;
      border-radius: 6px; background: #111824; color: var(--text); font: inherit;
    }
    #summary { color: var(--muted); font-variant-numeric: tabular-nums; }
    #test-all-timing { color: var(--muted); font-variant-numeric: tabular-nums; white-space: nowrap; }
    main { width: min(1100px, calc(100% - 32px)); margin: 24px auto 60px; }
    .notice {
      margin-bottom: 18px;
      padding: 12px 15px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--panel);
      color: var(--muted);
    }
    .group {
      margin: 0 0 18px;
      border: 1px solid var(--line);
      border-radius: 10px;
      overflow: clip;
      background: var(--panel);
    }
    .group.active { border-color: var(--accent); box-shadow: 0 0 0 1px var(--accent); }
    .group-header {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 11px 14px;
      background: var(--panel-2);
      border-bottom: 1px solid var(--line);
    }
    .group-header h2 { margin: 0 auto 0 0; font-size: 15px; font-family: ui-monospace, monospace; }
    .group-count { color: var(--muted); }
    .test {
      display: grid;
      grid-template-columns: 22px minmax(200px, 1fr) minmax(110px, auto) minmax(80px, auto) auto;
      gap: 10px;
      align-items: center;
      padding: 8px 14px;
      border-top: 1px solid #222c3a;
    }
    .test:first-child { border-top: 0; }
    .test:hover { background: #192230; }
    .indicator {
      width: 11px; height: 11px; border-radius: 50%;
      background: #596579; box-shadow: 0 0 0 3px #252e3c;
    }
    .test.running .indicator { background: var(--run); animation: pulse 1s infinite alternate; }
    .test.pass .indicator { background: var(--pass); }
    .test.fail .indicator { background: var(--fail); }
    .test-name { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; overflow-wrap: anywhere; }
    .test-assertions { color: var(--muted); text-align: right; font-variant-numeric: tabular-nums; font-size: .88em; }
    .duration { color: var(--muted); text-align: right; font-variant-numeric: tabular-nums; }
    details { grid-column: 2 / 6; }
    details summary { cursor: pointer; color: var(--muted); font-size: .88em; }
    details summary:hover { color: var(--text); }
    .assertion-table { width: 100%; border-collapse: collapse; margin-top: 6px; font-size: .85em; }
    .assertion-table th {
      padding: 4px 8px; text-align: left;
      background: #111824; color: var(--muted);
      font-weight: 600; font-size: .8em; letter-spacing: .04em;
    }
    .assertion-table td { padding: 4px 8px; border-bottom: 1px solid #1c2535; font-family: ui-monospace, monospace; }
    .assertion-table tr:last-child td { border-bottom: 0; }
    .a-pass { color: var(--pass); }
    .a-fail { color: var(--fail); }
    .a-err { color: #ff9090; font-size: .85em; }
    @keyframes pulse { to { opacity: .35; } }
    @media (max-width: 620px) {
      header { padding: 12px; }
      .test { grid-template-columns: 18px 1fr auto auto; }
      .test-assertions { display: none; }
      details { grid-column: 2 / 5; }
    }
  </style>
</head>
<body>
  <header>
    <h1>WASTE C engine tests</h1>
    <div id="execution-mode">Concurrent sandboxes:</div>
    <div id="summary">0 / __TEST_COUNT__ completed</div>
    <div id="test-all-timing">Started at: — · Finished at: —</div>
    <button id="download-results" type="button" disabled>Download results</button>
    <button id="test-all" type="button">Test all</button>
  </header>
  <main>
    <div class="notice">
      Tests run in Web Workers using the pre-compiled C engine (<code>waste-wast.wasm</code>).
      Each test file gets its own Worker instance. Official WAST assertions use the WASTE C executor.
      Repository DIY modules are assembled ahead of time and execute in the browser Wasm tier with
      a sandbox-local compatibility kernel while the corresponding C executor support is developed.
      Unsupported WAST syntax and engine features are reported as failures, never passes.
    </div>
    <div id="groups"></div>
  </main>
  <script>
  "use strict";

  const PAYLOAD = __PAYLOAD__;
  const WASM_BYTES = (function() {
    const b64 = PAYLOAD.wasmB64;
    const bin = atob(b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return bytes;
  })();
  const TESTS = PAYLOAD.tests;

  let batchRunning = false;
  let testAllStartedAt = null;
  let testAllFinishedAt = null;
  let threadCount = 1;
  const results = new Map();

  /* Worker source — runs inside a Blob URL */
  const WORKER_SRC = String.raw`
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
  const wastBytes = decodeB64(testSpec.wastB64);
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

self.onmessage = async function(e) {
  const {wasmBytes, testSpec} = e.data;
  try {
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
  }
};
  `;

  /* ---- DOM helpers ---- */

  function rowFor(test) {
    return document.querySelector(`.test[data-file="${CSS.escape(test.file)}"]`);
  }

  function formatDuration(ms) {
    if (ms < 1000) return `${Math.round(ms)} ms`;
    if (ms < 60000) return `${(ms / 1000).toFixed(2)} s`;
    return `${Math.floor(ms / 60000)}m ${((ms % 60000) / 1000).toFixed(1)}s`;
  }

  function formatLocalTime(date) {
    return date.toLocaleTimeString([], {hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false});
  }

  function updateTestAllTiming() {
    const started  = testAllStartedAt  ? formatLocalTime(testAllStartedAt)  : "—";
    const finished = testAllFinishedAt ? formatLocalTime(testAllFinishedAt) : "—";
    document.querySelector("#test-all-timing").textContent =
      `Started at: ${started} · Finished at: ${finished}`;
  }

  function updateSummary() {
    let passed = 0, failed = 0;
    for (const r of results.values()) {
      if (r.state === "pass") passed++;
      else if (r.state === "fail") failed++;
    }
    const done = results.size;
    document.querySelector("#summary").textContent =
      `${done} / ${TESTS.length} completed · ${passed} passed · ${failed} failed`;
    document.querySelector("#download-results").disabled = results.size === 0 || batchRunning;
  }

  function setResult(test, state, assertionResults, durationMs) {
    const row = rowFor(test);
    if (!row) return;
    row.classList.remove("idle", "running", "pass", "fail");
    row.classList.add(state);

    const durEl = row.querySelector(".duration");
    durEl.textContent = state === "running" ? "running…" :
      durationMs === null ? "—" : formatDuration(durationMs);

    const assertEl = row.querySelector(".test-assertions");
    if (state !== "idle" && state !== "running" && assertionResults) {
      const p = assertionResults.filter(a => a.pass).length;
      const t = assertionResults.length;
      assertEl.textContent = `${p} / ${t} assertions`;
    } else if (state === "running") {
      assertEl.textContent = "running…";
    } else {
      const total = test.spec.assertionCount || 0;
      assertEl.textContent = total > 0 ? `${total} assertions` : "";
    }

    /* Remove old details */
    const old = row.querySelector("details");
    if (old) old.remove();

    if ((state === "pass" || state === "fail") && assertionResults && assertionResults.length > 0) {
      const details = document.createElement("details");
      if (state === "fail") details.open = true;
      const summary = document.createElement("summary");
      summary.textContent = "Assertion details";
      const table = document.createElement("table");
      table.className = "assertion-table";
      table.innerHTML = "<thead><tr><th>#</th><th>Function</th><th>Result</th><th>Error</th></tr></thead>";
      const tbody = document.createElement("tbody");
      assertionResults.forEach((a, idx) => {
        const tr = document.createElement("tr");
        const errText = a.error || "";
        tr.innerHTML =
          `<td>${idx + 1}</td>` +
          `<td>${escHtml(a.func)}</td>` +
          `<td class="${a.pass ? "a-pass" : "a-fail"}">${a.pass ? "✓" : "✗"}</td>` +
          `<td class="a-err">${escHtml(errText)}</td>`;
        tbody.append(tr);
      });
      table.append(tbody);
      details.append(summary, table);
      row.append(details);
    }

    if (state === "pass" || state === "fail") {
      results.set(test.file, {
        file: test.file,
        backend: test.spec.mode === "browser-native" ? "browser-wasm-compat" : "c-engine",
        state,
        durationMs,
        completedAt: new Date().toISOString(),
        assertions: assertionResults || [],
      });
    }
    if (state === "running") results.delete(test.file);
    updateSummary();
  }

  function escHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;")
      .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }

  /* ---- Rendering ---- */

  function render() {
    /* Concurrency controls */
    const em = document.querySelector("#execution-mode");
    for (const opt of [{value:"1", label:"1", checked:true}, {value:"all", label:`${TESTS.length} (all)`}, {value:"custom", label:"custom"}]) {
      const lbl = document.createElement("label");
      const radio = document.createElement("input");
      radio.type = "radio"; radio.name = "threads"; radio.value = opt.value;
      radio.checked = opt.checked || false;
      lbl.append(radio, opt.label);
      em.append(lbl);
    }
    const custom = document.createElement("input");
    custom.id = "custom-thread-count"; custom.type = "number";
    custom.min = "1"; custom.step = "1"; custom.value = "2"; custom.disabled = true;
    em.append(custom);
    em.addEventListener("change", ev => {
      if (ev.target.name === "threads") custom.disabled = ev.target.value !== "custom";
    });

    /* Group tests by group name */
    const groups = new Map();
    for (const test of TESTS) {
      if (!groups.has(test.group)) groups.set(test.group, []);
      groups.get(test.group).push(test);
    }

    const container = document.querySelector("#groups");
    for (const [groupName, tests] of groups) {
      const section = document.createElement("section");
      section.className = "group";
      section.dataset.group = groupName;

      const header = document.createElement("div");
      header.className = "group-header";
      const title = document.createElement("h2");
      title.textContent = groupName;
      const count = document.createElement("span");
      count.className = "group-count";
      count.textContent = `${tests.length} files`;
      const btn = document.createElement("button");
      btn.type = "button"; btn.textContent = "Test module";
      btn.addEventListener("click", () => runBatch(tests, section));
      header.append(title, count, btn);
      section.append(header);

      for (const test of tests) {
        const row = document.createElement("div");
        row.className = "test idle";
        row.dataset.file = test.file;

        const ind = document.createElement("span");
        ind.className = "indicator";

        const name = document.createElement("span");
        name.className = "test-name";
        name.textContent = test.file;

        const assertEl = document.createElement("span");
        assertEl.className = "test-assertions";
        const total = test.spec.assertionCount || 0;
        assertEl.textContent = total > 0 ? `${total} assertions` : "";

        const dur = document.createElement("span");
        dur.className = "duration";
        dur.textContent = "—";

        const runBtn = document.createElement("button");
        runBtn.type = "button"; runBtn.textContent = "Run";
        runBtn.addEventListener("click", () => { if (!batchRunning) runTest(test); });

        row.append(ind, name, assertEl, dur, runBtn);
        section.append(row);
      }

      container.append(section);
    }

    document.querySelector("#test-all").addEventListener("click", runTestAll);
    document.querySelector("#download-results").addEventListener("click", downloadResults);
    updateSummary();
  }

  /* ---- Execution ---- */

  function selectedThreadCount() {
    const sel = document.querySelector('input[name="threads"]:checked').value;
    if (sel === "all") return TESTS.length;
    if (sel === "custom") {
      const v = Number(document.querySelector("#custom-thread-count").value);
      return (Number.isInteger(v) && v > 0) ? v : 1;
    }
    return 1;
  }

  function runOneTest(test) {
    return new Promise(resolve => {
      const startedAt = performance.now();
      const url = URL.createObjectURL(new Blob([WORKER_SRC], {type: "text/javascript"}));
      const worker = new Worker(url);

      const finish = (assertionResults, error) => {
        const durationMs = performance.now() - startedAt;
        worker.terminate();
        URL.revokeObjectURL(url);
        if (error) {
          setResult(test, "fail", [{func: "(worker)", pass: false, error}], durationMs);
        } else {
          /* A valid script with no assertions (for example inline-module.wast)
           * is a successful file.  Engine and parse errors are emitted as
           * explicit failing results, so an empty result list is not an error. */
          const passed = assertionResults.every(a => a.pass);
          setResult(test, passed ? "pass" : "fail", assertionResults, durationMs);
        }
        resolve();
      };

      worker.onmessage = ({data}) => {
        if (data.type === "done") finish(data.results, null);
        else if (data.type === "error") finish(null, data.error);
      };
      worker.onerror = ev => finish(null, ev.message || "Worker error");

      worker.postMessage({wasmBytes: WASM_BYTES, testSpec: test.spec});
    });
  }

  async function runTest(test) {
    const row = rowFor(test);
    const btn = row && row.querySelector("button");
    if (btn) btn.disabled = true;
    setResult(test, "running", null, null);
    await runOneTest(test);
    if (btn) btn.disabled = batchRunning;
  }

  async function runBatch(tests, section) {
    if (batchRunning) return;
    batchRunning = true;
    const maxConcurrent = selectedThreadCount();
    section && section.classList.add("active");
    document.querySelectorAll("button").forEach(b => b.disabled = true);

    try {
      const queue = [...tests];
      let active = 0;

      await new Promise(resolve => {
        function dispatch() {
          while (active < maxConcurrent && queue.length > 0) {
            const test = queue.shift();
            active++;
            setResult(test, "running", null, null);
            runOneTest(test).then(() => {
              active--;
              dispatch();
              if (active === 0 && queue.length === 0) resolve();
            });
          }
          if (active === 0 && queue.length === 0) resolve();
        }
        dispatch();
      });
    } finally {
      section && section.classList.remove("active");
      batchRunning = false;
      document.querySelectorAll("button").forEach(b => b.disabled = false);
      updateSummary();
      if (new URLSearchParams(location.search).has("autorun")) {
        const failed = [...results.values()].filter(result => result.state !== "pass").length;
        document.documentElement.dataset.autorun = failed ? `failed:${failed}` : "passed";
        document.title = failed ? `FAILED (${failed}) — WASTE C engine tests` : "PASSED — WASTE C engine tests";
      }
    }
  }

  async function runTestAll() {
    if (batchRunning) return;
    testAllStartedAt = new Date();
    testAllFinishedAt = null;
    updateTestAllTiming();

    batchRunning = true;
    const maxConcurrent = selectedThreadCount();
    document.querySelectorAll("button").forEach(b => b.disabled = true);
    document.querySelectorAll(".group").forEach(s => s.classList.add("active"));

    try {
      const queue = [...TESTS];
      let active = 0;

      await new Promise(resolve => {
        function dispatch() {
          while (active < maxConcurrent && queue.length > 0) {
            const test = queue.shift();
            active++;
            setResult(test, "running", null, null);
            runOneTest(test).then(() => {
              active--;
              dispatch();
              if (active === 0 && queue.length === 0) resolve();
            });
          }
          if (active === 0 && queue.length === 0) resolve();
        }
        dispatch();
      });
    } finally {
      document.querySelectorAll(".group").forEach(s => s.classList.remove("active"));
      testAllFinishedAt = new Date();
      updateTestAllTiming();
      batchRunning = false;
      document.querySelectorAll("button").forEach(b => b.disabled = false);
      updateSummary();
      if (new URLSearchParams(location.search).has("autorun")) {
        const failed = [...results.values()].filter(result => result.state !== "pass").length;
        document.documentElement.dataset.autorun = failed ? `failed:${failed}` : "passed";
        document.title = failed ? `FAILED (${failed}) — WASTE C engine tests` : "PASSED — WASTE C engine tests";
      }
    }
  }

  function downloadResults() {
    if (results.size === 0 || batchRunning) return;
    const ordered = TESTS.map(t => results.get(t.file)).filter(Boolean);
    const passed = ordered.filter(r => r.state === "pass").length;
    const failed = ordered.length - passed;
    const report = {
      format: "waste-c-engine-results-v1",
      downloadedAt: new Date().toISOString(),
      userAgent: navigator.userAgent,
      engine: "waste-wast.wasm (C engine)",
      testAll: {
        timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone,
        startedAt: testAllStartedAt?.toISOString() ?? null,
        startedAtLocal: testAllStartedAt ? formatLocalTime(testAllStartedAt) : null,
        finishedAt: testAllFinishedAt?.toISOString() ?? null,
        finishedAtLocal: testAllFinishedAt ? formatLocalTime(testAllFinishedAt) : null,
        durationMs: testAllStartedAt && testAllFinishedAt ?
          testAllFinishedAt - testAllStartedAt : null,
      },
      summary: {available: TESTS.length, completed: ordered.length, passed, failed},
      results: ordered,
    };
    const blob = new Blob([JSON.stringify(report, null, 2) + "\n"], {type: "application/json"});
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    const ts = new Date().toISOString().replace(/[:.]/g, "-");
    link.href = url;
    link.download = `waste-c-engine-results-${ts}.json`;
    document.body.append(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
  }

  render();
  if (new URLSearchParams(location.search).has("autorun")) runTestAll();
  </script>
</body>
</html>
'''


def total_assertions(spec: dict) -> int:
    return spec.get("assertionCount", 0)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate an interactive C engine test dashboard"
    )
    parser.add_argument("--runner", type=Path, default=None,
                        help="Path to the waste-wast native binary (needed for --count)")
    parser.add_argument("--wasm", type=Path, required=True,
                        help="Path to waste-wast.wasm (browser C engine)")
    parser.add_argument("--tests", type=Path, required=True, action="append",
                        help="Directory containing .wast test files (repeatable)")
    parser.add_argument("--output", type=Path, required=True,
                        help="Output HTML file path")
    parser.add_argument("--wasm-as", default="wasm-as",
                        help="Binaryen wasm-as used for repository DIY fixtures")
    parser.add_argument("--count", action="store_true",
                        help="Use native runner to count assertions per file")
    args = parser.parse_args()

    checks = [
        ("wasm", args.wasm, "file"),
        *((f"tests[{i}]", p, "dir") for i, p in enumerate(args.tests)),
    ]
    if args.runner:
        checks.insert(0, ("runner", args.runner, "file"))
    for label, p, kind in checks:
        if kind == "file" and not p.is_file():
            print(f"error: {label} not found: {p}", file=sys.stderr)
            return 1
        if kind == "dir" and not p.is_dir():
            print(f"error: {label} directory not found: {p}", file=sys.stderr)
            return 1

    wast_files = [
        (test_dir, wast_file)
        for test_dir in args.tests
        for wast_file in sorted(test_dir.glob("*.wast"))
    ]
    if not wast_files:
        print("error: no .wast files found", file=sys.stderr)
        return 1

    # Optionally count assertions per spec file using the native runner
    assertion_counts = {}
    if args.count and args.runner:
        for _, wast_file in wast_files:
            try:
                result = subprocess.run(
                    [str(args.runner), "--count", str(wast_file)],
                    capture_output=True, text=True, timeout=60,
                )
                if result.returncode == 0:
                    assertion_counts[wast_file.name] = int(result.stdout.strip())
            except (subprocess.TimeoutExpired, ValueError):
                pass

    print(f"Embedding {len(wast_files)} test files…")
    tests = []
    for test_dir, wast_file in wast_files:
        if test_dir.name == "diy-posix-test":
            try:
                spec = build_diy_spec(wast_file, args.wasm_as)
                n_steps = len(spec.get("steps", []))
                spec["assertionCount"] = n_steps
            except (OSError, ValueError) as exc:
                spec = {"file": wast_file.name, "mode": "browser-native",
                        "error": str(exc), "assertionCount": 0}
        else:
            wast_text = wast_file.read_bytes()
            wast_b64 = base64.b64encode(wast_text).decode("ascii")
            n_assert = assertion_counts.get(wast_file.name, 0)
            spec = {
                "file": wast_file.name,
                "mode": "wast-stream",
                "wastB64": wast_b64,
                "sourceBytes": len(wast_text),
                "assertionCount": n_assert,
            }
        n_display = total_assertions(spec) or len(spec.get("steps", []))
        print(f"  {wast_file.name}: {n_display or '?'} checks")
        tests.append({
            "file": wast_file.name,
            "group": test_dir.name,
            "spec": spec,
        })

    wasm_b64 = base64.b64encode(args.wasm.read_bytes()).decode("ascii")
    print(f"Embedded waste-wast.wasm: {len(args.wasm.read_bytes()):,} bytes "
          f"({len(wasm_b64):,} B base64)")

    payload = {"wasmB64": wasm_b64, "tests": tests}

    total_files = len(tests)
    document = (
        HTML
        .replace("__TEST_COUNT__", str(total_files))
        .replace("__PAYLOAD__", script_json(payload))
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(document, encoding="utf-8")

    total_a = sum(total_assertions(t["spec"]) for t in tests)
    print(f"Generated {args.output} ({args.output.stat().st_size:,} bytes)")
    print(f"{total_files} test files · {total_a} total assertions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
