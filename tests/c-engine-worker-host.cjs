"use strict";

/* Node worker_threads host that evaluates the browser worker script.  Each
 * instance owns one test; the main runtime terminates it on timeout.  The
 * self.onmessage / self.postMessage shim mirrors the browser Worker API so the
 * worker source stays unchanged. */

const {parentPort, workerData} = require("node:worker_threads");
const vm = require("node:vm");

const self = {
  onmessage: null,
  postMessage(value) { parentPort.postMessage(value); },
};
globalThis.self = self;
globalThis.postMessage = (value) => parentPort.postMessage(value);
if (workerData.engineBytes)
  globalThis.__WASTE_ENGINE_BYTES = workerData.engineBytes;

vm.runInThisContext(workerData.workerSrc, {filename: "c-engine-worker.js"});

parentPort.on("message", async (data) => {
  if (typeof self.onmessage !== "function") {
    parentPort.postMessage({type: "error", error: "worker onmessage not set"});
    return;
  }
  try {
    await self.onmessage({data});
  } catch (err) {
    parentPort.postMessage({type: "error", error: String(err?.stack || err)});
  }
});
