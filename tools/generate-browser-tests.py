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


def collect_tests(test_root: Path):
    tests = []
    for path in sorted(test_root.rglob("*.wast")):
        relative = path.relative_to(test_root).as_posix()
        parent = path.parent.relative_to(test_root).as_posix()
        group = parent if parent != "." else "root"
        unsupported = relative.startswith("legacy/")
        tests.append(
            {
                "path": relative,
                "name": path.name,
                "group": group,
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
  <title>WASTE · WebAssembly specification tests</title>
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
    <h1>WASTE · embedded specification tests</h1>
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
    const EXECUTION_MODE = __EXECUTION_MODE__;
    const DEFAULT_QUANTUM = __DEFAULT_QUANTUM__;
    const THREAD_COUNT = EXECUTION_MODE === "threaded" ? SUPPORTED_TESTS.length : 1;
    const TIMEOUT_MS = 120000;
    const results = new Map();
    const activeControlPages = new Set();
    let batchRunning = false;
    let testAllStartedAt = null;
    let testAllFinishedAt = null;

    const workerProgram = String.raw`
      "use strict";
      self.onmessage = async ({data}) => {
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
          if (taskOutput && activeTask !== null) taskOutput[activeTask].push(message);
          else output.push(message);
        };
        console.log = (...values) => capture(values);
        console.error = (...values) => capture(values);
        if (data.controlPage) globalThis.waste_control_page = new Int32Array(data.controlPage);
        if (data.tests) {
          globalThis.waste_argv = ["wasm", "-ca", "--schedule"];
          if (data.failLast) globalThis.waste_argv.push("--fail-last");
          globalThis.waste_argv.push("-q", String(data.quantum));
          for (const test of data.tests) globalThis.waste_argv.push("-e", test.source);
        } else {
          globalThis.waste_argv = ["wasm", "-ca", "-e", data.source];
        }
        if (data.controlPage) globalThis.waste_argv.push("--control-page");
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
          self.postMessage({
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

    function createControlPage() {
      if (typeof SharedArrayBuffer !== "function") return null;
      const page = new Int32Array(new SharedArrayBuffer((4 + 256) * 4));
      page[3] = 1; // control ABI version
      return page;
    }

    function sendControl(page, operation, argument = 0) {
      const sequence = Atomics.load(page, 0) + 1;
      const slot = 4 + (sequence & 255);
      Atomics.store(page, slot, (operation << 16) | (argument & 0xffff));
      Atomics.store(page, 0, sequence);
      Atomics.notify(page, 0);
    }

    function pauseRuntimes() {
      for (const page of activeControlPages) {
        Atomics.store(page, 1, 1);
        sendControl(page, 1);
      }
    }

    function resumeRuntimes() {
      for (const page of activeControlPages) {
        Atomics.store(page, 1, 0);
        Atomics.notify(page, 1);
      }
    }

    function signalRuntimes() {
      const signal = Number(document.querySelector("#signal-number").value);
      for (const page of activeControlPages) sendControl(page, 2, signal);
    }

    function render() {
      const executionMode = document.querySelector("#execution-mode");
      if (EXECUTION_MODE === "threaded") {
        executionMode.append(`Cooperative threads: ${THREAD_COUNT} · quantum: `);
        const quantum = document.createElement("input");
        quantum.id = "quantum";
        quantum.type = "number";
        quantum.min = "1";
        quantum.step = "1000";
        quantum.value = String(DEFAULT_QUANTUM);
        quantum.title = "Interpreter steps executed by a test before switching";
        executionMode.append(quantum, " steps");
      } else {
        executionMode.textContent = "Sequential threads: 1";
      }
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
      if (typeof SharedArrayBuffer !== "function") {
        document.querySelectorAll(".runtime-control").forEach(control => control.disabled = true);
        document.querySelector(".notice").append(
          " Shared memory controls require a cross-origin-isolated page (COOP/COEP)."
        );
      }
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
      let totalDuration = 0;
      const supportedResults = [...results.values()].filter(result => !result.unsupported);
      for (const result of supportedResults) {
        result.state === "pass" ? passed++ : failed++;
        totalDuration += result.durationMs;
      }
      document.querySelector("#summary").textContent =
        `${supportedResults.length} / ${SUPPORTED_TESTS.length} supported completed · ${passed} passed · ${failed} failed · ${formatDuration(totalDuration)} · ${PAYLOAD.tests.length - SUPPORTED_TESTS.length} unsupported excluded`;
      document.querySelector("#download-results").disabled = results.size === 0 || batchRunning;
    }

    function downloadResults() {
      if (results.size === 0 || batchRunning) return;
      const orderedResults = PAYLOAD.tests
        .map(test => results.get(test.path))
        .filter(Boolean);
      const supportedResults = orderedResults.filter(result => !result.unsupported);
      const passed = supportedResults.filter(result => result.state === "pass").length;
      const failed = supportedResults.length - passed;
        const report = {
          format: "waste-spec-test-results-v1",
          downloadedAt: new Date().toISOString(),
          userAgent: navigator.userAgent,
          execution: {
            mode: EXECUTION_MODE,
            threadCount: THREAD_COUNT,
            instructionQuantum: EXECUTION_MODE === "threaded" ?
              Math.max(1, Number(document.querySelector("#quantum")?.value) || DEFAULT_QUANTUM) : null
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
        const controlPage = createControlPage();
        if (controlPage) activeControlPages.add(controlPage);
        let finished = false;
        const finish = result => {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          worker.terminate();
          if (controlPage) activeControlPages.delete(controlPage);
          URL.revokeObjectURL(workerUrl);
          resolve(result);
        };
        const timer = setTimeout(() => finish({ok: false, error: `Timed out after ${TIMEOUT_MS / 1000} seconds`}), TIMEOUT_MS);
        worker.onerror = event => finish({ok: false, error: event.message || "Worker error"});
        worker.onmessage = event => finish(event.data);
        worker.postMessage({
          loader: PAYLOAD.loader, wasm: PAYLOAD.wasm, source: test.source,
          controlPage: controlPage?.buffer
        });
      });
    }

    function executeCooperatively(tests, onResult) {
      return new Promise(resolve => {
        const workerUrl = URL.createObjectURL(new Blob([workerProgram], {type: "text/javascript"}));
        const worker = new Worker(workerUrl);
        const controlPage = createControlPage();
        if (controlPage) activeControlPages.add(controlPage);
        let finished = false;
        const finish = result => {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          worker.terminate();
          if (controlPage) activeControlPages.delete(controlPage);
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
          failLast,
          controlPage: controlPage?.buffer
        });
      });
    }

    async function runCooperativeBatch(tests) {
      const startedAt = performance.now();
      const pending = new Set(tests.map(test => test.path));
      for (const test of tests) setResult(test, "running");
      const raw = await executeCooperatively(tests, result => {
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
          setResult(test, "fail", detail, performance.now() - startedAt, {
            exitCode: raw.exitCode ?? null,
            output: raw.output || "",
            error: raw.error || ""
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
      batchRunning = true;
      document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = true);
      const section = tests.length ? groupFor(tests[0].group) : null;
      section?.classList.add("active");
      try {
        if (EXECUTION_MODE === "threaded") await runCooperativeBatch(tests);
        else for (const test of tests) await runTest(test);
      } finally {
        section?.classList.remove("active");
        batchRunning = false;
        document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = false);
        updateSummary();
      }
    }

    async function runTestAll() {
      if (batchRunning) return;
      testAllStartedAt = new Date();
      testAllFinishedAt = null;
      updateTestAllTiming();
      batchRunning = true;
      document.querySelectorAll("button:not(.runtime-control)").forEach(button => button.disabled = true);
      try {
        if (EXECUTION_MODE === "threaded") {
          document.querySelectorAll(".group:not(.unsupported)")
            .forEach(section => section.classList.add("active"));
          try {
            await runCooperativeBatch(SUPPORTED_TESTS);
          } finally {
            document.querySelectorAll(".group")
              .forEach(section => section.classList.remove("active"));
          }
        } else {
          for (const [groupName, tests] of groupedTests()) {
            const supported = tests.filter(test => !test.unsupported);
            if (supported.length === 0) continue;
            const section = groupFor(groupName);
            section?.classList.add("active");
            try {
              for (const test of supported) await runTest(test);
            } finally {
              section?.classList.remove("active");
            }
          }
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
    parser.add_argument("--mode", choices=("sequential", "threaded"), default="sequential")
    parser.add_argument("--quantum", type=int, default=10_000)
    args = parser.parse_args()

    if args.quantum <= 0:
        parser.error("--quantum must be positive")

    root = args.repo_root.resolve()
    dist_name = "dist-threaded" if args.mode == "threaded" else "dist"
    dist = root / "build" / "ocaml-wasm" / dist_name
    loader_path = dist / "wasm_cli.bc.wasm.js"
    asset_dir = dist / "wasm_cli.bc.wasm.assets"
    test_root = root / "submodules" / "wasm-spec" / "test"
    default_name = "browser-tests-threaded.html" if args.mode == "threaded" else "browser-tests.html"
    output = args.output or root / "build" / "ocaml-wasm" / default_name

    if not loader_path.is_file():
        raise SystemExit(f"compiled loader not found: {loader_path}")
    wasm_files = sorted(asset_dir.glob("*.wasm"))
    if len(wasm_files) != 1:
        raise SystemExit(f"expected one Wasm asset in {asset_dir}, found {len(wasm_files)}")
    if not test_root.is_dir():
        raise SystemExit(f"spec test directory not found: {test_root}")

    tests = collect_tests(test_root)
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
        .replace(
            "__EXECUTION_NOTICE__",
            (
                "Test all runs every supported test as a cooperative thread inside one compiled OCaml WebAssembly interpreter instance. Logical threads switch after the configured instruction quantum."
                if args.mode == "threaded"
                else "Each test runs in a fresh Web Worker containing the compiled OCaml reference interpreter. Thread count is fixed at 1."
            ),
        )
        .replace(
            "__TIMEOUT_NOTICE__",
            (
                "Individual runs time out after 120 seconds; cooperative batches time out after 30 minutes."
                if args.mode == "threaded"
                else "A test times out after 120 seconds."
            ),
        )
        .replace("__PAYLOAD__", script_json(payload))
        .replace("__EXECUTION_MODE__", script_json(args.mode))
        .replace("__DEFAULT_QUANTUM__", str(args.quantum))
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8")
    print(f"Generated {output}")
    print(f"Embedded {len(tests)} tests in {len({test['group'] for test in tests})} directory groups")
    print(f"Output size: {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
