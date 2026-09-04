#!/usr/bin/env python3
"""Generate an interactive browser test dashboard for C-engine WAST tests.

Runs the native waste-wast runner (--browser-spec mode) to pre-encode WAT modules
and extract assertion specs, then embeds everything in a single offline HTML page
where tests run live in the browser using waste-wast.wasm and Web Workers.
"""

import argparse
import base64
import json
import subprocess
import sys
from pathlib import Path
import datetime


def run_browser_spec(runner: Path, wast_file: Path) -> dict:
    """Run waste-wast --browser-spec on a .wast file and return parsed JSON."""
    try:
        result = subprocess.run(
            [str(runner), "--browser-spec", str(wast_file)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if result.returncode not in (0, 1):
            return {
                "file": wast_file.name,
                "groups": [],
                "error": f"runner exited with status {result.returncode}: {result.stderr.strip()}",
            }
        spec = json.loads(result.stdout)
        if result.returncode != 0 and not spec.get("error"):
            spec["error"] = result.stderr.strip() or "WAST preprocessing failed"
        return spec
    except subprocess.TimeoutExpired:
        return {"file": wast_file.name, "groups": [], "error": "timeout"}
    except json.JSONDecodeError as exc:
        return {"file": wast_file.name, "groups": [], "error": f"invalid JSON: {exc}"}


def script_json(value) -> str:
    return (
        json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        .replace("&", "\\u0026")
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
    )


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
      Each test file gets its own Worker instance. Assertions execute using the WASTE C executor
      with relaxed-SIMD support; results are checked against the spec alternatives.
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

  const FLAT_VALUE_SIZE = 33; /* type(1) + data(16) + nan_mode(16) */

  let batchRunning = false;
  let testAllStartedAt = null;
  let testAllFinishedAt = null;
  let threadCount = 1;
  const results = new Map();

  /* Worker source — runs inside a Blob URL */
  const WORKER_SRC = String.raw`
"use strict";

const FLAT_SIZE = 33;

function packValue(mem, offset, v) {
  mem[offset] = v.type;
  const data = v.data;
  const nm   = v.nan_mode;
  for (let i = 0; i < 16; i++) {
    mem[offset + 1 + i]  = data[i] || 0;
    mem[offset + 17 + i] = nm[i]   || 0;
  }
}

self.onmessage = async function(e) {
  const {wasmBytes, testSpec} = e.data;
  const assertionResults = [];

  try {
    if (testSpec.error) throw new Error(testSpec.error);
    if (!Array.isArray(testSpec.groups) || testSpec.groups.length === 0)
      throw new Error("WAST preprocessing produced no module groups");

    const {instance} = await WebAssembly.instantiate(wasmBytes);
    const exp = instance.exports;

    const getStr = (ptr) => {
      const mem = new Uint8Array(exp.memory.buffer);
      let len = 0;
      while (mem[ptr + len] !== 0 && len < 512) len++;
      return new TextDecoder().decode(mem.subarray(ptr, ptr + len));
    };

    for (const group of testSpec.groups) {
      /* Decode hex module */
      const hex = group.module_hex;
      const modBytes = new Uint8Array(hex.length >> 1);
      for (let i = 0; i < modBytes.length; i++)
        modBytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);

      /* Allocate + write module */
      const modPtr = exp.waste_wast_alloc(modBytes.length);
      new Uint8Array(exp.memory.buffer).set(modBytes, modPtr);

      /* Load module */
      const loadOk = exp.waste_wast_load_module(modPtr, modBytes.length);
      if (loadOk !== 0) {
        const error = getStr(exp.waste_wast_error_ptr()) || "module load failed";
        if (group.assertions.length === 0)
          assertionResults.push({func: "(module)", pass: false, error});
        else for (const a of group.assertions)
          assertionResults.push({func: a.func, pass: false, error});
        continue;
      }

      if (group.assertions.length === 0)
        assertionResults.push({func: "(module)", pass: true, error: ""});

      for (const assertion of group.assertions) {
        const {func, args, alts} = assertion;
        const argCount = args.length;
        const altCount = alts.length;
        const resultCount = altCount > 0 ? alts[0].length : 0;

        /* Allocate name */
        const nameEnc = new TextEncoder().encode(func);
        const namePtr = exp.waste_wast_alloc(nameEnc.length + 1);
        const nameMem = new Uint8Array(exp.memory.buffer);
        nameMem.set(nameEnc, namePtr);
        nameMem[namePtr + nameEnc.length] = 0;

        /* Allocate + write args */
        const argsSize = Math.max(1, argCount * FLAT_SIZE);
        const argsPtr = exp.waste_wast_alloc(argsSize);
        {
          const m = new Uint8Array(exp.memory.buffer);
          for (let i = 0; i < argCount; i++)
            packValue(m, argsPtr + i * FLAT_SIZE, args[i]);
        }

        /* Allocate + write alts */
        const altsSize = Math.max(1, altCount * resultCount * FLAT_SIZE);
        const altsPtr = exp.waste_wast_alloc(altsSize);
        {
          const m = new Uint8Array(exp.memory.buffer);
          for (let a = 0; a < altCount; a++)
            for (let r = 0; r < alts[a].length; r++)
              packValue(m, altsPtr + (a * resultCount + r) * FLAT_SIZE, alts[a][r]);
        }

        /* Run assertion */
        const pass = exp.waste_wast_assert_return(
          namePtr, nameEnc.length,
          argsPtr, argCount,
          altsPtr, altCount,
          resultCount
        ) !== 0;

        let error = "";
        if (!pass) {
          const errPtr = exp.waste_wast_error_ptr();
          error = getStr(errPtr);
        }
        assertionResults.push({func, pass, error});
      }
    }
  } catch (err) {
    self.postMessage({type: "error", error: String(err.stack || err)});
    return;
  }

  self.postMessage({type: "done", file: testSpec.file, results: assertionResults});
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
      /* Count total from spec */
      let total = 0;
      for (const g of (test.spec.groups || [])) total += g.assertions.length;
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
        let total = 0;
        for (const g of (test.spec.groups || [])) total += g.assertions.length;
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
          const passed = assertionResults.length > 0 && assertionResults.every(a => a.pass);
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
  </script>
</body>
</html>
'''


def total_assertions(spec: dict) -> int:
    return sum(len(g.get("assertions", [])) for g in spec.get("groups", []))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate an interactive C engine test dashboard"
    )
    parser.add_argument("--runner", type=Path, required=True,
                        help="Path to the waste-wast native binary")
    parser.add_argument("--wasm", type=Path, required=True,
                        help="Path to waste-wast.wasm (browser C engine)")
    parser.add_argument("--tests", type=Path, required=True, action="append",
                        help="Directory containing .wast test files (repeatable)")
    parser.add_argument("--output", type=Path, required=True,
                        help="Output HTML file path")
    args = parser.parse_args()

    for label, p, kind in [
        ("runner", args.runner, "file"),
        ("wasm",   args.wasm,   "file"),
        *((f"tests[{index}]", path, "dir") for index, path in enumerate(args.tests)),
    ]:
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
        if wast_file.name not in {"spectest-isolation-a.wast", "spectest-isolation-b.wast"}
    ]
    if not wast_files:
        print("error: no .wast files found", file=sys.stderr)
        return 1

    print(f"Building browser specs from {len(wast_files)} test files…")
    tests = []
    for test_dir, wast_file in wast_files:
        spec = run_browser_spec(args.runner, wast_file)
        n_assert = total_assertions(spec)
        print(f"  {wast_file.name}: {n_assert} assertions")
        # Derive group name from test directory name relative to a common root
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
