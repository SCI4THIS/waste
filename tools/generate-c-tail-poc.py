#!/usr/bin/env python3

import argparse
import base64
from pathlib import Path


HTML = r'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>WASTE C tail-call proof</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { max-width: 850px; margin: 3rem auto; padding: 0 1rem; background: #0c1017; color: #e8edf5; }
    section { padding: 1.25rem; border: 1px solid #2b3647; border-radius: 10px; background: #151b25; }
    .controls { display: flex; flex-wrap: wrap; gap: .75rem; align-items: end; }
    label { display: grid; gap: .35rem; }
    input, button { padding: .55rem .75rem; font: inherit; }
    button { cursor: pointer; }
    table { width: 100%; margin-top: 1rem; border-collapse: collapse; }
    th, td { padding: .6rem; border-bottom: 1px solid #2b3647; text-align: right; }
    th:first-child, td:first-child { text-align: left; }
    .pass { color: #63dc8c; }
    .fail { color: #ff7070; }
    pre { min-height: 3rem; white-space: pre-wrap; color: #b9c5d8; }
  </style>
</head>
<body>
  <h1>WASTE C tail-call proof</h1>
  <p>This static page runs a real guest Wasm module inside the freestanding C interpreter Wasm.</p>
  <section>
    <div class="controls">
      <label>Tail transfers <input id="iterations" type="number" min="1" step="1" value="5000000"></label>
      <button data-mode="0">Run return_call</button>
      <button data-mode="1">Run return_call_ref</button>
      <button id="both">Run both</button>
    </div>
    <table>
      <thead><tr><th>Operation</th><th>Status</th><th>Time</th><th>Transfers/s</th><th>Frames</th><th>Run allocations</th></tr></thead>
      <tbody id="results"></tbody>
    </table>
    <pre id="status">Loading embedded engine…</pre>
  </section>
  <script>
    "use strict";
    const ENGINE = "__ENGINE__";
    const GUEST = "__GUEST__";
    const decode = text => Uint8Array.from(atob(text), character => character.charCodeAt(0));
    const status = document.querySelector("#status");
    const rows = document.querySelector("#results");
    let api;

    const ready = (async () => {
      const instance = await WebAssembly.instantiate(decode(ENGINE), {});
      api = instance.instance.exports;
      const guest = decode(GUEST);
      const address = api.waste_poc_alloc(guest.length);
      if (!address) throw new Error("C engine allocation failed");
      new Uint8Array(api.memory.buffer, address, guest.length).set(guest);
      const loadStatus = api.waste_poc_load(address, guest.length);
      if (loadStatus !== 0) throw new Error(`guest load failed with status ${loadStatus}`);
      status.textContent = "Ready. Timing uses browser performance.now().";
    })().catch(error => {
      status.textContent = error.stack || error.message || String(error);
      throw error;
    });

    function selectedIterations() {
      const value = Number(document.querySelector("#iterations").value);
      if (!Number.isSafeInteger(value) || value <= 0) throw new Error("Tail transfers must be a positive safe integer");
      return value;
    }

    async function run(mode) {
      await ready;
      const iterations = selectedIterations();
      const name = mode === 0 ? "return_call" : "return_call_ref";
      status.textContent = `Running ${name} at ${new Date().toLocaleTimeString()}…`;
      await new Promise(resolve => setTimeout(resolve, 0));
      const started = performance.now();
      const runStatus = api.waste_poc_run(mode, BigInt(iterations));
      const elapsed = performance.now() - started;
      const result = api.waste_poc_result();
      const transfers = api.waste_poc_tail_calls();
      const frames = api.waste_poc_max_frames();
      const allocations = api.waste_poc_run_allocations();
      const passed = runStatus === 0 && result === 0n && transfers === BigInt(iterations) && frames === 1 && allocations === 0n;
      const row = document.createElement("tr");
      row.innerHTML = `<td>${name}</td><td class="${passed ? "pass" : "fail"}">${passed ? "pass" : `status ${runStatus}`}</td>` +
        `<td>${elapsed.toFixed(3)} ms</td><td>${Math.round(iterations / (elapsed / 1000)).toLocaleString()}</td>` +
        `<td>${frames}</td><td>${allocations}</td>`;
      rows.prepend(row);
      status.textContent = `${name}: ${api.waste_poc_instructions()} decoded instructions, result ${result}`;
      return passed;
    }

    document.querySelectorAll("button[data-mode]").forEach(button => {
      button.addEventListener("click", () => run(Number(button.dataset.mode)).catch(error => status.textContent = error.stack || String(error)));
    });
    document.querySelector("#both").addEventListener("click", async () => {
      try { await run(0); await run(1); } catch (error) { status.textContent = error.stack || String(error); }
    });
  </script>
</body>
</html>
'''


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate the static C tail-call proof page")
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--guest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    document = HTML.replace(
        "__ENGINE__", base64.b64encode(args.engine.read_bytes()).decode("ascii")
    ).replace("__GUEST__", base64.b64encode(args.guest.read_bytes()).decode("ascii"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(document, encoding="utf-8")
    print(f"Generated {args.output} ({args.output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
