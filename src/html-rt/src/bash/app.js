"use strict";

let worker = null;
let startedAt = null;
let startedEpoch = null;
let statusTimer = null;
let starting = false;
const terminal = document.querySelector("#terminal");
const status = document.querySelector("#status");
const controls = ["#pause","#resume","#signal","#send-signal","#stop"];

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

async function startShell(event) {
  event?.preventDefault();
  try {
    if (worker) { worker.postMessage({type: "stop"}); }
    worker?.terminate();

    const source = await loadText("launch.wast");
    const wasmBytes = await loadBinary("waste-wast.wasm");

    /* Remove loading overlay — everything is inflated and ready */
    var overlay = document.getElementById("loading-overlay");
    if (overlay) overlay.remove();

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
    worker.postMessage({type: "start", wasmBytes, source});
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
