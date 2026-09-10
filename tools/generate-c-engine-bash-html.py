#!/usr/bin/env python3
"""Generate a self-contained C-engine Bash page.

Embeds the compiled C engine (waste-wast.wasm) and the bash-runtime.wast
launch script into a single offline HTML file.  The C engine processes the
WAST script in a Web Worker.

Terminal output is captured through the posix_write host import.  Interactive
input uses native yield/resume: when posix_read has no data, waste_host_posix_read
returns -2, the interpreter saves its frames and returns EXEC_YIELD.  The worker
awaits input then calls waste_wast_resume to continue.  No asyncify transform,
no SharedArrayBuffer, works on file://.
"""

import argparse
import base64
import json
from pathlib import Path


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
  <title>WASTE Bash (C engine)</title>
  <style>
    :root { color-scheme: dark; --bg:#0b1017; --panel:#151d29; --line:#334258; --text:#e8edf4; --muted:#9cacbf; --accent:#62b4ff; }
    * { box-sizing:border-box; }
    body { margin:0; min-height:100vh; background:radial-gradient(circle at top,#17253a,var(--bg) 38rem); color:var(--text); font:15px/1.45 system-ui,sans-serif; }
    main { width:min(1050px,calc(100% - 32px)); margin:32px auto; }
    h1 { margin:0 0 4px; font-size:24px; }
    .subtitle { color:var(--muted); margin:0 0 22px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:16px; box-shadow:0 14px 38px #0005; }
    form,.controls { display:flex; flex-wrap:wrap; gap:9px; align-items:center; }
    input,select,button { border:1px solid #40526c; border-radius:7px; background:#202c3e; color:var(--text); padding:8px 11px; font:inherit; }
    #terminal-input { flex:1 1 420px; font-family:ui-monospace,SFMono-Regular,Consolas,monospace; }
    button { cursor:pointer; }
    button:hover:not(:disabled) { border-color:var(--accent); }
    button:disabled { cursor:not-allowed; opacity:.48; }
    #start,#send-input { background:#135c8e; border-color:#2984bd; font-weight:650; }
    .controls { margin-top:10px; }
    #status { margin-left:auto; color:var(--muted); }
    pre { min-height:480px; max-height:68vh; overflow:auto; margin:16px 0 0; padding:16px; border:1px solid #263449; border-radius:8px; background:#070b10; color:#dce8d5; white-space:pre-wrap; overflow-wrap:anywhere; font:14px/1.5 ui-monospace,SFMono-Regular,Consolas,monospace; }
    code { color:#b9ddff; }
  </style>
</head>
<body>
  <main>
    <h1>WASTE Bash (C engine)</h1>
    <p class="subtitle">A self-contained C WebAssembly engine, shared runtime namespace, guest libc, and Bash. No server or network access is required.</p>
    <section class="panel">
      <form id="start-form">
        <button id="start" type="submit">Restart Bash</button>
      </form>
      <div class="controls">
        <button id="pause" type="button" disabled>Pause</button>
        <button id="resume" type="button" disabled>Resume</button>
        <select id="signal" disabled>
          <option value="2">SIGINT</option><option value="15">SIGTERM</option>
          <option value="1">SIGHUP</option><option value="14">SIGALRM</option>
          <option value="28">SIGWINCH</option>
        </select>
        <button id="send-signal" type="button" disabled>Send signal</button>
        <button id="stop" type="button" disabled>Stop worker</button>
        <span id="status">idle</span>
      </div>
      <pre id="terminal" aria-live="polite"></pre>
      <form id="terminal-form" style="margin-top:10px">
        <label for="terminal-input">input</label>
        <input id="terminal-input" autocomplete="off" spellcheck="false" disabled>
        <button id="send-input" type="submit" disabled>Send</button>
      </form>
    </section>
  </main>
  <script>
    "use strict";
    const PAYLOAD = __PAYLOAD__;
    let worker = null;
    let startedAt = null;
    let startedEpoch = null;
    let statusTimer = null;
    let starting = false;
    const terminal = document.querySelector("#terminal");
    const status = document.querySelector("#status");
    const controls = ["#pause","#resume","#signal","#send-signal","#stop"];

    const workerProgram = String.raw`
      "use strict";

      function decodeB64(text) {
        const raw = atob(text), bytes = new Uint8Array(raw.length);
        for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
        return bytes;
      }

      function watFloat(text, asF32) {
        return asF32 ? Math.fround(Number(text)) : Number(text);
      }

      let exp = null;
      let engineMemory = null;
      let inputQueue = [];
      let pendingSignal = -1;
      let ioResolve = null;
      let terminated = false;

      function waitForIO() {
        return new Promise(resolve => { ioResolve = resolve; });
      }

      const decoder = new TextDecoder();

      function posixRead(fd, ptr, count) {
        if (fd !== 0) return 0;
        if (terminated) return 0;
        if (pendingSignal >= 0) { pendingSignal = -1; return -1; }
        if (inputQueue.length > 0) {
          const input = inputQueue[0];
          const n = Math.min(input.length, count);
          new Uint8Array(engineMemory.buffer, ptr, n).set(input.subarray(0, n));
          if (n >= input.length) inputQueue.shift();
          else inputQueue[0] = input.subarray(n);
          return n;
        }
        return -2;
      }

      function posixWrite(fd, ptr, count) {
        if (fd === 1 || fd === 2) {
          const bytes = new Uint8Array(engineMemory.buffer, ptr, count);
          const text = decoder.decode(bytes, {stream: true});
          self.postMessage({type: "output", text});
        }
        return count;
      }

      async function run(wasmB64, source) {
        const wasmBytes = decodeB64(wasmB64);
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
        }};
        const {instance} = await WebAssembly.instantiate(wasmBytes, imports);
        exp = instance.exports;
        engineMemory = exp.memory;

        const sourceBytes = new TextEncoder().encode(source);
        const scriptPtr = exp.waste_wast_alloc(sourceBytes.length);
        if (!scriptPtr) throw new Error("C engine script allocation failed");
        new Uint8Array(engineMemory.buffer, scriptPtr, sourceBytes.length).set(sourceBytes);

        self.postMessage({type: "started"});

        let yielded = exp.waste_wast_run_script(scriptPtr, sourceBytes.length);
        while (yielded) {
          await waitForIO();
          if (terminated) break;
          yielded = exp.waste_wast_resume();
        }

        const total = exp.waste_wast_results_total();
        const passed = exp.waste_wast_results_passed();
        const resultsPtr = exp.waste_wast_results_ptr();
        const resultBytes = new Uint8Array(engineMemory.buffer);
        const results = [];
        for (let i = 0; i < total; i++) {
          const base = resultsPtr + i * 256;
          let funcEnd = 1;
          while (funcEnd < 64 && resultBytes[base + funcEnd]) funcEnd++;
          let errorEnd = 64;
          while (errorEnd < 256 && resultBytes[base + errorEnd]) errorEnd++;
          results.push({
            pass: resultBytes[base] !== 0,
            func: decoder.decode(resultBytes.subarray(base + 1, base + funcEnd)),
            error: decoder.decode(resultBytes.subarray(base + 64, base + errorEnd)),
          });
        }
        self.postMessage({type: "done", ok: total === 0 || passed === total,
          total, passed, results});
      }

      self.onmessage = function(e) {
        const msg = e.data;
        if (msg.type === "start") {
          run(msg.wasmB64, msg.source).catch(error => {
            self.postMessage({type: "done", ok: false,
              error: error && (error.stack || error.message) || String(error)});
          });
        } else if (msg.type === "input") {
          inputQueue.push(new Uint8Array(msg.bytes));
          if (ioResolve) { ioResolve(); ioResolve = null; }
        } else if (msg.type === "signal") {
          pendingSignal = msg.signal;
          if (ioResolve) { ioResolve(); ioResolve = null; }
        } else if (msg.type === "stop") {
          terminated = true;
          if (ioResolve) { ioResolve(); ioResolve = null; }
        }
      };
    `;

    function setRunning(running) {
      for (const selector of controls) document.querySelector(selector).disabled = !running;
      if (!running) {
        document.querySelector("#terminal-input").disabled = true;
        document.querySelector("#send-input").disabled = true;
      }
    }

    function append(line) {
      terminal.textContent += line + "\n";
      terminal.scrollTop = terminal.scrollHeight;
    }

    function appendRaw(text) {
      terminal.textContent += text;
      terminal.scrollTop = terminal.scrollHeight;
    }

    function finish(message) {
      const elapsed = startedEpoch ? ((Date.now() - startedEpoch) / 1000).toFixed(3) : "0.000";
      status.textContent = `${message} \u00b7 ${elapsed}s by Date`;
      clearInterval(statusTimer);
      starting = false;
      setRunning(false);
      if (worker) { worker.postMessage({type: "stop"}); }
      worker?.terminate();
      worker = null;
    }

    function startShell(event) {
      event?.preventDefault();
      try {
        if (worker) { worker.postMessage({type: "stop"}); }
        worker?.terminate();
        const source = PAYLOAD.launch;
        terminal.textContent = "";
        status.textContent = "Starting C engine (0.0 s)";
        setRunning(true);
        document.querySelector("#terminal-input").disabled = true;
        document.querySelector("#send-input").disabled = true;
        startedAt = performance.now();
        startedEpoch = Date.now();
        starting = true;
        clearInterval(statusTimer);
        statusTimer = setInterval(() => {
          if (starting) status.textContent = `Starting C engine (${((Date.now() - startedEpoch) / 1000).toFixed(1)} s by Date)`;
        }, 100);
        const url = URL.createObjectURL(new Blob([workerProgram], {type:"text/javascript"}));
        worker = new Worker(url);
        URL.revokeObjectURL(url);
        worker.onmessage = ({data}) => {
          if (data.type === "output") {
            appendRaw(data.text);
            if (starting && /bash-[^\r\n]*[#$] ?/.test(data.text)) {
              starting = false;
              clearInterval(statusTimer);
              const wallSeconds = (Date.now() - startedEpoch) / 1000;
              const monotonicSeconds = (performance.now() - startedAt) / 1000;
              status.textContent = `Bash running after ${wallSeconds.toFixed(3)} s by Date (${monotonicSeconds.toFixed(3)} s monotonic)`;
              document.querySelector("#terminal-input").disabled = false;
              document.querySelector("#send-input").disabled = false;
              document.querySelector("#terminal-input").focus();
            }
          }
          else if (data.type === "started" && starting) {
            status.textContent = `C engine loaded; running Bash (${((Date.now() - startedEpoch) / 1000).toFixed(1)} s by Date)`;
          }
          else if (data.type === "done") {
            if (data.error) append(data.error);
            finish(data.ok ? "completed" : `failed${data.exitCode === undefined ? "" : ` (exit ${data.exitCode})`}`);
          }
        };
        worker.onerror = event => { append(event.message || "worker error"); finish("failed"); };
        worker.postMessage({type: "start", wasmB64: PAYLOAD.wasmB64, source});
      } catch (error) { status.textContent = error.message || String(error); }
    }
    document.querySelector("#start-form").addEventListener("submit", startShell);

    document.querySelector("#terminal-form").addEventListener("submit", event => {
      event.preventDefault();
      const input = document.querySelector("#terminal-input");
      const bytes = new TextEncoder().encode(input.value + "\n");
      if (worker) worker.postMessage({type: "input", bytes: Array.from(bytes)});
      append(input.value);
      input.value = "";
      status.textContent = "Bash running";
    });

    document.querySelector("#pause").onclick = () => status.textContent = "paused (C engine executes synchronously)";
    document.querySelector("#resume").onclick = () => status.textContent = "running";
    document.querySelector("#send-signal").onclick = () => {
      const select = document.querySelector("#signal");
      const signal = Number(select.value);
      if (worker) worker.postMessage({type: "signal", signal});
      status.textContent = `${select.selectedOptions[0].textContent} queued`;
    };
    document.querySelector("#stop").onclick = () => finish("stopped");
    startShell();
  </script>
</body>
</html>
'''


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a self-contained C-engine Bash page"
    )
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--wasm", type=Path, required=True,
                        help="Path to waste-wast.wasm (C engine)")
    parser.add_argument("--launch", type=Path, required=True,
                        help="Path to bash-runtime.wast (interactive mode)")
    parser.add_argument("--output", type=Path, required=True,
                        help="Output HTML file path")
    args = parser.parse_args()

    if not args.wasm.is_file():
        raise SystemExit(f"C engine Wasm not found: {args.wasm}")
    if not args.launch.is_file():
        raise SystemExit(f"Bash launch script not found: {args.launch}")

    launch_text = args.launch.read_text(encoding="utf-8")
    wasm_bytes = args.wasm.read_bytes()
    wasm_b64 = base64.b64encode(wasm_bytes).decode("ascii")

    payload = {
        "wasmB64": wasm_b64,
        "launch": launch_text,
    }

    document = HTML.replace("__PAYLOAD__", script_json(payload))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(document, encoding="utf-8")
    print(f"Generated {args.output}")
    print(f"Output size: {args.output.stat().st_size:,} bytes")
    print(f"C engine Wasm: {len(wasm_bytes):,} bytes "
          f"({len(wasm_b64):,} B base64)")
    print(f"Launch script: {len(launch_text):,} bytes")


if __name__ == "__main__":
    main()
