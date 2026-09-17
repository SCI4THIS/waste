"use strict";

var WASM_BYTES = null;
var ALL_TESTS = [];
var TESTS = [];

let batchRunning = false;
let batchPaused = false;
let resumeBatchDispatch = null;
let testAllStartedAt = null;
let testAllFinishedAt = null;
let threadCount = 1;
const results = new Map();
const activeWorkers = new Set();

/* ---- DOM helpers ---- */

function rowFor(test) {
  return document.querySelector(`.test[data-path="${CSS.escape(test.path)}"]`);
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
  const started  = testAllStartedAt  ? formatLocalTime(testAllStartedAt)  : "\u2014";
  const finished = testAllFinishedAt ? formatLocalTime(testAllFinishedAt) : "\u2014";
  document.querySelector("#test-all-timing").textContent =
    `Started at: ${started} \u00b7 Finished at: ${finished}`;
}

function updateSummary() {
  let passed = 0, failed = 0;
  const supportedResults = [...results.values()].filter(result => !result.unsupported);
  for (const r of supportedResults) {
    if (r.state === "pass") passed++;
    else if (r.state === "fail") failed++;
  }
  const done = supportedResults.length;
  document.querySelector("#summary").textContent =
    `${done} / ${TESTS.length} supported reported \u00b7 ${passed} passed \u00b7 ${failed} failed \u00b7 ${ALL_TESTS.length - TESTS.length} unsupported excluded`;
  document.querySelector("#download-results").disabled = results.size === 0 || batchRunning;
}

function setResult(test, state, assertionResults, durationMs) {
  const row = rowFor(test);
  if (!row) return;
  row.classList.remove("idle", "running", "pass", "fail");
  row.classList.add(state);

  const durEl = row.querySelector(".duration");
  durEl.textContent = state === "running" ? "running\u2026" :
    durationMs === null ? "\u2014" : formatDuration(durationMs);

  const assertEl = row.querySelector(".test-assertions");
  if (assertEl && state !== "idle" && state !== "running" && assertionResults) {
    const p = assertionResults.filter(a => a.pass).length;
    const t = assertionResults.length;
    assertEl.textContent = `${p} / ${t} assertions`;
  } else if (assertEl && state === "running") {
    assertEl.textContent = "running\u2026";
  } else if (assertEl) {
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
        `<td class="${a.pass ? "a-pass" : "a-fail"}">${a.pass ? "\u2713" : "\u2717"}</td>` +
        `<td class="a-err">${escHtml(errText)}</td>`;
      tbody.append(tr);
    });
    table.append(tbody);
    details.append(summary, table);
    row.append(details);
  }

  if (state === "pass" || state === "fail") {
    results.set(test.path, {
      path: test.path,
      name: test.name,
      group: test.group,
      suite: test.suite,
      unsupported: test.unsupported,
      unsupportedReason: test.unsupportedReason,
      file: test.file,
      backend: test.spec.mode === "browser-native" ? "browser-wasm-compat" : "c-engine",
      state,
      durationMs,
      completedAt: new Date().toISOString(),
      assertions: assertionResults || [],
    });
  }
  if (state === "running") results.delete(test.path);
  updateSummary();
}

function escHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;")
    .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function sendControl(worker, operation, argument) {
  if (argument === undefined) argument = 0;
  worker.postMessage({type: "control", operation, argument});
}

function pauseRuntimes() {
  batchPaused = true;
  for (const worker of activeWorkers) sendControl(worker, 1);
  document.querySelector("#control-status").textContent =
    `paused scheduling; ${activeWorkers.size} active runtime(s) finishing their current C call`;
}

function resumeRuntimes() {
  batchPaused = false;
  for (const worker of activeWorkers) sendControl(worker, 0);
  document.querySelector("#control-status").textContent =
    `running ${activeWorkers.size} active runtime(s)`;
  if (resumeBatchDispatch) resumeBatchDispatch();
}

function signalRuntimes() {
  const signal = Number(document.querySelector("#signal-number").value);
  for (const worker of activeWorkers) sendControl(worker, 2, signal);
  const name = document.querySelector("#signal-number").selectedOptions[0].textContent;
  document.querySelector("#control-status").textContent =
    `${name} queued for ${activeWorkers.size} active runtime(s)`;
}

/* ---- Rendering ---- */

function render() {
  /* Concurrency controls */
  const em = document.querySelector("#execution-mode");
  var opts = [{value:"1", label:"1", checked:true}, {value:"all", label:TESTS.length + " (#tests)"}, {value:"custom", label:"custom"}];
  for (var oi = 0; oi < opts.length; oi++) {
    var opt = opts[oi];
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
  em.addEventListener("change", function(ev) {
    if (ev.target.name === "threads") custom.disabled = ev.target.value !== "custom";
  });

  /* Group tests by group name */
  const groups = new Map();
  for (const test of ALL_TESTS) {
    if (!groups.has(test.group)) groups.set(test.group, []);
    groups.get(test.group).push(test);
  }

  const container = document.querySelector("#groups");
  for (const [groupName, tests] of groups) {
    const section = document.createElement("section");
    section.className = "group";
    section.dataset.group = groupName;
    const unsupportedGroup = tests.every(function(test) { return test.unsupported; });
    if (unsupportedGroup) section.classList.add("unsupported");

    const header = document.createElement("div");
    header.className = "group-header";
    const title = document.createElement("h2");
    title.textContent = groupName;
    const count = document.createElement("span");
    count.className = "group-count";
    count.textContent = unsupportedGroup ?
      `${tests.length} tests \u00b7 unsupported` : `${tests.length} tests`;
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = unsupportedGroup ? "Test module anyway" : "Test module";
    btn.addEventListener("click", function() { runBatch(tests, section); });
    header.append(title, count, btn);
    section.append(header);

    for (const test of tests) {
      const row = document.createElement("div");
      row.className = "test idle" + (test.unsupported ? " unsupported" : "");
      row.dataset.path = test.path;

      const ind = document.createElement("span");
      ind.className = "indicator";

      const name = document.createElement("span");
      name.className = "test-path";
      name.textContent = test.name;
      if (test.expectFailure) {
        const expected = document.createElement("span");
        expected.className = "expected";
        expected.textContent = "expected rejection";
        name.append(expected);
      }
      if (test.unsupported) {
        const unsupported = document.createElement("span");
        unsupported.className = "unsupported-label";
        unsupported.textContent = "unsupported legacy module";
        unsupported.title = test.unsupportedReason;
        name.append(unsupported);
      }

      const dur = document.createElement("span");
      dur.className = "duration";
      dur.textContent = "\u2014";

      const assertCount = document.createElement("span");
      assertCount.className = "test-assertions";
      const total = test.spec.assertionCount || 0;
      assertCount.textContent = total > 0 ? `${total} assertions` : "";

      const runBtn = document.createElement("button");
      runBtn.type = "button"; runBtn.textContent = "Run";
      runBtn.addEventListener("click", function() { if (!batchRunning) runTest(test); });

      row.append(ind, name, assertCount, dur, runBtn);
      section.append(row);
    }

    container.append(section);
  }

  document.querySelector("#test-all").addEventListener("click", runTestAll);
  document.querySelector("#download-results").addEventListener("click", downloadResults);
  document.querySelector("#pause-runtime").addEventListener("click", pauseRuntimes);
  document.querySelector("#resume-runtime").addEventListener("click", resumeRuntimes);
  document.querySelector("#send-signal").addEventListener("click", signalRuntimes);
  document.querySelector("#control-status").textContent = "ready";
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

async function runOneTest(test) {
  const startedAt = performance.now();
  const worker = await createWorker("worker.js");
  activeWorkers.add(worker);
  let finished = false;

  /* Load WAST source on demand for wast-stream tests */
  let testSpec = test.spec;
  if (testSpec.mode === "wast-stream" && !testSpec.wastText) {
    /* staging: fetch from the original source via sourcePath;
       production: look up wast/<relative> in the tar_hash. */
    const wastUrl = g.is_staging
      ? "../../../../" + testSpec.sourcePath
      : "wast/" + test.file;
    const wastText = await loadText(wastUrl);
    testSpec = Object.assign({}, testSpec, {wastText: wastText});
  }

  return new Promise(function(resolve) {
    function finish(assertionResults, error) {
      if (finished) return;
      finished = true;
      const durationMs = performance.now() - startedAt;
      worker.terminate();
      activeWorkers.delete(worker);
      if (error) {
        setResult(test, "fail", [{func: "(worker)", pass: false, error: error}], durationMs);
      } else {
        const passed = assertionResults.every(function(a) { return a.pass; });
        setResult(test, passed ? "pass" : "fail", assertionResults, durationMs);
      }
      resolve();
    }

    worker.onmessage = function(ev) {
      if (ev.data.type === "done") finish(ev.data.results, null);
      else if (ev.data.type === "error") finish(null, ev.data.error);
    };
    worker.onerror = function(ev) { finish(null, ev.message || "Worker error"); };

    worker.postMessage({wasmBytes: WASM_BYTES, testSpec: testSpec});
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

async function runConcurrentBatch(tests, maxConcurrent) {
  const queue = tests.slice();
  let active = 0;
  try {
    await new Promise(function(resolve) {
      function dispatch() {
        if (batchPaused) return;
        while (active < maxConcurrent && queue.length > 0) {
          const test = queue.shift();
          active++;
          setResult(test, "running", null, null);
          runOneTest(test).then(function() {
            active--;
            dispatch();
            if (active === 0 && queue.length === 0) resolve();
          });
        }
        if (active === 0 && queue.length === 0) resolve();
      }
      resumeBatchDispatch = dispatch;
      dispatch();
    });
  } finally {
    resumeBatchDispatch = null;
  }
}

async function runBatch(tests, section) {
  if (batchRunning) return;
  batchRunning = true;
  const maxConcurrent = selectedThreadCount();
  if (section) section.classList.add("active");
  document.querySelectorAll("button:not(.runtime-control)").forEach(function(b) { b.disabled = true; });

  try {
    await runConcurrentBatch(tests, maxConcurrent);
  } finally {
    if (section) section.classList.remove("active");
    batchRunning = false;
    document.querySelectorAll("button:not(.runtime-control)").forEach(function(b) { b.disabled = false; });
    updateSummary();
    if (new URLSearchParams(location.search).has("autorun")) {
      const failed = [...results.values()].filter(function(result) { return result.state !== "pass"; }).length;
      document.documentElement.dataset.autorun = failed ? "failed:" + failed : "passed";
      document.title = failed ? "FAILED (" + failed + ") \u2014 WASTE C engine tests" : "PASSED \u2014 WASTE C engine tests";
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
  document.querySelectorAll("button:not(.runtime-control)").forEach(function(b) { b.disabled = true; });
  document.querySelectorAll(".group:not(.unsupported)").forEach(function(s) { s.classList.add("active"); });

  try {
    await runConcurrentBatch(TESTS, maxConcurrent);
  } finally {
    document.querySelectorAll(".group").forEach(function(s) { s.classList.remove("active"); });
    testAllFinishedAt = new Date();
    updateTestAllTiming();
    batchRunning = false;
    document.querySelectorAll("button:not(.runtime-control)").forEach(function(b) { b.disabled = false; });
    updateSummary();
    if (new URLSearchParams(location.search).has("autorun")) {
      const failed = [...results.values()].filter(function(result) { return result.state !== "pass"; }).length;
      document.documentElement.dataset.autorun = failed ? "failed:" + failed : "passed";
      document.title = failed ? "FAILED (" + failed + ") \u2014 WASTE C engine tests" : "PASSED \u2014 WASTE C engine tests";
    }
  }
}

function downloadResults() {
  if (results.size === 0 || batchRunning) return;
  const ordered = ALL_TESTS.map(function(t) { return results.get(t.path); }).filter(Boolean);
  const supported = ordered.filter(function(result) { return !result.unsupported; });
  const passed = supported.filter(function(r) { return r.state === "pass"; }).length;
  const failed = supported.length - passed;
  const report = {
    format: "waste-c-engine-results-v1",
    downloadedAt: new Date().toISOString(),
    userAgent: navigator.userAgent,
    engine: "waste-wast.wasm (C engine)",
    testAll: {
      timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone,
      startedAt: testAllStartedAt ? testAllStartedAt.toISOString() : null,
      startedAtLocal: testAllStartedAt ? formatLocalTime(testAllStartedAt) : null,
      finishedAt: testAllFinishedAt ? testAllFinishedAt.toISOString() : null,
      finishedAtLocal: testAllFinishedAt ? formatLocalTime(testAllFinishedAt) : null,
      durationMs: testAllStartedAt && testAllFinishedAt ?
        testAllFinishedAt - testAllStartedAt : null,
    },
    summary: {available: ALL_TESTS.length, supported: TESTS.length,
              unsupported: ALL_TESTS.length - TESTS.length,
              completed: ordered.length,
              completedSupported: supported.length, passed: passed, failed: failed},
    results: ordered,
  };
  const blob = new Blob([JSON.stringify(report, null, 2) + "\n"], {type: "application/json"});
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  const ts = new Date().toISOString().replace(/[:.]/g, "-");
  link.href = url;
  link.download = "waste-c-engine-results-" + ts + ".json";
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

async function startTests() {
  const payload = await loadJSON("payload.json");
  WASM_BYTES = new Uint8Array(await loadBinary("waste-wast.wasm"));
  ALL_TESTS = payload.tests;
  TESTS = ALL_TESTS.filter(function(test) { return !test.unsupported; });

  render();

  /* Remove loading overlay — everything is inflated and ready */
  var overlay = document.getElementById("loading-overlay");
  if (overlay) overlay.remove();

  if (new URLSearchParams(location.search).has("autorun")) runTestAll();
}
