"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawnSync} = require("node:child_process");
const {corpusSchedule} = require("./corpus-schedule.cjs");

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "waste-corpus-schedule-"));
const writeJson = (name, value) => {
  const filename = path.join(scratch, name);
  fs.writeFileSync(filename, JSON.stringify(value));
  return filename;
};

try {
  const tests = ["fast", "slow", "medium", "new"].map(file => ({
    group: "synthetic", file: `${file}.wast`, path: `synthetic/${file}.wast`,
  }));
  const timings = writeJson("timings.json", {tests: [
    {identity: tests[0].path, elapsedMs: 1},
    {identity: tests[1].path, elapsedMs: 300},
    {identity: tests[2].path, elapsedMs: 150},
    {identity: "removed/old.wast", elapsedMs: 999},
  ]});
  const flags = ["--schedule=longest-first", `--timings=${timings}`];
  assert.deepEqual(corpusSchedule(tests, []), [0, 1, 2, 3]);
  assert.deepEqual(corpusSchedule(tests, flags), [1, 2, 0, 3]);
  assert.deepEqual(corpusSchedule([tests[0], tests[2]], flags), [1, 0]);
  assert.deepEqual(corpusSchedule([tests[3]], flags), [0]);
  const tied = writeJson("tied.json", {tests: tests.map(test => ({
    identity: test.path, elapsedMs: 1,
  }))});
  assert.deepEqual(corpusSchedule(tests, [flags[0], `--timings=${tied}`]), [0, 1, 2, 3]);
  assert.throws(() => corpusSchedule(tests, [flags[0]]), /requires --timings/);
  assert.throws(() => corpusSchedule(tests, [flags[1]]), /requires --schedule/);
  assert.throws(() => corpusSchedule(tests, ["--schedule=unknown"]), /unknown corpus schedule/);
  for (const history of [{}, {tests: null}, {tests: [null]},
    {tests: [{identity: tests[0].path, elapsedMs: -1}]},
    {tests: [{identity: tests[0].path, elapsedMs: "1"}]},
    {tests: [{identity: tests[0].path, elapsedMs: null}]},
    {tests: [{identity: tests[0].path, elapsedMs: 1},
      {identity: tests[0].path, elapsedMs: 2}]}]) {
    const bad = writeJson("bad.json", history);
    assert.throws(() => corpusSchedule(tests, [flags[0], `--timings=${bad}`]), /corpus timing/);
  }

  // Exercise actual runner entry points with small synthetic backends. Check
  // dispatch order, stable reporting, filtering and replacement after a hang.
  const dispatchLog = path.join(scratch, "dispatch.jsonl");
  const nativeRunner = path.join(scratch, "server.cjs");
  fs.writeFileSync(nativeRunner, `#!/usr/bin/env node
const fs = require("node:fs");
const readline = require("node:readline");
readline.createInterface({input: process.stdin}).on("line", filename => {
  const spec = JSON.parse(fs.readFileSync(filename, "utf8"));
  fs.appendFileSync(${JSON.stringify(dispatchLog)}, JSON.stringify({file:spec.file,pid:process.pid}) + "\\n");
  if (spec.hang) return;
  setTimeout(() => {
    console.log(JSON.stringify({passed:1,total:1,assertions:[{pass:true}]}));
    console.log("###END###");
    console.error("###END###");
  }, spec.delayMs);
});
`, {mode: 0o755});
  const fixtures = tests.slice(0, 3).map((test, index) => {
    const spec = {file: test.file, delayMs: [1, 300, 150][index],
      expectedDispatch: [3, 1, 2][index]};
    const sourcePath = writeJson(test.file, spec);
    return {...test, spec: {mode: "wast-stream", sourcePath, wastText: "", ...spec}};
  });
  const payloadPath = writeJson("payload.json", {tests: fixtures});
  const refreshPayload = () => fs.writeFileSync(payloadPath, JSON.stringify({tests: fixtures}));
  const workerPath = path.join(scratch, "worker.js");
  const workerSrc = `let dispatched = 0;
self.onmessage = async ({data:{testSpec}}) => {
  dispatched++;
  if (testSpec.hang) await new Promise(() => {});
  await new Promise(resolve => setTimeout(resolve, testSpec.delayMs));
  self.postMessage({type:"done", file:testSpec.file,
    results:[{pass:dispatched === testSpec.expectedDispatch}]});
};`;
  const writeWorker = () => fs.writeFileSync(workerPath, workerSrc);
  writeWorker();
  const run = (runtime, payload, extra = []) => {
    const resultsPath = path.join(scratch, `${runtime}-results.json`);
    const child = spawnSync(process.execPath, [
      path.join(__dirname, `c-engine-${runtime}-runtime.cjs`), payload,
      ...flags, `--results=${resultsPath}`,
      ...(runtime === "browser" ? [`--worker-source=${workerPath}`] : []), ...extra,
    ], {encoding: "utf8", timeout: 30000});
    assert.equal(child.error, undefined, child.error?.message);
    return {...child, results: JSON.parse(fs.readFileSync(resultsPath, "utf8"))};
  };
  const identities = fixtures.map(test => test.path);
  const checkOrder = result => {
    assert.deepEqual(result.results.tests.map(test => test.identity), identities);
    const statusLines = result.stdout.trim().split("\n")
      .map(line => line.match(/^(?:PASS|XFAIL|XPASS|FAIL|TIMEOUT) (\S+\.wast)$/))
      .filter(Boolean);
    assert.deepEqual(statusLines.map(match => match[1]), identities);
  };
  let result = run("native", payloadPath, [`--runner=${nativeRunner}`, "--jobs=2"]);
  assert.equal(result.status, 0, result.stderr + result.stdout);
  checkOrder(result);
  let dispatches = fs.readFileSync(dispatchLog, "utf8").trim().split("\n").map(JSON.parse);
  assert.deepEqual(dispatches.slice(0, 2).map(entry => entry.file).sort(), ["medium.wast", "slow.wast"]);
  assert.equal(dispatches[2].file, "fast.wast");
  result = run("browser", payloadPath);
  assert.equal(result.status, 0, result.stderr + result.stdout);
  checkOrder(result);

  result = run("native", payloadPath, [`--runner=${nativeRunner}`, "--exclude=slow.wast"]);
  assert.equal(result.status, 0, result.stderr + result.stdout);
  assert.deepEqual(result.results.tests.map(test => test.identity), [identities[0], identities[2]]);
  fixtures[0].spec.expectedDispatch = 2;
  fixtures[2].spec.expectedDispatch = 1;
  refreshPayload();
  writeWorker();
  result = run("browser", payloadPath, ["--exclude=slow.wast"]);
  assert.equal(result.status, 0, result.stderr + result.stdout);
  assert.deepEqual(result.results.tests.map(test => test.identity), [identities[0], identities[2]]);

  fixtures[1].spec.hang = true;
  writeJson(fixtures[1].file, fixtures[1].spec);
  refreshPayload();
  fs.writeFileSync(dispatchLog, "");
  writeWorker();
  for (const [runtime, payload, extra] of [
    ["native", payloadPath, [`--runner=${nativeRunner}`]],
    ["browser", payloadPath, []],
  ]) {
    result = run(runtime, payload, [...extra, "--timeout-group=synthetic:1000"]);
    assert.equal(result.status, 1, result.stderr);
    checkOrder(result);
    assert.deepEqual(result.results.tests.map(test => test.status), ["PASS", "TIMEOUT", "PASS"]);
    assert.equal(result.results.summary.fail, 1);
    assert.deepEqual(result.results.summary.failures, [identities[1]]);
  }
  dispatches = fs.readFileSync(dispatchLog, "utf8").trim().split("\n").map(JSON.parse);
  assert.deepEqual(dispatches.map(entry => entry.file), ["slow.wast", "medium.wast", "fast.wast"]);
  assert.notEqual(dispatches[0].pid, dispatches[1].pid);
  assert.equal(dispatches[1].pid, dispatches[2].pid);
  console.log("PASS corpus duration scheduling, ordered results, filtering and timeout replacement in both runners");
} finally {
  fs.rmSync(scratch, {recursive: true, force: true});
}
