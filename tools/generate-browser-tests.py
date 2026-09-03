#!/usr/bin/env python3

import argparse
import base64
import json
from pathlib import Path


def browser_loader(source: str) -> str:
    argv_original = 'argv:()=>g?globalThis.process.argv.slice(1):["a.out"]'
    argv_browser = 'argv:()=>g?globalThis.process.argv.slice(1):(globalThis.waste_argv||["a.out"])'
    exit_original = 'exit:a=>g&&globalThis.process.exit(a)'
    exit_browser = (
        'exit:a=>{if(g)return globalThis.process.exit(a);'
        'globalThis.waste_exit_code=a}'
    )

    if argv_original not in source:
        raise RuntimeError("generated loader's browser argv hook was not recognized")
    if exit_original not in source:
        raise RuntimeError("generated loader's browser exit hook was not recognized")
    return source.replace(argv_original, argv_browser).replace(exit_original, exit_browser)


def script_json(value) -> str:
    return (
        json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        .replace("&", "\\u0026")
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
    )


def collect_tests(test_root: Path, suite: str, path_prefix: str = ""):
    tests = []
    for path in sorted(test_root.rglob("*.wast")):
        relative_path = path.relative_to(test_root)
        if "_output" in relative_path.parts:
            continue
        local_relative = relative_path.as_posix()
        relative = "/".join(part for part in (path_prefix, local_relative) if part)
        parent = path.parent.relative_to(test_root).as_posix()
        group = "/".join(
            part for part in (path_prefix, parent if parent != "." else "") if part
        ) or "root"
        unsupported = suite == "wasm-spec" and local_relative.startswith("legacy/")
        tests.append(
            {
                "path": relative,
                "name": path.name,
                "group": group,
                "suite": suite,
                "expectFailure": ".fail." in path.name,
                "unsupported": unsupported,
                "unsupportedReason": (
                    "Legacy exception syntax is not supported by the current WebAssembly 3.0 interpreter"
                    if unsupported
                    else None
                ),
                "source": path.read_text(encoding="utf-8"),
            }
        )
    return tests


HTML = r'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WASTE · WebAssembly and POSIX tests</title>
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
      --unsupported: #f0a43a;
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
    #runtime-controls { display: flex; gap: 6px; align-items: center; }
    #runtime-controls select {
      border: 1px solid #40506a; border-radius: 6px; padding: 6px;
      background: #111824; color: var(--text); font: inherit;
    }
    #summary { min-width: 235px; color: var(--muted); font-variant-numeric: tabular-nums; }
    #execution-mode { color: var(--muted); font-variant-numeric: tabular-nums; white-space: nowrap; }
    #quantum {
      width: 92px;
      border: 1px solid #40506a;
      border-radius: 6px;
      padding: 6px 8px;
      background: #111824;
      color: var(--text);
      font: inherit;
    }
    #execution-mode { display: flex; align-items: center; gap: 8px; }
    #execution-mode label { display: inline-flex; align-items: center; gap: 3px; }
    #custom-thread-count {
      width: 72px; padding: 5px 7px; border: 1px solid #40506a;
      border-radius: 6px; background: #111824; color: var(--text);
    }
    #test-all-timing { color: var(--muted); font-variant-numeric: tabular-nums; white-space: nowrap; }
    main { width: min(1180px, calc(100% - 32px)); margin: 24px auto 60px; }
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
    .group.unsupported { border-color: #875a22; }
    .group.unsupported .group-header { background: #372817; }
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
    .test { display: grid; grid-template-columns: 22px minmax(240px, 1fr) minmax(72px, auto) auto; gap: 10px; align-items: center; padding: 8px 14px; border-top: 1px solid #222c3a; }
    .test:first-child { border-top: 0; }
    .test:hover { background: #192230; }
    .indicator { width: 11px; height: 11px; border-radius: 50%; background: #596579; box-shadow: 0 0 0 3px #252e3c; }
    .test.running .indicator { background: var(--run); animation: pulse 1s infinite alternate; }
    .test.pass .indicator { background: var(--pass); }
    .test.fail .indicator { background: var(--fail); }
    .test.unsupported { background: #211a12; }
    .test.unsupported .indicator { background: var(--unsupported); box-shadow: 0 0 0 3px #3b2b18; }
    .test-path { min-width: 0; overflow-wrap: anywhere; font-family: ui-monospace, SFMono-Regular, Consolas, monospace; }
    .duration { color: var(--muted); text-align: right; font-variant-numeric: tabular-nums; }
    .expected { margin-left: 8px; color: var(--muted); font: 11px system-ui, sans-serif; }
    .unsupported-label { margin-left: 8px; color: var(--unsupported); font: 11px system-ui, sans-serif; font-weight: 700; }
    details { grid-column: 2 / 5; color: var(--muted); }
    pre { max-height: 280px; overflow: auto; padding: 10px; border-radius: 6px; background: #090c12; color: #cbd5e3; white-space: pre-wrap; }
    @keyframes pulse { to { opacity: .35; } }
    @media (max-width: 620px) {
      header { padding: 12px; }
      main { width: calc(100% - 16px); margin-top: 12px; }
      .test { grid-template-columns: 18px 1fr auto; }
      .test-path { grid-column: 2 / 4; }
      .duration { grid-column: 2; grid-row: 2; text-align: left; }
      .test button { grid-column: 3; grid-row: 2; }
      details { grid-column: 2 / 4; }
    }
  </style>
</head>
<body>
  <header>
    <h1>WASTE · embedded WebAssembly and POSIX tests</h1>
    <div id="execution-mode"></div>
    <div id="summary">0 / __TEST_COUNT__ completed</div>
    <div id="test-all-timing">Started at: — · Finished at: —</div>
    <div id="runtime-controls">
      <button class="runtime-control" id="pause-runtime" type="button">Pause</button>
      <button class="runtime-control" id="resume-runtime" type="button">Resume</button>
      <select class="runtime-control" id="signal-number" title="POSIX signal number">
        <option value="2">SIGINT</option><option value="15">SIGTERM</option>
        <option value="1">SIGHUP</option><option value="14">SIGALRM</option>
        <option value="28">SIGWINCH</option>
      </select>
      <button class="runtime-control" id="send-signal" type="button">Send signal</button>
      <span id="control-status"></span>
    </div>
    <button id="download-results" type="button" disabled>Download results</button>
    <button id="test-all" type="button">Test all</button>
  </header>
  <main>
    <div class="notice">
      __EXECUTION_NOTICE__
      Expected <code>.fail.wast</code> rejections count as passes. __TIMEOUT_NOTICE__
      Orange legacy modules are unsupported by the current interpreter and are excluded from Test all.
    </div>
    <div id="groups"></div>
  </main>
  <script>
    "use strict";
    const PAYLOAD = __PAYLOAD__;
    const SUPPORTED_TESTS = PAYLOAD.tests.filter(test => !test.unsupported);
    const DEFAULT_QUANTUM = __DEFAULT_QUANTUM__;
    const TIMEOUT_MS = 30 * 60 * 1000;
    const results = new Map();
    const activeWorkers = new Set();
    let batchRunning = false;
    let testAllStartedAt = null;
    let testAllFinishedAt = null;
    let threadCount = 1;
    let lastRunThreadCount = 1;
    let lastRunQuantum = DEFAULT_QUANTUM;

    const workerProgram = String.raw`
      "use strict";
      let running = false;
      const queueControl = (operation, argument = 0) => {
        const page = globalThis.waste_control_page;
        if (!(page instanceof Int32Array)) return;
        const sequence = page[0] + 1;
        page[4 + (sequence & 255)] = (operation << 16) | (argument & 0xffff);
        page[0] = sequence;
      };
      self.onmessage = async ({data}) => {
        if (data.type === "control") {
          if (data.operation === 1) globalThis.waste_runtime_paused = true;
          else if (data.operation === 0) globalThis.waste_runtime_paused = false;
          else queueControl(data.operation, data.argument);
          return;
        }
        if (running) return;
        running = true;
        const output = [];
        const taskOutput = data.tests ? data.tests.map(() => []) : null;
        let activeTask = null;
        const format = value => {
          if (typeof value === "string") return value;
          try { return JSON.stringify(value); } catch (_) { return String(value); }
        };
        const capture = values => {
          const message = values.map(format).join(" ").trimEnd();
          const task = /^WASTE_TASK (\d+)$/.exec(message);
          if (task) {
            activeTask = Number(task[1]);
            return;
          }
          const result = /^WASTE_RESULT (\d+) ([01])$/.exec(message);
          if (result) {
            const index = Number(result[1]);
            self.postMessage({
              type: "result",
              index,
              path: data.tests[index].path,
              ok: result[2] === "0",
              output: taskOutput[index].join("\n")
            });
            return;
          }
          const done = /^WASTE_DONE ([01])$/.exec(message);
          if (done) {
            self.postMessage({
              type: "done",
              ok: done[1] === "0",
              output: output.join("\n"),
              exitCode: done[1] === "0" ? 0 : 1
            });
            return;
          }
          if (taskOutput && activeTask !== null) taskOutput[activeTask].push(message);
          else output.push(message);
        };
        console.log = (...values) => capture(values);
        console.error = (...values) => capture(values);
        globalThis.waste_control_page = new Int32Array(4 + 256);
        globalThis.waste_control_page[3] = 1;
        globalThis.waste_runtime_paused = false;
        const deferred = [];
        const deferChannel = new MessageChannel();
        deferChannel.port1.onmessage = () => deferred.shift()?.();
        globalThis.waste_defer = (callback, milliseconds) => {
          const resume = () => {
            if (globalThis.waste_runtime_paused)
              globalThis.setTimeout(resume, 10);
            else {
              try {
                callback();
              } catch (error) {
                self.postMessage({
                  type: "done",
                  ok: false,
                  output: output.join("\n"),
                  error: error && (error.stack || error.message) || String(error),
                  exitCode: error && error.wasteExitCode
                });
              }
            }
          };
          if (milliseconds > 0) globalThis.setTimeout(resume, milliseconds);
          else {
            deferred.push(resume);
            deferChannel.port2.postMessage(0);
          }
        };
        if (data.tests) {
          globalThis.waste_argv = ["wasm", "-ca", "--schedule", "--browser-schedule", "--control-page"];
          if (data.failLast) globalThis.waste_argv.push("--fail-last");
          globalThis.waste_argv.push("-q", String(data.quantum), "--threads", String(data.threadCount));
          for (const test of data.tests) globalThis.waste_argv.push("-e", test.source);
        } else {
          globalThis.waste_argv = ["wasm", "-ca", "-e", data.source];
        }
        globalThis.waste_exit_code = 0;
        const binary = atob(data.wasm);
        const bytes = Uint8Array.from(binary, char => char.charCodeAt(0));
        globalThis.fetch = async () => new Response(bytes, {
          headers: {"Content-Type": "application/wasm"}
        });
        try {
          const completion = (0, eval)(data.loader);
          if (!completion || typeof completion.then !== "function") {
            throw new Error("Generated loader did not return a completion promise");
          }
          await completion;
          if (!data.tests) self.postMessage({
              type: "done",
              ok: globalThis.waste_exit_code === 0,
              output: output.join("\n"),
              exitCode: globalThis.waste_exit_code
            });
        } catch (error) {
          self.postMessage({
            type: "done",
            ok: false,
            output: output.join("\n"),
            error: error && (error.stack || error.message) || String(error),
            exitCode: error && error.wasteExitCode
          });
        }
      };
    `;

    function groupedTests() {
      const groups = new Map();
      for (const test of PAYLOAD.tests) {
        if (!groups.has(test.group)) groups.set(test.group, []);
        groups.get(test.group).push(test);
      }
      return groups;
    }

    function sendControl(worker, operation, argument = 0) {
      worker.postMessage({type: "control", operation, argument});
    }

    function pauseRuntimes() {
      for (const worker of activeWorkers) sendControl(worker, 1);
      document.querySelector("#control-status").textContent =
        `paused ${activeWorkers.size} active runtime(s)`;
    }

    function resumeRuntimes() {
      for (const worker of activeWorkers) sendControl(worker, 0);
      document.querySelector("#control-status").textContent =
        `running ${activeWorkers.size} active runtime(s)`;
    }

    function signalRuntimes() {
      const signal = Number(document.querySelector("#signal-number").value);
      for (const worker of activeWorkers) sendControl(worker, 2, signal);
      const name = document.querySelector("#signal-number").selectedOptions[0].textContent;
      document.querySelector("#control-status").textContent =
        `${name} queued for ${activeWorkers.size} active runtime(s)`;
    }

    function selectedThreadCount() {
      const selected = document.querySelector('input[name="threads"]:checked').value;
      if (selected === "all") threadCount = SUPPORTED_TESTS.length;
      else if (selected === "custom") {
        const custom = document.querySelector("#custom-thread-count");
        const value = Number(custom.value);
        if (!Number.isInteger(value) || value <= 0) {
          custom.setCustomValidity("Thread count must be an integer greater than zero.");
          custom.reportValidity();
          return null;
        }
        custom.setCustomValidity("");
        threadCount = value;
      } else threadCount = 1;
      return threadCount;
    }

    function selectedQuantum() {
      return Math.max(1, Number(document.querySelector("#quantum").value) || DEFAULT_QUANTUM);
    }

    function render() {
      const executionMode = document.querySelector("#execution-mode");
      executionMode.append("Concurrent sandboxes:");
      for (const option of [
        {value: "1", label: "1", checked: true},
        {value: "all", label: `${SUPPORTED_TESTS.length} (#tests)`},
        {value: "custom", label: "custom"}
      ]) {
        const label = document.createElement("label");
        const radio = document.createElement("input");
        radio.type = "radio";
        radio.name = "threads";
        radio.value = option.value;
        radio.checked = option.checked || false;
        label.append(radio, option.label);
        executionMode.append(label);
      }
      const custom = document.createElement("input");
      custom.id = "custom-thread-count";
      custom.type = "number";
      custom.min = "1";
      custom.step = "1";
      custom.value = "2";
      custom.disabled = true;
      custom.title = "Maximum number of simultaneously runnable test sandboxes";
      executionMode.append(custom, "· quantum:");
      const quantum = document.createElement("input");
      quantum.id = "quantum";
      quantum.type = "number";
      quantum.min = "1";
      quantum.step = "1000";
      quantum.value = String(DEFAULT_QUANTUM);
      quantum.title = "Interpreter steps executed by a test before switching";
      executionMode.append(quantum, "steps");
      executionMode.addEventListener("change", event => {
        if (event.target.name === "threads") {
          custom.disabled = event.target.value !== "custom";
          selectedThreadCount();
        }
      });
      const container = document.querySelector("#groups");
      for (const [groupName, tests] of groupedTests()) {
        const section = document.createElement("section");
        section.className = "group";
        section.dataset.group = groupName;
        const unsupportedGroup = tests.every(test => test.unsupported);
        if (unsupportedGroup) section.classList.add("unsupported");
        const header = document.createElement("div");
        header.className = "group-header";
        const title = document.createElement("h2");
        title.textContent = groupName;
        const count = document.createElement("span");
        count.className = "group-count";
        count.textContent = unsupportedGroup ?
          `${tests.length} tests · unsupported` : `${tests.length} tests`;
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = unsupportedGroup ? "Test module anyway" : "Test module";
        button.addEventListener("click", () => runBatch(tests));
        header.append(title, count, button);
        section.append(header);

        const list = document.createElement("div");
        for (const test of tests) {
          const row = document.createElement("div");
          row.className = `test idle${test.unsupported ? " unsupported" : ""}`;
          row.dataset.path = test.path;
          const indicator = document.createElement("span");
          indicator.className = "indicator";
          const path = document.createElement("span");
          path.className = "test-path";
          path.textContent = test.name;
          if (test.expectFailure) {
            const expected = document.createElement("span");
            expected.className = "expected";
            expected.textContent = "expected rejection";
            path.append(expected);
          }
          if (test.unsupported) {
            const unsupported = document.createElement("span");
            unsupported.className = "unsupported-label";
            unsupported.textContent = "unsupported legacy module";
            unsupported.title = test.unsupportedReason;
            path.append(unsupported);
          }
          const duration = document.createElement("span");
          duration.className = "duration";
          duration.textContent = "—";
          const run = document.createElement("button");
          run.type = "button";
          run.textContent = "Run";
          run.addEventListener("click", () => runTest(test));
          row.append(indicator, path, duration, run);
          list.append(row);
        }
        section.append(list);
        container.append(section);
      }
      document.querySelector("#test-all").addEventListener("click", runTestAll);
      document.querySelector("#download-results").addEventListener("click", downloadResults);
      document.querySelector("#pause-runtime").addEventListener("click", pauseRuntimes);
      document.querySelector("#resume-runtime").addEventListener("click", resumeRuntimes);
      document.querySelector("#send-signal").addEventListener("click", signalRuntimes);
      document.querySelector("#control-status").textContent =
        "ready (static worker messaging)";
      updateSummary();
    }

    function rowFor(test) {
      return [...document.querySelectorAll(".test")].find(row => row.dataset.path === test.path);
    }

    function groupFor(groupName) {
      return [...document.querySelectorAll(".group")]
        .find(section => section.dataset.group === groupName);
    }

    function formatDuration(milliseconds) {
      if (milliseconds < 1000) return `${Math.round(milliseconds)} ms`;
      if (milliseconds < 60000) return `${(milliseconds / 1000).toFixed(2)} s`;
      const minutes = Math.floor(milliseconds / 60000);
      const seconds = ((milliseconds % 60000) / 1000).toFixed(1);
      return `${minutes}m ${seconds}s`;
    }

    function formatLocalTime(date) {
      return date.toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        hour12: false
      });
    }

    function updateTestAllTiming() {
      const started = testAllStartedAt ? formatLocalTime(testAllStartedAt) : "—";
      const finished = testAllFinishedAt ? formatLocalTime(testAllFinishedAt) : "—";
      document.querySelector("#test-all-timing").textContent =
        `Started at: ${started} · Finished at: ${finished}`;
    }

    function setResult(test, state, detail = "", durationMs = null, record = {}) {
      const row = rowFor(test);
      row.classList.remove("idle", "running", "pass", "fail");
      row.classList.add(state);
      const duration = row.querySelector(".duration");
      duration.textContent = state === "running" ? "running…" :
        durationMs === null ? "—" : formatDuration(durationMs);
      const oldDetails = row.querySelector("details");
      if (oldDetails) oldDetails.remove();
      if (detail) {
        const details = document.createElement("details");
        const summary = document.createElement("summary");
        summary.textContent = "Output";
        const pre = document.createElement("pre");
        pre.textContent = detail;
        details.append(summary, pre);
        row.append(details);
      }
      if (state === "running") results.delete(test.path);
      if (state === "pass" || state === "fail") {
        results.set(test.path, {
          path: test.path,
          name: test.name,
          group: test.group,
          suite: test.suite,
          expectedFailure: test.expectFailure,
          unsupported: test.unsupported,
          unsupportedReason: test.unsupportedReason,
          state,
          durationMs,
          completedAt: new Date().toISOString(),
          ...record
        });
      }
      updateSummary();
    }

    function updateSummary() {
      let passed = 0;
      let failed = 0;
      let aborted = 0;
      let totalDuration = 0;
      const supportedResults = [...results.values()].filter(result => !result.unsupported);
      for (const result of supportedResults) {
        if (result.state === "pass") passed++;
        else if (result.aborted) aborted++;
        else failed++;
        totalDuration += result.durationMs;
      }
      document.querySelector("#summary").textContent =
        `${supportedResults.length} / ${SUPPORTED_TESTS.length} supported reported · ${passed} passed · ${failed} failed · ${aborted} aborted · ${formatDuration(totalDuration)} · ${PAYLOAD.tests.length - SUPPORTED_TESTS.length} unsupported excluded`;
      document.querySelector("#download-results").disabled = results.size === 0 || batchRunning;
    }

    function downloadResults() {
      if (results.size === 0 || batchRunning) return;
      const orderedResults = PAYLOAD.tests
        .map(test => results.get(test.path))
        .filter(Boolean);
      const supportedResults = orderedResults.filter(result => !result.unsupported);
      const passed = supportedResults.filter(result => result.state === "pass").length;
      const aborted = supportedResults.filter(result => result.aborted).length;
      const failed = supportedResults.length - passed - aborted;
        const report = {
          format: "waste-spec-test-results-v1",
          downloadedAt: new Date().toISOString(),
          userAgent: navigator.userAgent,
          execution: {
            mode: "cooperative",
            unit: "test-sandbox",
            threadCount: lastRunThreadCount,
            instructionQuantum: lastRunQuantum
          },
        testAll: {
          timeZone: Intl.DateTimeFormat().resolvedOptions().timeZone,
          startedAt: testAllStartedAt?.toISOString() ?? null,
          startedAtLocal: testAllStartedAt ? formatLocalTime(testAllStartedAt) : null,
          finishedAt: testAllFinishedAt?.toISOString() ?? null,
          finishedAtLocal: testAllFinishedAt ? formatLocalTime(testAllFinishedAt) : null,
          durationMs: testAllStartedAt && testAllFinishedAt ?
            testAllFinishedAt - testAllStartedAt : null
        },
        summary: {
          available: PAYLOAD.tests.length,
          supported: SUPPORTED_TESTS.length,
          unsupported: PAYLOAD.tests.length - SUPPORTED_TESTS.length,
          completed: orderedResults.length,
          completedSupported: supportedResults.length,
          passed,
          failed,
          aborted,
          durationMs: supportedResults.reduce((sum, result) => sum + result.durationMs, 0)
        },
        results: orderedResults
      };
      const blob = new Blob([JSON.stringify(report, null, 2) + "\n"], {type: "application/json"});
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
      link.href = url;
      link.download = `waste-spec-results-${timestamp}.json`;
      document.body.append(link);
      link.click();
      link.remove();
      URL.revokeObjectURL(url);
    }

    function execute(test) {
      return new Promise(resolve => {
        const workerUrl = URL.createObjectURL(new Blob([workerProgram], {type: "text/javascript"}));
        const worker = new Worker(workerUrl);
        activeWorkers.add(worker);
        let finished = false;
        const finish = result => {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          worker.terminate();
          activeWorkers.delete(worker);
          URL.revokeObjectURL(workerUrl);
          resolve(result);
        };
        const timer = setTimeout(() => finish({ok: false, error: `Timed out after ${TIMEOUT_MS / 1000} seconds`}), TIMEOUT_MS);
        worker.onerror = event => finish({ok: false, error: event.message || "Worker error"});
        let taskResult = null;
        worker.onmessage = event => {
          if (event.data.type === "result") taskResult = event.data;
          else if (event.data.type === "done")
            finish(taskResult || event.data);
        };
        const quantum = selectedQuantum();
        lastRunThreadCount = 1;
        lastRunQuantum = quantum;
        worker.postMessage({
          loader: PAYLOAD.loader, wasm: PAYLOAD.wasm,
          tests: [test], quantum, threadCount: 1,
          failLast: test.path === "core/custom.wast"
        });
      });
    }

    function executeCooperatively(tests, requestedThreads, onResult) {
      return new Promise(resolve => {
        const workerUrl = URL.createObjectURL(new Blob([workerProgram], {type: "text/javascript"}));
        const worker = new Worker(workerUrl);
        activeWorkers.add(worker);
        let finished = false;
        const finish = result => {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          worker.terminate();
          activeWorkers.delete(worker);
          URL.revokeObjectURL(workerUrl);
          resolve(result);
        };
        const timer = setTimeout(() => finish({
          ok: false,
          error: "Cooperative test run timed out after 30 minutes"
        }), 30 * 60 * 1000);
        worker.onerror = event => finish({ok: false, error: event.message || "Worker error"});
        worker.onmessage = event => {
          if (event.data.type === "result") onResult(event.data);
          else if (event.data.type === "done") finish(event.data);
        };
        const quantum = Math.max(1, Number(document.querySelector("#quantum")?.value) || DEFAULT_QUANTUM);
        lastRunQuantum = quantum;
        const failLast = tests.some(test => test.path === "core/custom.wast");
        const ordered = failLast ? [
          ...tests.filter(test => test.path !== "core/custom.wast"),
          ...tests.filter(test => test.path === "core/custom.wast")
        ] : tests;
        worker.postMessage({
          loader: PAYLOAD.loader,
          wasm: PAYLOAD.wasm,
          tests: ordered,
          quantum,
          threadCount: requestedThreads,
          failLast
        });
      });
    }

    async function runCooperativeBatch(tests, requestedThreads) {
      lastRunThreadCount = requestedThreads;
      const startedAt = performance.now();
      const pending = new Set(tests.map(test => test.path));
      for (const test of tests) setResult(test, "running");
      const raw = await executeCooperatively(tests, requestedThreads, result => {
        const test = tests.find(candidate => candidate.path === result.path);
        if (!test || !pending.delete(test.path)) return;
        const durationMs = performance.now() - startedAt;
        const passed = test.expectFailure ? !result.ok : result.ok;
        const expectation = test.expectFailure && !result.ok ? "Expected rejection observed." : "";
        const knownFailure = test.path === "core/custom.wast" && !result.ok ?
          "Known failure: -c custom recursively dispatches a binary section named custom. The cooperative runner records this expected result without entering the non-terminating handler." : "";
        const detail = [expectation, knownFailure, result.output].filter(Boolean).join("\n\n");
        setResult(test, passed ? "pass" : "fail", detail, durationMs, {
          exitCode: result.ok ? 0 : 1,
          output: result.output || "",
          error: knownFailure
        });
      });
      if (pending.size) {
        const detail = raw.error || raw.output || "The cooperative interpreter stopped before reporting this test.";
        for (const test of tests) {
          if (!pending.has(test.path)) continue;
          setResult(test, "fail", detail, null, {
            exitCode: raw.exitCode ?? null,
            output: raw.output || "",
            error: raw.error || "",
            aborted: true
          });
        }
      }
      return pending.size === 0 && [...tests].every(test => results.get(test.path)?.state === "pass");
    }

    async function runTest(test) {
      const row = rowFor(test);
      const button = row.querySelector("button");
      button.disabled = true;
      setResult(test, "running");
      const startedAt = performance.now();
      const raw = await execute(test);
      const durationMs = performance.now() - startedAt;
      const passed = test.expectFailure ? raw.exitCode === 1 : raw.ok;
      const expectation = test.expectFailure && raw.exitCode === 1 ? "Expected rejection observed." : "";
      const detail = [expectation, raw.output, raw.error].filter(Boolean).join("\n\n");
      setResult(test, passed ? "pass" : "fail", detail, durationMs, {
        exitCode: raw.exitCode ?? null,
        output: raw.output || "",
        error: raw.error || ""
      });
      button.disabled = batchRunning;
      return passed;
    }

    async function runBatch(tests) {
      if (batchRunning) return;
      const requestedThreads = selectedThreadCount();
      if (requestedThreads === null) return;
      batchRunning = true;
      document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = true);
      const section = tests.length ? groupFor(tests[0].group) : null;
      section?.classList.add("active");
      try {
        await runCooperativeBatch(tests, requestedThreads);
      } finally {
        section?.classList.remove("active");
        batchRunning = false;
        document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = false);
        updateSummary();
      }
    }

    async function runTestAll() {
      if (batchRunning) return;
      const requestedThreads = selectedThreadCount();
      if (requestedThreads === null) return;
      testAllStartedAt = new Date();
      testAllFinishedAt = null;
      updateTestAllTiming();
      batchRunning = true;
      document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = true);
      try {
        document.querySelectorAll(".group:not(.unsupported)")
          .forEach(section => section.classList.add("active"));
        try {
          await runCooperativeBatch(SUPPORTED_TESTS, requestedThreads);
        } finally {
          document.querySelectorAll(".group")
            .forEach(section => section.classList.remove("active"));
        }
      } finally {
        batchRunning = false;
        testAllFinishedAt = new Date();
        updateTestAllTiming();
        document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = false);
        updateSummary();
      }
    }

    render();
  </script>
</body>
</html>
'''


def main():
    parser = argparse.ArgumentParser(description="Generate the embedded WASTE browser test dashboard")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quantum", type=int, default=10_000)
    args = parser.parse_args()

    if args.quantum <= 0:
        parser.error("--quantum must be positive")

    root = args.repo_root.resolve()
    # The dashboard uses the CPS artifact so running evaluator tasks can retain
    # their continuations while yielding to a static page's worker event loop.
    dist_name = "dist-threaded"
    dist = root / "build" / "ocaml-wasm" / dist_name
    loader_path = dist / "wasm_cli.bc.wasm.js"
    asset_dir = dist / "wasm_cli.bc.wasm.assets"
    test_root = root / "submodules" / "wasm-spec" / "test"
    diy_posix_root = root / "tests" / "diy-posix-test"
    libc_test_root = root / "build" / "waste-libc" / "tests"
    output = args.output or root / "build" / "ocaml-wasm" / "browser-tests.html"

    if not loader_path.is_file():
        raise SystemExit(f"compiled loader not found: {loader_path}")
    wasm_files = sorted(asset_dir.glob("*.wasm"))
    if len(wasm_files) != 1:
        raise SystemExit(f"expected one Wasm asset in {asset_dir}, found {len(wasm_files)}")
    if not test_root.is_dir():
        raise SystemExit(f"spec test directory not found: {test_root}")
    if not diy_posix_root.is_dir():
        raise SystemExit(f"DIY POSIX test directory not found: {diy_posix_root}")
    if not libc_test_root.is_dir():
        raise SystemExit(f"libc test directory not found: {libc_test_root}")

    tests = [
        *collect_tests(test_root, suite="wasm-spec"),
        *collect_tests(
            diy_posix_root,
            suite="diy-posix-test",
            path_prefix="diy-posix-test",
        ),
        *collect_tests(
            libc_test_root,
            suite="libc-test",
            path_prefix="libc-test",
        ),
    ]
    if not tests:
        raise SystemExit(f"no .wast tests found under {test_root}")

    payload = {
        "loader": browser_loader(loader_path.read_text(encoding="utf-8")),
        "wasm": base64.b64encode(wasm_files[0].read_bytes()).decode("ascii"),
        "tests": tests,
    }
    supported_count = sum(not test["unsupported"] for test in tests)
    document = (
        HTML.replace("__TEST_COUNT__", str(supported_count))
        .replace("__EXECUTION_NOTICE__", "Tests run as isolated cooperative sandboxes inside one compiled OCaml WebAssembly interpreter instance. The concurrency setting does not create guest threads.")
        .replace("__TIMEOUT_NOTICE__", "Individual runs and cooperative batches time out after 30 minutes.")
        .replace("__PAYLOAD__", script_json(payload))
        .replace("__DEFAULT_QUANTUM__", str(args.quantum))
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8")
    print(f"Generated {output}")
    print(f"Embedded {len(tests)} tests in {len({test['group'] for test in tests})} directory groups")
    print(f"Output size: {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
