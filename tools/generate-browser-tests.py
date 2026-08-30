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
        tests.append(
            {
                "path": relative,
                "name": path.name,
                "group": group,
                "expectFailure": ".fail." in path.name,
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
    #summary { min-width: 235px; color: var(--muted); font-variant-numeric: tabular-nums; }
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
    .test-path { min-width: 0; overflow-wrap: anywhere; font-family: ui-monospace, SFMono-Regular, Consolas, monospace; }
    .duration { color: var(--muted); text-align: right; font-variant-numeric: tabular-nums; }
    .expected { margin-left: 8px; color: var(--muted); font: 11px system-ui, sans-serif; }
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
    <div id="summary">0 / __TEST_COUNT__ completed</div>
    <button id="test-all" type="button">Test all</button>
  </header>
  <main>
    <div class="notice">
      Each test runs in a fresh Web Worker containing the compiled OCaml reference interpreter.
      Expected <code>.fail.wast</code> rejections count as passes. A test times out after 120 seconds.
    </div>
    <div id="groups"></div>
  </main>
  <script>
    "use strict";
    const PAYLOAD = __PAYLOAD__;
    const TIMEOUT_MS = 120000;
    const results = new Map();
    let batchRunning = false;

    const workerProgram = String.raw`
      "use strict";
      self.onmessage = async ({data}) => {
        const output = [];
        const format = value => {
          if (typeof value === "string") return value;
          try { return JSON.stringify(value); } catch (_) { return String(value); }
        };
        console.log = (...values) => output.push(values.map(format).join(" "));
        console.error = (...values) => output.push(values.map(format).join(" "));
        globalThis.waste_argv = ["wasm", "-e", data.source];
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
            ok: globalThis.waste_exit_code === 0,
            output: output.join("\n"),
            exitCode: globalThis.waste_exit_code
          });
        } catch (error) {
          self.postMessage({
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

    function render() {
      const container = document.querySelector("#groups");
      for (const [groupName, tests] of groupedTests()) {
        const section = document.createElement("section");
        section.className = "group";
        const header = document.createElement("div");
        header.className = "group-header";
        const title = document.createElement("h2");
        title.textContent = groupName;
        const count = document.createElement("span");
        count.className = "group-count";
        count.textContent = `${tests.length} tests`;
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = "Test module";
        button.addEventListener("click", () => runBatch(tests));
        header.append(title, count, button);
        section.append(header);

        const list = document.createElement("div");
        for (const test of tests) {
          const row = document.createElement("div");
          row.className = "test idle";
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
      document.querySelector("#test-all").addEventListener("click", () => runBatch(PAYLOAD.tests));
      updateSummary();
    }

    function rowFor(test) {
      return [...document.querySelectorAll(".test")].find(row => row.dataset.path === test.path);
    }

    function formatDuration(milliseconds) {
      if (milliseconds < 1000) return `${Math.round(milliseconds)} ms`;
      if (milliseconds < 60000) return `${(milliseconds / 1000).toFixed(2)} s`;
      const minutes = Math.floor(milliseconds / 60000);
      const seconds = ((milliseconds % 60000) / 1000).toFixed(1);
      return `${minutes}m ${seconds}s`;
    }

    function setResult(test, state, detail = "", durationMs = null) {
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
        results.set(test.path, {state, durationMs});
      }
      updateSummary();
    }

    function updateSummary() {
      let passed = 0;
      let failed = 0;
      let totalDuration = 0;
      for (const result of results.values()) {
        result.state === "pass" ? passed++ : failed++;
        totalDuration += result.durationMs;
      }
      document.querySelector("#summary").textContent =
        `${results.size} / ${PAYLOAD.tests.length} completed · ${passed} passed · ${failed} failed · ${formatDuration(totalDuration)}`;
    }

    function execute(test) {
      return new Promise(resolve => {
        const workerUrl = URL.createObjectURL(new Blob([workerProgram], {type: "text/javascript"}));
        const worker = new Worker(workerUrl);
        let finished = false;
        const finish = result => {
          if (finished) return;
          finished = true;
          clearTimeout(timer);
          worker.terminate();
          URL.revokeObjectURL(workerUrl);
          resolve(result);
        };
        const timer = setTimeout(() => finish({ok: false, error: `Timed out after ${TIMEOUT_MS / 1000} seconds`}), TIMEOUT_MS);
        worker.onerror = event => finish({ok: false, error: event.message || "Worker error"});
        worker.onmessage = event => finish(event.data);
        worker.postMessage({loader: PAYLOAD.loader, wasm: PAYLOAD.wasm, source: test.source});
      });
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
      setResult(test, passed ? "pass" : "fail", detail, durationMs);
      button.disabled = false;
      return passed;
    }

    async function runBatch(tests) {
      if (batchRunning) return;
      batchRunning = true;
      document.querySelectorAll("header button, .group-header button").forEach(button => button.disabled = true);
      try {
        for (const test of tests) await runTest(test);
      } finally {
        batchRunning = false;
        document.querySelectorAll("button").forEach(button => button.disabled = false);
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
    args = parser.parse_args()

    root = args.repo_root.resolve()
    dist = root / "build" / "ocaml-wasm" / "dist"
    loader_path = dist / "wasm_cli.bc.wasm.js"
    asset_dir = dist / "wasm_cli.bc.wasm.assets"
    test_root = root / "submodules" / "wasm-spec" / "test"
    output = args.output or root / "build" / "ocaml-wasm" / "browser-tests.html"

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
    document = HTML.replace("__TEST_COUNT__", str(len(tests))).replace("__PAYLOAD__", script_json(payload))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8")
    print(f"Generated {output}")
    print(f"Embedded {len(tests)} tests in {len({test['group'] for test in tests})} directory groups")
    print(f"Output size: {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
