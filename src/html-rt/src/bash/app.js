"use strict";

let worker = null;
let startedAt = null;
let startedEpoch = null;
let statusTimer = null;
let starting = false;
const terminal = document.querySelector("#terminal-canvas");
const transcript = document.querySelector("#terminal-transcript");
const status = document.querySelector("#status");
const controls = ["#pause","#resume","#signal","#send-signal","#stop"];
const model = new WasteTerminalModel(80, 24);
const renderer = new WasteTerminalRenderer(terminal, model);
const transcriptLimit = 65536;

function sendTerminalResize() {
  if (worker) worker.postMessage({type: "resize", columns: model.columns, rows: model.rows});
}

function setRunning(running) {
  for (const selector of controls) document.querySelector(selector).disabled = !running;
  if (!running) {
    document.querySelector("#terminal-input").disabled = true;
    document.querySelector("#send-input").disabled = true;
  }
}

function append(line) {
  appendRaw(line + "\n");
}

function appendRaw(text) {
  model.write(text);
  renderer.markDirty();
  transcript.textContent = (transcript.textContent + text).slice(-transcriptLimit);
  transcript.scrollTop = transcript.scrollHeight;
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

async function startShell(event) {
  event?.preventDefault();
  try {
    if (worker) { worker.postMessage({type: "stop"}); }
    worker?.terminate();

    const source = await loadText("launch.wast");
    const wasmBytes = await loadBinary("waste-wast.wasm");
    let probeBytes = null;
    try { probeBytes = await loadBinary("waste-probe.wasm"); } catch (_) { /* optional during development */ }

    /* Remove loading overlay — everything is inflated and ready */
    var overlay = document.getElementById("loading-overlay");
    if (overlay) overlay.remove();

    model.fullReset();
    renderer.markDirty();
    transcript.textContent = "";
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

    worker = await createWorker("worker.js");
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
          terminal.focus();
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
    worker.postMessage({type: "start", wasmBytes, source, probeBytes});
    sendTerminalResize();
  } catch (error) { status.textContent = error.message || String(error); }
}
document.querySelector("#start-form").addEventListener("submit", startShell);

document.querySelector("#terminal-form").addEventListener("submit", event => {
  event.preventDefault();
  const input = document.querySelector("#terminal-input");
  const bytes = new TextEncoder().encode(input.value + "\n");
  if (worker) worker.postMessage({type: "input", bytes: Array.from(bytes)});
  input.value = "";
  status.textContent = "Bash running";
  terminal.focus();
});

function sendInputBytes(bytes) {
  if (worker) worker.postMessage({type: "input", bytes: Array.from(bytes)});
}

document.querySelector("#pause").onclick = () => status.textContent = "paused (C engine executes synchronously)";
document.querySelector("#resume").onclick = () => status.textContent = "running";
document.querySelector("#send-signal").onclick = () => {
  const select = document.querySelector("#signal");
  const signal = Number(select.value);
  if (worker) worker.postMessage({type: "signal", signal});
  status.textContent = `${select.selectedOptions[0].textContent} queued`;
};
document.querySelector("#stop").onclick = () => finish("stopped");

/* The engine owns canonical editing and echo. The canvas only translates
 * browser events into terminal bytes; visible output still comes exclusively
 * from the guest terminal path. */
terminal.addEventListener("keydown", event => {
  if (event.metaKey) return;
  if (event.key === "Enter") { sendInputBytes(new Uint8Array([10])); event.preventDefault(); return; }
  if (event.key === "Backspace") { sendInputBytes(new Uint8Array([127])); event.preventDefault(); return; }
  if (event.key === "Tab") { sendInputBytes(new Uint8Array([9])); event.preventDefault(); return; }
  if (event.ctrlKey && event.key.length === 1) {
    const code = event.key.toUpperCase().charCodeAt(0) - 64;
    if (code > 0 && code < 32) sendInputBytes(new Uint8Array([code]));
    event.preventDefault(); return;
  }
  if (event.key.length === 1) { sendInputBytes(new TextEncoder().encode(event.key)); event.preventDefault(); }
});
terminal.addEventListener("paste", event => {
  sendInputBytes(new TextEncoder().encode(event.clipboardData?.getData("text") || ""));
  event.preventDefault();
});

window.addEventListener("resize", sendTerminalResize);
