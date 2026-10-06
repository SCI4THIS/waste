"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {Worker} = require("node:worker_threads");
const {corpusSchedule} = require("./corpus-schedule.cjs");

const root = path.resolve(__dirname, "..");
const frontendDir = path.join(root, "src/html-rt/src");
const positional = process.argv.slice(2).filter(arg => !arg.startsWith("--"));
const flagArgs = process.argv.slice(2).filter(arg => arg.startsWith("--"));
const payloadPath = positional[0] || path.join(root, "build/html-rt/tests/payload.json");
const workerSourceFlag = flagArgs.find(arg => arg.startsWith("--worker-source="));
const workerSourcePath = workerSourceFlag
  ? path.resolve(workerSourceFlag.slice("--worker-source=".length))
  : path.join(frontendDir, "tests-worker.js");
const sharedFilePageProbe = flagArgs.includes("--shared-file-page");
const emitJson = flagArgs.includes("--json");
const listOnly = flagArgs.includes("--list");
const resultsFlag = flagArgs.find(arg => arg.startsWith("--results="));
const resultsPath = resultsFlag ? resultsFlag.slice("--results=".length) : null;
const requestedGroups = new Set(flagArgs
  .filter(arg => arg.startsWith("--group="))
  .map(arg => arg.slice("--group=".length)));
const excludedFiles = new Set(flagArgs
  .filter(arg => arg.startsWith("--exclude="))
  .map(arg => arg.slice("--exclude=".length)));
const excludedGroups = new Set(flagArgs
  .filter(arg => arg.startsWith("--exclude-group="))
  .map(arg => arg.slice("--exclude-group=".length)));
const requestedFiles = new Set(positional.slice(1));
const timeoutFlag = flagArgs.find(arg => arg.startsWith("--timeout-ms="));
const defaultTimeoutMs = timeoutFlag ? Number(timeoutFlag.slice("--timeout-ms=".length)) : 60000;
/* Per-group timeout budgets.  Rationale: core/bulk-memory, core/simd,
 * core/memory64, and core legitimately run 15-28s per fixture; the
 * remaining groups all finish under ~1.5s.  A single 60s ceiling
 * burns ~55s per hang in a fast group.  These budgets add ~2x
 * headroom over the observed max; --timeout-group=NAME:MS overrides
 * individual entries, and --timeout-ms= still sets the fallback for
 * groups not listed below. */
const GROUP_TIMEOUT_MS = {
  "core/bulk-memory": 60000,
  "core/simd": 50000,
  "core/memory64": 50000,
  "core": 35000,
  "core/gc": 5000,
  "core/multi-memory": 5000,
  "core/exceptions": 5000,
  "core/relaxed-simd": 5000,
  "libc-test": 5000,
  "diy-posix-test": 5000,
  "custom/custom": 5000,
  "custom/name": 5000,
  "custom/metadata.code.branch_hint": 5000,
};
for (const arg of flagArgs.filter(a => a.startsWith("--timeout-group="))) {
  const spec = arg.slice("--timeout-group=".length);
  const colon = spec.lastIndexOf(":");
  if (colon < 0) throw new Error(`--timeout-group= expects NAME:MS, got ${spec}`);
  GROUP_TIMEOUT_MS[spec.slice(0, colon)] = Number(spec.slice(colon + 1));
}
const timeoutFor = (group) => GROUP_TIMEOUT_MS[group] ?? defaultTimeoutMs;
const jobsFlag = flagArgs.find(arg => arg.startsWith("--jobs="));
const jobs = Math.max(1, jobsFlag ? Number(jobsFlag.slice("--jobs=".length)) : 1);
const workerHostPath = path.join(__dirname, "c-engine-worker-host.cjs");

const expectedFailuresPath = path.join(root, "tests/browser-corpus-expected-failures.txt");
const expectedFailures = new Set();
if (fs.existsSync(expectedFailuresPath)) {
  for (const line of fs.readFileSync(expectedFailuresPath, "utf8").split("\n")) {
    const trimmed = line.replace(/#.*/, "").trim();
    if (trimmed) expectedFailures.add(trimmed);
  }
}

const payload = JSON.parse(fs.readFileSync(payloadPath, "utf8"));
const wasmPath = path.join(root, "build/html-rt/waste-wast.wasm");
const engineBytes = new Uint8Array(fs.readFileSync(wasmPath));
const workerSrc = fs.readFileSync(workerSourcePath, "utf8");

(async () => {
  const summary = {pass: 0, fail: 0, xfail: 0, xpass: 0,
    failures: [], unexpectedPasses: []};
  const records = [];
  const hasFilter = requestedFiles.size > 0 || requestedGroups.size > 0;
  const included = hasFilter ? payload.tests.filter(test =>
    requestedFiles.has(test.file) ||
    requestedFiles.has(test.path || `${test.group}/${test.file}`) ||
    requestedGroups.has(test.group)
  ) : payload.tests.filter(test => !test.unsupported);
  const tests = included.filter(test => {
    const identity = test.path || `${test.group}/${test.file}`;
    return !excludedFiles.has(test.file) && !excludedFiles.has(identity) &&
      !excludedGroups.has(test.group);
  });
  if ((hasFilter || excludedFiles.size || excludedGroups.size) && tests.length === 0)
    throw new Error("no requested C-engine browser tests found");
  const dispatchOrder = corpusSchedule(tests, flagArgs);
  if (listOnly) {
    for (const test of tests) {
      const identity = test.path || `${test.group}/${test.file}`;
      const expectedFailure = expectedFailures.has(identity);
      console.log(`${expectedFailure ? "XFAIL?" : "PASS?"} ${identity}`);
    }
    if (emitJson) console.log(JSON.stringify({count: tests.length,
      expectedFailures: tests.filter(t =>
        expectedFailures.has(t.path || `${t.group}/${t.file}`)).length}, null, 2));
    return;
  }

  async function verifyWorkerHostErrorForwarding() {
    const cases = [
      {label: "missing handler", workerSrc: "self.onmessage = null;",
        expected: "worker onmessage not set"},
      {label: "thrown handler", workerSrc:
        "self.onmessage = function() { throw new Error('intentional handler failure'); };",
        expected: "intentional handler failure"},
    ];
    for (const item of cases) {
      const worker = new Worker(workerHostPath, {workerData: {workerSrc: item.workerSrc}});
      const message = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error(
          `worker host ${item.label} did not reply`)), 5000);
        worker.once("message", value => { clearTimeout(timer); resolve(value); });
        worker.once("error", error => { clearTimeout(timer); reject(error); });
        worker.postMessage({});
      }).finally(() => worker.terminate());
      if (message?.type !== "error" || !String(message.error).includes(item.expected))
        throw new Error(`worker host ${item.label} was not forwarded: ${JSON.stringify(message)}`);
    }
    console.log("PASS worker host error forwarding: missing-handler and thrown-handler cases");
  }
  await verifyWorkerHostErrorForwarding();

  /* Worker pool: --jobs=N (default 1).  Each slot owns one long-lived
   * Worker that handles successive tests; on timeout the slot
   * terminates and respawns.  The browser worker script re-instantiates
   * the engine per message, so test isolation is preserved regardless
   * of pool size. Optional --schedule=longest-first uses --timings=PATH
   * from a prior run to dispatch slow tests first. Status lines and
   * result records remain in manifest order regardless of dispatch. */
  function prepareSpec(test) {
    let testSpec = test.spec;
    if (testSpec.mode === "wast-stream" && !testSpec.wastB64 && !testSpec.wastText) {
      const wastPath = testSpec.sourcePath
        ? path.resolve(root, testSpec.sourcePath)
        : path.join(root, "build/html-rt/tests/wast", test.file);
      testSpec = Object.assign({}, testSpec, {
        wastText: fs.readFileSync(wastPath, "utf8"),
      });
    }
    if (sharedFilePageProbe && testSpec.mode === "wast-stream")
      testSpec = Object.assign({}, testSpec, {sharedFilePageProbe: true});
    return testSpec;
  }

  function runOnWorker(worker, testSpec, budgetMs) {
    return new Promise((resolve) => {
      let timer;
      const onMessage = (value) => { cleanup(); clearTimeout(timer); resolve({timedOut: false, message: value}); };
      const onError = (err) => { cleanup(); clearTimeout(timer);
        resolve({timedOut: false, message: {type: "error", error: String(err?.stack || err)}}); };
      const cleanup = () => {
        worker.off("message", onMessage); worker.off("error", onError);
      };
      worker.on("message", onMessage);
      worker.on("error", onError);
      timer = setTimeout(() => {
        cleanup(); worker.terminate(); resolve({timedOut: true, message: null});
      }, budgetMs);
      worker.postMessage({testSpec});
    });
  }

  const outputs = new Array(tests.length);
  let nextToFlush = 0;
  const slotResults = new Array(tests.length);
  function flush() {
    while (nextToFlush < tests.length && outputs[nextToFlush] !== undefined) {
      const {statusLine, diagnostic} = outputs[nextToFlush];
      console.log(statusLine);
      if (diagnostic) console.error(diagnostic);
      nextToFlush++;
    }
  }

  let nextIndex = 0;
  async function slotLoop() {
    let worker = new Worker(workerHostPath, {workerData: {workerSrc, engineBytes}});
    while (nextIndex < dispatchOrder.length) {
      const index = dispatchOrder[nextIndex++];
      const test = tests[index];
      const identity = test.path || `${test.group}/${test.file}`;
      const expectedFailure = expectedFailures.has(identity);
      const testSpec = prepareSpec(test);
      const budgetMs = timeoutFor(test.group);
      const startedAt = process.hrtime.bigint();
      const {timedOut, message} = await runOnWorker(worker, testSpec, budgetMs);
      if (timedOut) worker = new Worker(workerHostPath, {workerData: {workerSrc, engineBytes}});
      const elapsedMs = Number((process.hrtime.bigint() - startedAt) / 1000n) / 1000;
      const ok = !timedOut && message?.type === "done" &&
        message.results.every(result => result.pass) &&
        (!message.setup || (message.setup.complete && message.completed &&
                            message.setup.passed === message.setup.total));
      let status;
      if (timedOut) { status = "TIMEOUT"; summary.fail++; summary.failures.push(identity); }
      else if (ok && !expectedFailure) { status = "PASS"; summary.pass++; }
      else if (!ok && expectedFailure) { status = "XFAIL"; summary.xfail++; }
      else if (ok && expectedFailure) {
        status = "XPASS"; summary.xpass++; summary.unexpectedPasses.push(identity);
      } else {
        status = "FAIL"; summary.fail++; summary.failures.push(identity);
      }
      const record = {identity, status, expectedFailure,
        group: test.group, file: test.file, mode: test.spec?.mode,
        elapsedMs: Math.round(elapsedMs * 1000) / 1000};
      if (message?.type === "done") {
        const failures = message.results.map((result, i) =>
          ({index: i, ...result})).filter(result => !result.pass);
        record.failureCount = failures.length;
        record.resultCount = message.results.length;
        if (message.setup) { record.setup = message.setup; record.completed = message.completed; }
        // Preserve successful actions too, so cutover checks can compare every
        // result rather than only file classifications or failed assertions.
        if (resultsPath) record.assertions = message.results;
      }
      slotResults[index] = record;
      let diagnostic = null;
      if (status === "FAIL" || status === "TIMEOUT") {
        if (timedOut) {
          diagnostic = JSON.stringify({type: "timeout", identity, timeoutMs: budgetMs}, null, 2);
        } else if (message?.type === "done") {
          const failures = message.results.map((result, i) =>
            ({index: i, ...result})).filter(result => !result.pass);
          diagnostic = JSON.stringify({type: message.type, file: message.file,
            resultCount: message.results.length, failureCount: failures.length,
            failures: failures.slice(0, 12)}, null, 2);
        } else {
          diagnostic = JSON.stringify(message, null, 2);
        }
      }
      outputs[index] = {statusLine: `${status} ${identity}`, diagnostic};
      flush();
    }
    await worker.terminate();
  }

  await Promise.all(Array.from({length: Math.min(jobs, tests.length)}, () => slotLoop()));
  for (const record of slotResults) if (record) records.push(record);
  if (emitJson) console.log(JSON.stringify(summary, null, 2));
  if (resultsPath) {
    fs.mkdirSync(path.dirname(resultsPath), {recursive: true});
    fs.writeFileSync(resultsPath,
      JSON.stringify({summary, tests: records}, null, 2) + "\n");
  }
  if (summary.fail)
    throw new Error(`${summary.fail} C-engine browser test(s) regressed`);
  if (summary.xpass)
    throw new Error(`${summary.xpass} expected-failure test(s) unexpectedly passed: ` +
      summary.unexpectedPasses.join(", "));
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
