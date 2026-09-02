#!/usr/bin/env python3

import argparse
import base64
import hashlib
import json
import subprocess
from pathlib import Path

def browser_loader(source: str) -> str:
    argv_original = 'argv:()=>g?globalThis.process.argv.slice(1):["a.out"]'
    argv_browser = 'argv:()=>g?globalThis.process.argv.slice(1):(globalThis.waste_argv||["a.out"])'
    exit_original = 'exit:a=>g&&globalThis.process.exit(a)'
    exit_browser = 'exit:a=>{if(g)return globalThis.process.exit(a);globalThis.waste_exit_code=a}'
    if argv_original not in source or exit_original not in source:
        raise RuntimeError("generated loader browser hooks were not recognized")
    return source.replace(argv_original, argv_browser).replace(exit_original, exit_browser)


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
  <title>WASTE Bash</title>
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
    #quantum { width:115px; }
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
    <h1>WASTE Bash</h1>
    <p class="subtitle">A self-contained OCaml WebAssembly interpreter, shared runtime namespace, guest libc, and Bash. No server or network access is required.</p>
    <section class="panel">
      <form id="start-form">
        <label for="quantum">quantum</label>
        <input id="quantum" type="number" min="1" step="1" value="__DEFAULT_QUANTUM__">
        <label><input id="profile" type="checkbox"> profile startup phases</label>
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
      const queueControl = (operation, argument = 0) => {
        const page = globalThis.waste_control_page;
        if (!(page instanceof Int32Array)) return;
        const sequence = page[0] + 1;
        page[4 + (sequence & 255)] = (operation << 16) | (argument & 0xffff);
        page[0] = sequence;
      };
      const sha256Hex = async bytes => {
        if (!globalThis.crypto?.subtle) return null;
        const digest = await globalThis.crypto.subtle.digest("SHA-256", bytes);
        return Array.from(new Uint8Array(digest), byte =>
          byte.toString(16).padStart(2,"0")).join("");
      };
      self.onmessage = async ({data}) => {
        if (data.type === "control") {
          if (data.operation === 1) globalThis.waste_runtime_paused = true;
          else if (data.operation === 0) globalThis.waste_runtime_paused = false;
          else queueControl(data.operation, data.argument);
          return;
        }
        if (data.type === "input") {
          for (const byte of data.bytes) queueControl(4, byte);
          return;
        }
        let taskOk = true;
        const sendLine = values => {
          const line = values.map(value => typeof value === "string" ? value : String(value)).join(" ").trimEnd();
          const result = /^WASTE_RESULT 0 ([01])$/.exec(line);
          if (result) { taskOk = result[1] === "0"; return; }
          const done = /^WASTE_DONE ([01])$/.exec(line);
          if (done) {
            self.postMessage({type:"done",ok:taskOk && done[1] === "0",exitCode:done[1] === "0" ? 0 : 1});
            return;
          }
          if (/^WASTE_TASK /.test(line)) return;
          self.postMessage({type:"output", line});
        };
        console.log = (...values) => sendLine(values);
        console.error = (...values) => sendLine(values);
        globalThis.waste_control_page = new Int32Array(4 + 256);
        globalThis.waste_control_page[3] = 1;
        globalThis.waste_runtime_paused = false;
        const deferred = [];
        const channel = new MessageChannel();
        channel.port1.onmessage = () => deferred.shift()?.();
        globalThis.waste_defer = (callback, milliseconds) => {
          const resume = () => globalThis.waste_runtime_paused
            ? globalThis.setTimeout(resume, 10) : callback();
          if (milliseconds > 0) globalThis.setTimeout(resume, milliseconds);
          else { deferred.push(resume); channel.port2.postMessage(0); }
        };
        const binary = atob(data.wasm);
        const bytes = Uint8Array.from(binary, character => character.charCodeAt(0));
        let validationCacheMatched = false;
        try {
          if (data.validation?.schema === 1) {
            const encoder = new TextEncoder();
            const [launchHash,loaderHash,wasmHash] = await Promise.all([
              sha256Hex(encoder.encode(data.source)),
              sha256Hex(encoder.encode(data.loader)),
              sha256Hex(bytes),
            ]);
            validationCacheMatched = launchHash !== null &&
              launchHash === data.validation.launch_sha256 &&
              loaderHash === data.validation.loader_sha256 &&
              wasmHash === data.validation.wasm_sha256;
          }
        } catch (_) {
          validationCacheMatched = false;
        }
        globalThis.waste_argv = ["wasm","-ca","--schedule","--browser-schedule","--control-page"];
        if (validationCacheMatched) globalThis.waste_argv.push("-u");
        if (data.profile) {
          globalThis.waste_argv.push("-t");
          sendLine([validationCacheMatched
            ? "-- generation-time validation cache matched; runtime validation disabled"
            : "-- validation cache unavailable or mismatched; runtime validation enabled"]);
        }
        globalThis.waste_argv.push("-q",String(data.quantum),"--threads","1","-e",data.source);
        globalThis.waste_exit_code = 0;
        globalThis.fetch = async () => new Response(bytes, {headers:{"Content-Type":"application/wasm"}});
        try {
          const completion = (0,eval)(data.loader);
          if (!completion || typeof completion.then !== "function") throw new Error("loader did not return a promise");
          await completion;
          self.postMessage({type:"loaded"});
        } catch (error) {
          self.postMessage({type:"done", ok:false, error:error && (error.stack || error.message) || String(error)});
        }
      };
    `;

    function setRunning(running) {
      document.querySelector("#quantum").disabled = running;
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

    function browserTiming(label) {
      const nowEpoch = Date.now();
      const wallMilliseconds = nowEpoch - startedEpoch;
      const monotonicMilliseconds = performance.now() - startedAt;
      const difference = wallMilliseconds - monotonicMilliseconds;
      return `-- [browser ${wallMilliseconds} ms Date, ${monotonicMilliseconds.toFixed(1)} ms performance, delta ${difference.toFixed(1)} ms] ${label} at ${new Date(nowEpoch).toString()}`;
    }

    function finish(message) {
      const elapsed = startedEpoch ? ((Date.now() - startedEpoch) / 1000).toFixed(3) : "0.000";
      status.textContent = `${message} · ${elapsed}s by Date`;
      clearInterval(statusTimer);
      starting = false;
      setRunning(false);
      worker?.terminate();
      worker = null;
    }

    function startShell(event) {
      event?.preventDefault();
      const quantum = Number(document.querySelector("#quantum").value);
      const profile = document.querySelector("#profile").checked;
      if (!Number.isSafeInteger(quantum) || quantum < 1) { status.textContent = "quantum must be a positive integer"; return; }
      try {
        worker?.terminate();
        const source = PAYLOAD.launch;
        terminal.textContent = "";
        status.textContent = "Starting interpreter (0.0 s)";
        setRunning(true);
        document.querySelector("#terminal-input").disabled = true;
        document.querySelector("#send-input").disabled = true;
        startedAt = performance.now();
        startedEpoch = Date.now();
        starting = true;
        if (profile) append(browserTiming("startup requested"));
        clearInterval(statusTimer);
        statusTimer = setInterval(() => {
          if (starting) status.textContent = `Starting interpreter (${((Date.now() - startedEpoch) / 1000).toFixed(1)} s by Date)`;
        }, 100);
        const url = URL.createObjectURL(new Blob([workerProgram], {type:"text/javascript"}));
        worker = new Worker(url);
        URL.revokeObjectURL(url);
        worker.onmessage = ({data}) => {
          if (data.type === "output") {
            append(data.line);
            if (profile && data.line.includes('Invoking function "main"'))
              append(browserTiming("Bash main invoked"));
            if (starting && /bash-[^\r\n]*[#$] ?/.test(data.line)) {
              starting = false;
              clearInterval(statusTimer);
              const wallSeconds = (Date.now() - startedEpoch) / 1000;
              const monotonicSeconds = (performance.now() - startedAt) / 1000;
              status.textContent = `Bash running after ${wallSeconds.toFixed(3)} s by Date (${monotonicSeconds.toFixed(3)} s monotonic)`;
              document.querySelector("#terminal-input").disabled = false;
              document.querySelector("#send-input").disabled = false;
              document.querySelector("#terminal-input").focus();
              if (profile) append(browserTiming("first Bash prompt displayed"));
            }
          }
          else if (data.type === "loaded" && starting) {
            status.textContent = `Interpreter loaded; starting Bash (${((Date.now() - startedEpoch) / 1000).toFixed(1)} s by Date)`;
            if (profile) append(browserTiming("interpreter loader resolved"));
          }
          else if (data.type === "done") {
            if (data.error) append(data.error);
            finish(data.ok ? "completed" : `failed${data.exitCode === undefined ? "" : ` (exit ${data.exitCode})`}`);
          }
        };
        worker.onerror = event => { append(event.message || "worker error"); finish("failed"); };
        worker.postMessage({loader:PAYLOAD.loader, wasm:PAYLOAD.wasm, source,
          validation:PAYLOAD.validation, quantum, profile});
      } catch (error) { status.textContent = error.message || String(error); }
    }
    document.querySelector("#start-form").addEventListener("submit", startShell);

    document.querySelector("#terminal-form").addEventListener("submit", event => {
      event.preventDefault();
      const input = document.querySelector("#terminal-input");
      const bytes = new TextEncoder().encode(input.value + "\n");
      if (bytes.length > 240) { status.textContent = "Input line is limited to 239 UTF-8 bytes"; return; }
      worker?.postMessage({type:"input",bytes:Array.from(bytes)});
      append(input.value);
      input.value = "";
      status.textContent = "Bash running";
    });

    document.querySelector("#pause").onclick = () => { worker?.postMessage({type:"control",operation:1}); status.textContent = "paused"; };
    document.querySelector("#resume").onclick = () => { worker?.postMessage({type:"control",operation:0}); status.textContent = "running"; };
    document.querySelector("#send-signal").onclick = () => {
      const select = document.querySelector("#signal");
      worker?.postMessage({type:"control",operation:2,argument:Number(select.value)});
      status.textContent = `${select.selectedOptions[0].textContent} queued`;
    };
    document.querySelector("#stop").onclick = () => finish("stopped");
    startShell();
  </script>
</body>
</html>
'''


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a self-contained WASTE Bash page")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--launch", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quantum", type=int, default=10_000)
    parser.add_argument("--validator", type=Path)
    args = parser.parse_args()
    if args.quantum <= 0:
        parser.error("--quantum must be positive")

    root = args.repo_root.resolve()
    dist = root / "build" / "ocaml-wasm" / "dist-threaded"
    loader_path = dist / "wasm_cli.bc.wasm.js"
    asset_dir = dist / "wasm_cli.bc.wasm.assets"
    launch_path = args.launch or root / "build" / "bash" / "bash-runtime.wast"
    output = args.output or root / "build" / "ocaml-wasm" / "bash.html"
    validator = args.validator or root / "submodules" / "wasm-spec" / "interpreter" / "_build" / "default" / "wasm.exe"
    wasm_files = sorted(asset_dir.glob("*.wasm"))
    if not loader_path.is_file():
        raise SystemExit(f"compiled CPS loader not found: {loader_path}")
    if len(wasm_files) != 1:
        raise SystemExit(f"expected one Wasm asset in {asset_dir}, found {len(wasm_files)}")
    if not launch_path.is_file():
        raise SystemExit(f"Bash launch script not found: {launch_path}")
    if not validator.is_file():
        raise SystemExit(f"native validation interpreter not found: {validator}")

    launch_bytes = launch_path.read_bytes()
    subprocess.run([str(validator), "-ca", "-d", str(launch_path)], check=True)
    if launch_path.read_bytes() != launch_bytes:
        raise SystemExit("Bash launch script changed while it was being validated")
    loader = browser_loader(loader_path.read_text(encoding="utf-8"))
    wasm = wasm_files[0].read_bytes()
    payload = {
        "loader": loader,
        "wasm": base64.b64encode(wasm).decode("ascii"),
        "launch": launch_bytes.decode("utf-8"),
        "validation": {
            "schema": 1,
            "algorithm": "SHA-256",
            "launch_sha256": hashlib.sha256(launch_bytes).hexdigest(),
            "loader_sha256": hashlib.sha256(loader.encode("utf-8")).hexdigest(),
            "wasm_sha256": hashlib.sha256(wasm).hexdigest(),
            "validator_sha256": hashlib.sha256(validator.read_bytes()).hexdigest(),
        },
    }
    document = (
        HTML.replace("__DEFAULT_QUANTUM__", str(args.quantum))
        .replace("__PAYLOAD__", script_json(payload))
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8")
    print(f"Generated {output}")
    print(f"Output size: {output.stat().st_size} bytes")
    print(f"Validated launch SHA-256: {payload['validation']['launch_sha256']}")
    print(f"Validator SHA-256: {payload['validation']['validator_sha256']}")


if __name__ == "__main__":
    main()
