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
const evidenceStatus = document.querySelector("#evidence-status");
const evidenceMarkers = [
  ["WAT explicit", "__WASTE_EVIDENCE_WAT_EXPLICIT_0__"],
  ["WAT direct", "__WASTE_EVIDENCE_WAT_DIRECT_0__"],
  ["WAT shebang", "__WASTE_EVIDENCE_WAT_SHEBANG_0__"],
  ["WAST explicit", "__WASTE_EVIDENCE_WAST_EXPLICIT_0__"],
  ["WAST direct", "__WASTE_EVIDENCE_WAST_DIRECT_0__"],
  ["WAST shebang", "__WASTE_EVIDENCE_WAST_SHEBANG_0__"],
];
let evidenceStartedAt = null;
let evidenceFinishedAt = null;
let evidenceOutput = "";
let evidenceStep = 0;
let evidenceWaitOffset = 0;
let evidenceWorkerReady = false;
const evidenceSteps = [
  {marker: "__WASTE_EVIDENCE_BEGIN__", command: "/bin/wat /tmp/waste-evidence.wat; printf '__WASTE_EVIDENCE_WAT_EXPLICIT_%s__\\n' \"$?\""},
  {marker: "__WASTE_EVIDENCE_WAT_EXPLICIT_0__", command: "/tmp/waste-evidence.wat; printf '__WASTE_EVIDENCE_WAT_DIRECT_%s__\\n' \"$?\""},
  {marker: "__WASTE_EVIDENCE_WAT_DIRECT_0__", command: "/tmp/waste-evidence-shebang.wat; printf '__WASTE_EVIDENCE_WAT_SHEBANG_%s__\\n' \"$?\""},
  {marker: "__WASTE_EVIDENCE_WAT_SHEBANG_0__", command: "/bin/wast /tmp/waste-evidence.wast"},
  {prompt: true, command: "printf '__WASTE_EVIDENCE_WAST_EXPLICIT_%s__\\n' \"$?\""},
  {marker: "__WASTE_EVIDENCE_WAST_EXPLICIT_0__", command: "/tmp/waste-evidence.wast"},
  {prompt: true, command: "printf '__WASTE_EVIDENCE_WAST_DIRECT_%s__\\n' \"$?\""},
  {marker: "__WASTE_EVIDENCE_WAST_DIRECT_0__", command: "/tmp/waste-evidence-shebang.wast"},
  {prompt: true, command: "printf '__WASTE_EVIDENCE_WAST_SHEBANG_%s__\\n' \"$?\""},
  {marker: "__WASTE_EVIDENCE_WAST_SHEBANG_0__", command: "printf '__WASTE_EVIDENCE_END__\\n'"}
];

function evidenceFiles() {
  const wat = new TextEncoder().encode('(module (memory 1) (func (export "_start")))\n');
  const watShebang = new TextEncoder().encode('#!/bin/wat\n(module (memory 1) (func (export "_start")))\n');
  const wast = new TextEncoder().encode('(module)\n');
  const wastShebang = new TextEncoder().encode('#!/bin/wast\n(module)\n');
  return [
    {path: "/tmp/waste-evidence.wat", bytes: wat, kind: 1, mode: 0o755},
    {path: "/tmp/waste-evidence-shebang.wat", bytes: watShebang, kind: 1, mode: 0o755},
    {path: "/tmp/waste-evidence.wast", bytes: wast, kind: 1, mode: 0o755},
    {path: "/tmp/waste-evidence-shebang.wast", bytes: wastShebang, kind: 1, mode: 0o755},
  ];
}

function sendTerminalResize() {
  if (worker) worker.postMessage({type: "resize", columns: model.columns, rows: model.rows});
}

function setRunning(running) {
  for (const selector of controls) document.querySelector(selector).disabled = !running;
  document.querySelector("#run-evidence").disabled = !running;
  if (!running) {
    document.querySelector("#terminal-input").disabled = true;
    document.querySelector("#send-input").disabled = true;
  }
}

function append(line) {
  appendRaw(line + "\n");
}

function evidencePassed() {
  return evidenceOutput.includes("__WASTE_EVIDENCE_END__") &&
    evidenceMarkers.every(([, marker]) => evidenceOutput.includes(marker));
}

function completeEvidence() {
  if (evidenceFinishedAt || !evidencePassed()) return;
  evidenceFinishedAt = new Date().toISOString();
  evidenceStatus.textContent = "Loader evidence complete: all expected markers observed.";
  evidenceStatus.style.color = "#9de6a8";
}

function appendRaw(text) {
  model.write(text);
  renderer.markDirty();
  transcript.textContent = (transcript.textContent + text).slice(-transcriptLimit);
  transcript.scrollTop = transcript.scrollHeight;
  if (evidenceStartedAt) {
    evidenceOutput += text;
    if (evidenceStep < evidenceSteps.length) {
      const step = evidenceSteps[evidenceStep];
      const outputAfterSend = evidenceOutput.slice(evidenceWaitOffset);
      const markerIndex = step.marker ?
        outputAfterSend.indexOf(step.marker) : -1;
      const ready = step.prompt
        ? /bash-[^\r\n]*[#$] ?/.test(outputAfterSend)
        : markerIndex >= 0 &&
          /bash-[^\r\n]*[#$] ?/.test(outputAfterSend.slice(
            markerIndex + step.marker.length));
      if (ready && evidenceWorkerReady) {
        evidenceStep++;
        evidenceWaitOffset = evidenceOutput.length;
        evidenceWorkerReady = false;
        setTimeout(() => sendInputBytes(
          new TextEncoder().encode(step.command + "\n")), 10);
      }
    }
    /* A worker message may contain output assembled across several terminal
       writes.  Test the accumulated transcript so completion cannot be lost
       when the END marker shares a message with the final prompt. */
    completeEvidence();
  }
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
    const vfsFiles = [];
    if (typeof g !== "undefined" && g.tar_hash && Object.keys(g.tar_hash).length) {
      for (const name of Object.keys(g.tar_hash)) {
        const bytes = await g.tar_hash[name].arrayBuffer();
        const isProbe = name === "waste-probe.wasm";
        const isCoreutilsTrue = name === "true.wasm";
        const isCoreutilsFalse = name === "false.wasm";
        const paths = isProbe ? ["/bin/waste-probe"] :
          isCoreutilsTrue ? ["/usr/bin/true", "/bin/true"] :
          isCoreutilsFalse ? ["/usr/bin/false", "/bin/false"] :
          ["/usr/share/waste/" + name];
        for (const path of paths) {
          vfsFiles.push({path, bytes, kind: 1,
            mode: isProbe || isCoreutilsTrue || isCoreutilsFalse ? 0o755 : 0o644});
        }
      }
    }
    vfsFiles.push(...evidenceFiles());

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
      else if (data.type === "started") {
        evidenceWorkerReady = true;
        if (starting)
          status.textContent = `C engine loaded; running Bash (${((Date.now() - startedEpoch) / 1000).toFixed(1)} s by Date)`;
        sendTerminalResize();
      }
      else if (data.type === "io-ready") {
        evidenceWorkerReady = true;
        if (evidenceStartedAt) appendRaw("");
      }
      else if (data.type === "done") {
        if (data.error) append(data.error);
        finish(data.ok ? "completed" : `failed${data.exitCode === undefined ? "" : ` (exit ${data.exitCode})`}`);
      }
    };
    worker.onerror = event => { append(event.message || "worker error"); finish("failed"); };
    worker.postMessage({type: "start", wasmBytes, source, probeBytes, vfsFiles});
  } catch (error) { status.textContent = error.message || String(error); }
}

function runLoaderEvidence() {
  if (!worker) return;
  evidenceStartedAt = new Date().toISOString();
  evidenceFinishedAt = null;
  evidenceOutput = "";
  evidenceStep = 0;
  evidenceWaitOffset = 0;
  evidenceWorkerReady = true;
  evidenceStatus.textContent = "Running loader evidence in the browser VFS...";
  evidenceStatus.style.color = "";
  /* Consume the current ready token just like every subsequent evidence
     command.  Otherwise each command is released by the previous resume's
     io-ready message and the sequence runs one handshake ahead. */
  evidenceWorkerReady = false;
  sendInputBytes(new TextEncoder().encode(
    "printf '__WASTE_EVIDENCE_BEGIN__\\n'\n"));
}

function downloadEvidence() {
  completeEvidence();
  const lines = [
    "WASTE browser loader evidence",
    `generated_at=${new Date().toISOString()}`,
    `page_url=${location.href}`,
    `user_agent=${navigator.userAgent}`,
    `evidence_started_at=${evidenceStartedAt || "not-run"}`,
    `evidence_finished_at=${evidenceFinishedAt || "not-finished"}`,
    `evidence_complete=${evidenceFinishedAt && evidencePassed() ? "yes" : "no"}`,
    "expected_markers:",
    ...evidenceMarkers.map(([label, marker]) => `${label}: ${marker}`),
    "observed_markers:",
    ...evidenceMarkers.map(([label, marker]) => `${label}: ${evidenceOutput.includes(marker) ? "yes" : "no"}`),
    "--- terminal transcript ---",
    transcript.textContent,
  ];
  const blob = new Blob([lines.join("\n") + "\n"], {type: "text/plain;charset=utf-8"});
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = `waste-browser-loader-evidence-${new Date().toISOString().replace(/[:.]/g, "-")}.log`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

document.querySelector("#run-evidence").addEventListener("click", runLoaderEvidence);
document.querySelector("#download-evidence").addEventListener("click", downloadEvidence);
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

/* Headless/offline browser acceptance hook.  It is inert unless explicitly
   requested by the page URL and uses the same button path as manual evidence. */
if (new URLSearchParams(location.search).has("auto-evidence")) {
  const autoEvidenceTimer = setInterval(() => {
    if (worker && !starting && !evidenceStartedAt) {
      clearInterval(autoEvidenceTimer);
      runLoaderEvidence();
    }
  }, 25);
}
