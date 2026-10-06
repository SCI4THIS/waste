"use strict";
const {config, withConfig} = require("./runtime-config.cjs");

/* Native-side corpus runner: feeds every wast-stream fixture in the shared
 * payload.json through build/cli-rt/waste-wast and classifies the result into
 * the same {pass, fail, xfail, xpass} record contract the browser runtime
 * emits.  Native lacks the browser's POSIX stub resolver, so a
 * native-specific expected-failures baseline (tests/native-corpus-expected-failures.txt)
 * covers the libc-test / diy-posix / posix-dependent fixtures that only run
 * under the browser worker. */

const fs = require("node:fs");
const path = require("node:path");
const {spawn} = require("node:child_process");
const {corpusSchedule} = require("./corpus-schedule.cjs");

const root = path.resolve(__dirname, "..");
const positional = process.argv.slice(2).filter(arg => !arg.startsWith("--"));
const flagArgs = process.argv.slice(2).filter(arg => arg.startsWith("--"));
const payloadPath = positional[0] || path.join(root, "build/html-rt/tests/payload.json");
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
const defaultTimeoutMs = timeoutFlag ? Number(timeoutFlag.slice("--timeout-ms=".length)) : config.LEGACY_NATIVE_TIMEOUT_DEFAULT_MS;
/* Per-group timeout budgets.  Native is substantially faster than the
 * browser worker — observed maxima from Stage 6B.11 top out at 7.7s
 * (core) and sit under 4.3s for every other group.  Budgets add ~2x
 * headroom.  --timeout-group=NAME:MS overrides individual entries;
 * --timeout-ms= still sets the fallback for groups not listed. */
const GROUP_TIMEOUT_MS = {
  "core": config.SUITE_TIMEOUT_CORE_MS,
  "core/simd": config.SUITE_TIMEOUT_HEAVY_MS,
  "core/bulk-memory": config.SUITE_TIMEOUT_HEAVY_MS,
  "core/memory64": config.SUITE_TIMEOUT_HEAVY_MS,
  "core/gc": config.SUITE_TIMEOUT_DEFAULT_MS,
  "core/multi-memory": config.SUITE_TIMEOUT_DEFAULT_MS,
  "core/exceptions": config.SUITE_TIMEOUT_DEFAULT_MS,
  "core/relaxed-simd": config.SUITE_TIMEOUT_DEFAULT_MS,
  "libc-test": config.SUITE_TIMEOUT_DEFAULT_MS,
  "diy-posix-test": config.SUITE_TIMEOUT_DEFAULT_MS,
  "custom/custom": config.SUITE_TIMEOUT_DEFAULT_MS,
  "custom/name": config.SUITE_TIMEOUT_DEFAULT_MS,
  "custom/metadata.code.branch_hint": config.SUITE_TIMEOUT_DEFAULT_MS,
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
const runnerFlag = flagArgs.find(arg => arg.startsWith("--runner="));
const runnerPath = runnerFlag ? runnerFlag.slice("--runner=".length)
                               : path.join(root, "build/cli-rt/waste-cli");

const expectedFailuresPath = path.join(root, "tests/native-corpus-expected-failures.txt");
const expectedFailures = new Set();
if (fs.existsSync(expectedFailuresPath)) {
  for (const line of fs.readFileSync(expectedFailuresPath, "utf8").split("\n")) {
    const trimmed = line.replace(/#.*/, "").trim();
    if (trimmed) expectedFailures.add(trimmed);
  }
}

const payload = JSON.parse(fs.readFileSync(payloadPath, "utf8"));

/* Persistent server-mode child: each pool slot keeps one `waste-cli --server`
 * child alive and feeds it WAST paths over stdin.  The child emits each
 * test's JSON on stdout followed by a "###END###\n" sentinel; stderr gets
 * its own sentinel so parent can delimit per-test error output without
 * opening per-test pipes.  Amortises process startup across many tests. */
const SENTINEL = "###END###\n";

function spawnServer() {
  const state = {
    child: spawn(runnerPath, ["--server"], {stdio: ["pipe", "pipe", "pipe"]}),
    stdoutBuf: "", stderrBuf: "",
    pending: null,  // {resolve, timer, timedOut, stdoutDone, stderrDone}
    exited: false, exitCode: null, spawnError: null,
  };
  state.child.stdout.setEncoding("utf8");
  state.child.stderr.setEncoding("utf8");
  state.child.stdout.on("data", (chunk) => {
    state.stdoutBuf += chunk;
    tryDeliver(state, "stdout");
  });
  state.child.stderr.on("data", (chunk) => {
    state.stderrBuf += chunk;
    tryDeliver(state, "stderr");
  });
  state.child.once("error", (err) => {
    state.exited = true;
    state.spawnError = String(err?.stack || err);
    if (state.pending) finish(state, {ok: false, timedOut: false,
      spawnError: state.spawnError, stdout: "", stderr: "", code: -1});
  });
  state.child.once("close", (code) => {
    state.exited = true;
    state.exitCode = code;
    if (state.pending && !state.pending.timedOut)
      finish(state, {ok: false, timedOut: false, stdout: state.stdoutBuf,
        stderr: state.stderrBuf, code});
  });
  return state;
}

function tryDeliver(state, stream) {
  if (!state.pending || state.pending.timedOut) return;
  const bufKey = stream + "Buf", doneKey = stream + "Done";
  const idx = state[bufKey].indexOf(SENTINEL);
  if (idx < 0) return;
  state.pending[doneKey] = state[bufKey].slice(0, idx);
  state[bufKey] = state[bufKey].slice(idx + SENTINEL.length);
  if (state.pending.stdoutDone !== undefined && state.pending.stderrDone !== undefined) {
    const {stdoutDone, stderrDone} = state.pending;
    finish(state, {ok: true, timedOut: false, stdout: stdoutDone,
      stderr: stderrDone, code: 0});
  }
}

function finish(state, result) {
  const p = state.pending;
  if (!p) return;
  clearTimeout(p.timer);
  state.pending = null;
  p.resolve(result);
}

function runNative(state, sourceAbs, budgetMs) {
  if (state.exited) {
    return Promise.resolve({ok: false, timedOut: false,
      spawnError: state.spawnError || "server child exited",
      stdout: "", stderr: "", code: state.exitCode ?? -1});
  }
  return new Promise((resolve) => {
    const pending = {resolve, timedOut: false,
      stdoutDone: undefined, stderrDone: undefined};
    pending.timer = setTimeout(() => {
      pending.timedOut = true;
      state.child.kill("SIGKILL");
      state.pending = null;
      resolve({ok: false, timedOut: true, stdout: state.stdoutBuf,
        stderr: state.stderrBuf, code: -1});
    }, budgetMs);
    state.pending = pending;
    state.child.stdin.write(sourceAbs + "\n");
  });
}

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
      !excludedGroups.has(test.group) &&
      test.spec?.mode === "wast-stream" && test.spec?.sourcePath;
  });
  if ((hasFilter || excludedFiles.size || excludedGroups.size) && tests.length === 0)
    throw new Error("no requested C-engine native tests found");
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
  /* Worker pool: --jobs=N (default 1) dispatches tests across N
   * parallel waste-wast processes. Optional --schedule=longest-first
   * uses --timings=PATH from a prior run to dispatch slow tests first.
   * Output stays buffered by manifest index for console and results.json. */
  const outputs = new Array(tests.length);
  const slotResults = new Array(tests.length);
  let nextToFlush = 0;
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
    let server = spawnServer();
    while (nextIndex < dispatchOrder.length) {
      const index = dispatchOrder[nextIndex++];
      const test = tests[index];
      const identity = test.path || `${test.group}/${test.file}`;
      const expectedFailure = expectedFailures.has(identity);
      const sourceAbs = path.resolve(root, test.spec.sourcePath);
      const budgetMs = timeoutFor(test.group);
      const startedAt = process.hrtime.bigint();
      const result = await runNative(server, sourceAbs, budgetMs);
      if (result.timedOut || server.exited) server = spawnServer();
      const elapsedMs = Number((process.hrtime.bigint() - startedAt) / 1000n) / 1000;
      let parsed = null, parseError = null;
      if (result.stdout) {
        try { parsed = JSON.parse(result.stdout); }
        catch (err) { parseError = String(err?.message || err); }
      }
      const ok = result.ok && parsed && parsed.passed === parsed.total;
      let status;
      if (result.timedOut) { status = "TIMEOUT"; summary.fail++; summary.failures.push(identity); }
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
      if (parsed) {
        record.passed = parsed.passed;
        record.total = parsed.total;
        if (parsed.passed !== parsed.total) record.failureCount = parsed.total - parsed.passed;
      }
      if (result.code !== 0) record.exitCode = result.code;
      slotResults[index] = record;
      let diagnostic = null;
      if (status === "FAIL" || status === "TIMEOUT") {
        const diag = {type: status.toLowerCase(), identity, exitCode: result.code,
          passed: parsed?.passed, total: parsed?.total};
        if (parsed) {
          const failedAsserts = (parsed.assertions || [])
            .filter(a => !a.pass).slice(0, 8)
            .map(a => ({index: a.index, func: a.func, error: a.error}));
          if (failedAsserts.length) diag.failures = failedAsserts;
        }
        if (!parsed && result.stdout) diag.stdoutPreview = result.stdout.slice(0, 400);
        if (parseError) diag.parseError = parseError;
        if (result.stderr) diag.stderrPreview = result.stderr.slice(0, 400);
        if (result.spawnError) diag.spawnError = result.spawnError;
        diagnostic = JSON.stringify(diag, null, 2);
      }
      outputs[index] = {statusLine: `${status} ${identity}`, diagnostic};
      flush();
    }
    if (!server.exited) {
      server.child.stdin.end();
      server.child.kill("SIGTERM");
    }
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
    throw new Error(`${summary.fail} C-engine native test(s) regressed`);
  if (summary.xpass)
    throw new Error(`${summary.xpass} expected-failure test(s) unexpectedly passed: ` +
      summary.unexpectedPasses.join(", "));
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
