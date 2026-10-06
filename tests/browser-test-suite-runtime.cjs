#!/usr/bin/env node
"use strict";
const {config, withConfig} = require("./runtime-config.cjs");

/* Host-boundary gate only. Guest behavior is asserted by authored WAST. The
 * controller and production worker are exactly those used in the offline page. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const {Worker} = require("node:worker_threads");
const {installedVfs, treeVfs} = require("./vfs-package.cjs");
const root = path.resolve(__dirname, "..");
const frontend = path.join(root, "src/html-rt/src");
const workerSource = withConfig(fs.readFileSync(path.join(frontend, "worker.js"), "utf8"));
const context = vm.createContext({setTimeout, clearTimeout, performance, TextEncoder, TextDecoder});
vm.runInContext(withConfig(fs.readFileSync(path.join(frontend, "test-suite.js"), "utf8")), context);
let live = 0;
function createWorker() {
  const native = new Worker(`
    const {parentPort} = require('node:worker_threads');
    global.self = {postMessage: data => parentPort.postMessage(data)};
    parentPort.on('message', data => self.onmessage({data}));
    ${workerSource}
  `, {eval: true});
  live++;
  native.once("exit", () => { live--; });
  const adapter = {postMessage: data => native.postMessage(data), terminate: () => native.terminate()};
  native.on("message", data => adapter.onmessage?.({data}));
  native.on("error", error => adapter.onerror?.({message: error.message}));
  return adapter;
}
const wasmBytes = fs.readFileSync(path.join(root, "build/html-rt/waste-wast.wasm"));
function suite(vfs, expectedFailures = "", factory = createWorker) {
  return new context.WasteTestSuite({wasmBytes, vfs, expectedFailures, createWorker: factory});
}
function test(name, source, changes = {}) {
  return {record: {id: "synthetic/" + name, group: "synthetic", path: "/root/waste/tests/synthetic/" + name,
    executionSpec: {mode: "wast-stream", file: name}, assets: [], unsupported: false,
    expectFailure: false, ...changes}, source};
}
const read = name => fs.readFileSync(path.join(root, "tests", name));
const definitions = [test("pass.wast", read("test-suite-pass.wast")),
  test("fail.wast", read("test-suite-fail.wast")),
  test("writer.wast", read("test-suite-kernel-isolation-a.wast")),
  test("reader.wast", read("test-suite-kernel-isolation-b.wast")),
  test("memory-writer.wast", read("diy-posix-test/spectest-isolation-a.wast")),
  test("memory-reader.wast", read("diy-posix-test/spectest-isolation-b.wast")),
  test("companion.wast", read("test-suite-companion.wast"), {assets: [{kind: "vfs-file",
    path: "/root/waste/tests/.support/tail", mountPath: "/companion", mode: 0o644}]}),
  test("skip.wast", Buffer.from("unparseable"), {unsupported: true, unsupportedReason: "legacy"}),
  test("compat.wast", Buffer.from("unparseable"), {executionSpec: {mode: "browser-native", file: "compat.wast"}})];
function filesystem(items = definitions, extra = {"/root/waste/tests/.support/tail": Buffer.from("tail")}, transform) {
  const manifest = {format: 1, tests: items.map(item => structuredClone(item.record))};
  transform?.(manifest);
  const files = new Map([["/root/waste/tests/manifest.json", Buffer.from(JSON.stringify(manifest))],
    ...items.filter(item => item.source).map(item => [item.record.path, item.source]), ...Object.entries(extra)]);
  const directories = new Set(["/", "/root", "/tmp"]);
  for (const name of files.keys()) {
    for (let parent = path.posix.dirname(name); parent !== "/"; parent = path.posix.dirname(parent))
      directories.add(parent);
  }
  const names = [...directories, ...files.keys()].sort((a, b) => a.split("/").length - b.split("/").length || a.localeCompare(b));
  return installedVfs({version: 1, entries: names.map((name, index) => ({path: name,
    role: directories.has(name) ? "directory" : "file", kind: directories.has(name) ? 2 : 1,
    mode: directories.has(name) ? 0o755 : 0o644, uid: 0, gid: 0,
    size: files.get(name)?.length || 0, inode: index + 1, mtime_sec: 0, mtime_nsec: 0}))}, name => files.get(name));
}
const plain = value => JSON.parse(JSON.stringify(value));

(async () => {
  const groupFlag = process.argv.slice(2).find(arg => arg.startsWith("--installed-group="));
  const installedAll = process.argv.includes("--installed-all");
  if (groupFlag || installedAll) {
    const group = groupFlag && groupFlag.slice("--installed-group=".length);
    const runner = suite(treeVfs(path.join(root, "src/vfs")),
      fs.readFileSync(path.join(root, "tests/browser-corpus-expected-failures.txt"), "utf8"));
    const report = await runner.run(installedAll ? {jobs: 4, timeoutMs: 60000} :
      {groups: [group], jobs: 2, timeoutMs: 60000});
    const resultFlag = process.argv.slice(2).find(arg => arg.startsWith("--results="));
    if (resultFlag) fs.writeFileSync(resultFlag.slice("--results=".length), JSON.stringify(report, null, 2) + "\n");
    assert.equal(report.exitCode, 0, JSON.stringify(report.summary));
    assert(report.tests.length > 0 && (installedAll || report.tests.every(test => test.group === group)));
    if (installedAll) assert.deepEqual(plain(report.tests.map(test => test.identity)),
      plain((await runner.list()).map(test => test.identity)));
    console.log(`PASS browser installed ${installedAll ? "full corpus" : "group " + group}: ${report.tests.length} tests, ` +
      `${report.tests.reduce((total, test) => total + (test.total || 0), 0)} assertions`);
    return;
  }
  const baseline = "synthetic/fail.wast";
  for (const jobs of [1, 3]) {
    const runner = suite(filesystem(), baseline);
    const listed = await runner.list();
    assert.equal(listed.length, definitions.length);
    assert.equal(listed.filter(test => test.skipReason).length, 2);
    const report = await runner.run({jobs, timeoutMs: 5000});
    assert.deepEqual(plain(report.summary), {pass: 6, fail: 0, xfail: 1, xpass: 0, skip: 2, failures: [], unexpectedPasses: []});
    assert.equal(report.exitCode, 0);
    assert.deepEqual(plain(report.tests.map(record => record.identity)), definitions.map(item => item.record.id));
    assert.equal(report.tests[0].total, 3);
    assert(report.tests[0].browserReport.results.every(result => result.pass));
    assert.equal((await runner.run({files: ["pass.wast"], jobs})).tests.length, 1);
    assert.equal((await runner.run({excludeFiles: ["fail.wast"], jobs})).summary.xfail, 0);
    await assert.rejects(runner.run({files: ["unknown"]}), /Unknown test/);
    await assert.rejects(runner.run({jobs: 0}), /Invalid jobs/);
    await assert.rejects(runner.run({timeoutGroups: {unknown: 10}}), /Unknown timeout/);
  }
  // Keep intentional in-script aliases while isolating host memory across
  // scripts, independent of manifest order and concurrent scheduling.
  const memoryTests = definitions.slice(4, 6);
  for (const ordered of [memoryTests, [...memoryTests].reverse()]) {
    for (const jobs of [1, 3]) {
      const report = await suite(filesystem(ordered)).run({jobs});
      assert.deepEqual(plain(report.tests.map(test => test.identity)), ordered.map(item => item.record.id));
      for (const test of report.tests) {
        const expected = test.identity.endsWith("memory-writer.wast") ? 4 : 1;
        assert.equal(test.status, "PASS");
        assert.equal(test.passed, expected);
        assert.equal(test.total, expected);
      }
    }
  }
  // The guest ABI delegates to this controller, including early cancellation.
  const wire = (...args) => {
    const bytes = Buffer.concat([Buffer.from([1,0,0,0]), Buffer.from(args.map(arg => arg + "\0").join(""))]);
    return bytes;
  };
  const decodeReply = bytes => {
    const header = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    assert.equal(header.getUint32(0, true), 1);
    const out = header.getUint32(8, true), json = header.getUint32(12, true);
    assert.equal(bytes.length, 16 + out + json);
    return {code:header.getUint32(4, true), output:new TextDecoder().decode(bytes.subarray(16, 16 + out)),
      report:json ? JSON.parse(new TextDecoder().decode(bytes.subarray(16 + out))) : null};
  };
  const guestRunner = suite(filesystem(), baseline);
  const guestList = decodeReply(await guestRunner.guestCommand(wire("--list", "--json", "pass.wast")));
  assert.equal(guestList.code, 0);
  assert.equal(JSON.parse(guestList.output).count, 1);
  const guestPass = decodeReply(await guestRunner.guestCommand(wire("--json", "pass.wast")));
  assert.equal(guestPass.report.summary.pass, 1);
  assert.deepEqual(JSON.parse(guestPass.output), guestPass.report);
  for (const args of [["--jobs=" + (config.SUITE_BROWSER_MAX_JOBS + 1)], ["--vfs-root=/host"], ["--results=/host"], ["missing.wast"]])
    assert.equal(decodeReply(await guestRunner.guestCommand(wire(...args))).code, 2);
  const early = suite(filesystem(), baseline);
  const pending = early.guestCommand(wire("--list"));
  early.cancel();
  const earlyReply = decodeReply(await pending);
  assert.equal(earlyReply.code, 130);
  assert.equal(earlyReply.report.cancelledBeforeEnumeration, true);
  const nested = await suite(filesystem([test("nested.wast", read("guest-test-nested-capability.wast"))])).run();
  assert.equal(nested.tests[0].passed, 1);
  assert.equal(nested.summary.pass, 1);
  const rendererUnavailable = await suite(filesystem([
    test("render-capability.wast", read("engine-regressions/render-capability.wast"))])).run();
  assert.equal(rendererUnavailable.summary.pass, 1);
  assert.equal(rendererUnavailable.tests[0].passed, 2);
  const minLiteral = await suite(filesystem([test("i64-min.wast", read("test-suite-i64-min-literal.wast"))])).run();
  assert.equal(minLiteral.tests[0].passed, 2);
  assert.equal(minLiteral.summary.pass, 1);
  const xpass = await suite(filesystem([definitions[0]]), "synthetic/pass.wast").run();
  assert.equal(xpass.tests[0].status, "XPASS");
  assert.equal(xpass.exitCode, 1);
  for (const expected of ["synthetic/unknown.wast", baseline + "\n" + baseline])
    await assert.rejects(suite(filesystem(), expected).list(), /Unknown or duplicate/);
  for (const mutate of [m => { m.format = 2; }, m => { m.tests[0].path = "/tmp/test"; },
    m => { m.tests[1].id = m.tests[0].id; }, m => { m.tests[0].expectFailure = "true"; }])
    await assert.rejects(suite(filesystem(definitions, {}, mutate)).list());
  const mounted = suite(treeVfs(path.join(root, "src/vfs")),
    fs.readFileSync(path.join(root, "tests/browser-corpus-expected-failures.txt"), "utf8"));
  const mountedTests = await mounted.list();
  const compatibility = mountedTests.find(test =>
    test.identity === "diy-posix-test/posix-kernel.wast");
  assert(compatibility, "mounted POSIX kernel fixture is listed");
  assert.equal(compatibility.skipReason, "",
    "Bash installed-test runner enables its packaged browser compatibility mode");
  const compatibilityReport = await mounted.run({
    files: ["diy-posix-test/posix-kernel.wast"], jobs: 1, timeoutMs: 30000,
  });
  assert.deepEqual(plain(compatibilityReport.summary), {
    pass: 1, fail: 0, xfail: 0, xpass: 0, skip: 0,
    failures: [], unexpectedPasses: [],
  });
  assert.equal(compatibilityReport.tests[0].passed, 7);
  assert.equal(compatibilityReport.tests[0].total, 7);
  assert.equal(compatibilityReport.tests[0].browserReport.setup.passed, 1);
  assert.equal(compatibilityReport.tests[0].browserReport.setup.total, 1);
  for (const item of [test("missing.wast", null, {expectFailure: true}),
    test("missing-companion.wast", definitions[6].source, {assets: definitions[6].record.assets, expectFailure: true})]) {
    const report = await suite(filesystem([item], {})).run();
    assert.equal(report.tests[0].status, "FAIL");
    assert(report.tests[0].error.includes("cannot"));
    assert.equal(report.exitCode, 1);
  }
  const timeoutItems = [test("timeout.wast", read("test-suite-timeout.wast")), definitions[0]];
  const timeout = await suite(filesystem(timeoutItems)).run({jobs: 1, timeoutMs: 3000});
  assert.deepEqual(plain(timeout.tests.map(record => record.status)), ["TIMEOUT", "PASS"]);
  const cancelRunner = suite(filesystem(timeoutItems));
  await cancelRunner.list();
  const cancelPromise = cancelRunner.run({jobs: 1, timeoutMs: 5000});
  setTimeout(() => cancelRunner.cancel(), 100);
  const cancelled = await cancelPromise;
  assert.deepEqual(plain(cancelled.tests.map(record => record.status)), ["CANCELLED", "CANCELLED"]);
  assert.equal(cancelled.exitCode, 130);
  assert.equal((await cancelRunner.run({files: ["pass.wast"]})).summary.pass, 1);
  // A worker stuck before posting even one message must still be bounded.
  const stuckFactory = () => ({postMessage() {}, terminate() {}});
  const stuck = suite(filesystem([definitions[0]]), "synthetic/pass.wast", stuckFactory);
  stuck.catalogue = [{identity: "synthetic/pass.wast", group: "synthetic", file: "pass.wast", expectedFailure: true}];
  assert.equal((await stuck.run({timeoutMs: 20})).tests[0].status, "TIMEOUT");
  const unicode = suite(filesystem([test("π.wast", definitions[0].source)]));
  assert.equal((await unicode.run()).tests[0].identity, "synthetic/π.wast");
  const yielded = await suite(filesystem([test("yield.wast", read("test-suite-yield.wast"),
    {expectFailure: true})])).run({timeoutMs: 15000});
  assert.equal(yielded.tests[0].status, "XFAIL");
  assert.deepEqual(plain(yielded.tests[0].browserReport.results.map(result => [result.func, result.pass])),
    [["value", true], ["trap", true], ["value", false]]);
  const timed = await suite(filesystem([test("select.wast", read("test-suite-select-timeout.wast"))])).run();
  assert.equal(timed.tests[0].status, "PASS");
  assert.equal(timed.tests[0].total, 2);
  const streamItems = [test("stream.wast", read("test-suite-command-stream.wast")),
    test("stream-fail.wast", read("test-suite-command-stream-fail.wast")),
    test("stream-recovery.wast", read("test-suite-command-stream-recovery.wast"))];
  const stream = await suite(filesystem(streamItems)).run();
  assert.deepEqual(plain(stream.tests.map(record => record.status)), ["PASS", "FAIL", "FAIL"]);
  assert.equal(stream.tests[0].total, 18);
  assert.deepEqual(plain(stream.tests[1].browserReport.results.map(result => [result.func, result.pass])),
    [["(module)", true], ["answer", false], ["answer", true]]);
  assert.deepEqual(plain(stream.tests[2].browserReport.results.map(result => [result.func, result.pass])),
    [["answer", true], ["(parse)", false], ["answer", true]]);
  const setupCases = [["empty", 0, 0, 0], ["valid", 3, 3, 0],
    ["invalid", 1, 0, 0], ["unlinkable", 1, 0, 0], ["start-trap", 1, 0, 0],
    ["definition", 1, 0, 0], ["encode", 1, 0, 0], ["recovery", 4, 2, 3]];
  const setupItems = setupCases.map(([name]) => test(`setup-${name}.wast`, read(`test-suite-setup-${name}.wast`)));
  const setupReport = await suite(filesystem(setupItems)).run();
  assert.equal(setupReport.exitCode, 1);
  setupReport.tests.forEach((test, i) => {
    const [name, total, passed, assertions] = setupCases[i];
    const detail = test.browserReport;
    assert.equal(test.status, passed === total ? "PASS" : "FAIL");
    assert(detail.completed && detail.setup.complete);
    assert.equal(detail.setup.total, total);
    assert.equal(detail.setup.passed, passed);
    assert.equal(detail.setup.failures.length, total - passed);
    assert.equal(detail.total, assertions);
    assert.equal(detail.passed, assertions);
    if (passed !== total) {
      const failure = detail.setup.failures[0];
      assert.equal(failure.line, name === "recovery" ? 4 : 2);
      assert(failure.status !== 0 && failure.error);
      assert.equal(failure.phase, ["definition", "encode"].includes(name) ? name : "load");
      if (name === "recovery") assert.deepEqual(plain(detail.setup.failures.map(f => [f.line, f.phase])),
        [[4, "load"], [5, "encode"]]);
    }
  });
  const setupPolicy = await suite(filesystem(setupItems),
    "synthetic/setup-invalid.wast\nsynthetic/setup-valid.wast\n").run();
  assert.equal(setupPolicy.tests[1].status, "XPASS");
  assert.equal(setupPolicy.tests[2].status, "XFAIL");
  const segments = await suite(filesystem([test("segment-indices.wast",
    read("test-suite-segment-indices.wast"))])).run();
  assert.equal(segments.tests[0].status, "PASS", JSON.stringify(segments.tests[0]));
  assert.equal(segments.tests[0].passed, 64);
  assert.equal(segments.tests[0].total, 64);
  assert.equal(segments.tests[0].browserReport.setup.passed, 2);
  assert.equal(segments.tests[0].browserReport.setup.total, 2);
  const capacity = [];
  for (const [kind, label] of [["data", "data"], ["elem", "element"]]) {
    const limit = config[kind === "data" ? "WAST_MAX_DATA_SEGS" : "WAST_MAX_ELEM_SEGS"];
    const segment = kind === "data" ? '(data "x")' : '(elem func)';
    const source = Buffer.from("(module\n" + (segment + "\n").repeat(limit + 1) + ")");
    for (const wrapped of [false, true]) {
      const probe = wrapped ? Buffer.concat([Buffer.from("(assert_invalid "), source,
        Buffer.from(' "invalid")')]) : source;
      const report = await suite(filesystem([test("capacity.wast", probe)])).run();
      assert.equal(report.tests[0].status, "FAIL");
      assert(report.tests[0].browserReport.results.some(result =>
        result.func === "(parse)" && result.error.includes(`${label} segment capacity exceeded (${limit})`)));
      capacity.push(report);
    }
  }
  const elements = await suite(filesystem([test("element-types.wast",
    read("test-suite-element-types.wast"))])).run();
  assert.equal(elements.tests[0].status, "PASS", JSON.stringify(elements.tests[0]));
  assert.equal(elements.tests[0].passed, 82);
  assert.equal(elements.tests[0].total, 82);
  assert.equal(elements.tests[0].browserReport.setup.passed, 11);
  assert.equal(elements.tests[0].browserReport.setup.total, 11);
  const tableItems = [test("table64-writer.wast", read("test-suite-spectest-table64.wast")),
    test("table64-reader.wast", read("test-suite-spectest-table64-isolation.wast"))];
  const tables = [];
  for (const ordered of [tableItems, [...tableItems].reverse()]) {
    for (const jobs of [1, 3]) {
      const report = await suite(filesystem(ordered)).run({jobs});
      assert.deepEqual(plain(report.tests.map(t => t.identity)), ordered.map(t => t.record.id));
      for (const record of report.tests) {
        const writer = record.identity.endsWith("writer.wast");
        assert.equal(record.status, "PASS", JSON.stringify(record));
        assert.equal(record.passed, writer ? 36 : 3);
        assert.equal(record.total, writer ? 36 : 3);
        assert.equal(record.browserReport.setup.passed, writer ? 3 : 1);
        assert.equal(record.browserReport.setup.total, writer ? 3 : 1);
      }
      tables.push(report);
    }
  }
  const flatBulk = await suite(filesystem([test("flat-bulk.wast",
    read("test-suite-flat-bulk.wast"))])).run();
  assert.equal(flatBulk.tests[0].status, "PASS", JSON.stringify(flatBulk.tests[0]));
  assert.equal(flatBulk.tests[0].passed, 190);
  assert.equal(flatBulk.tests[0].total, 190);
  assert.equal(flatBulk.tests[0].browserReport.setup.passed, 3);
  assert.equal(flatBulk.tests[0].browserReport.setup.total, 3);
  const tableBulk = await suite(filesystem([test("table-copy-fill.wast",
    read("test-suite-table-copy-fill.wast"))])).run();
  assert.equal(tableBulk.tests[0].status, "PASS", JSON.stringify(tableBulk.tests[0]));
  assert.equal(tableBulk.tests[0].passed, 611);
  assert.equal(tableBulk.tests[0].total, 611);
  assert.equal(tableBulk.tests[0].browserReport.setup.passed, 7);
  assert.equal(tableBulk.tests[0].browserReport.setup.total, 7);
  const tail = await suite(filesystem([test("tail.wast", Buffer.concat([
    read("test-suite-command-stream.wast"), Buffer.from("\n(module")]))])).run();
  assert.equal(tail.tests[0].status, "FAIL");
  assert.equal(tail.tests[0].passed, 18);
  assert.equal(tail.tests[0].total, 19);
  assert.equal(tail.tests[0].browserReport.completed, false);
  const names = await suite(filesystem([test("names.wast", read("test-suite-result-names.wast"))])).run();
  assert.deepEqual(plain(names.tests[0].browserReport.results.map(result => [result.func, result.pass])),
    [["abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqr", true], ["\ufeff", true]]);
  await new Promise(resolve => setTimeout(resolve, 200));
  assert.equal(live, 0, "all isolated workers must terminate");
  const setupResultsFlag = process.argv.find(arg => arg.startsWith("--setup-results="));
  if (setupResultsFlag) fs.writeFileSync(setupResultsFlag.slice("--setup-results=".length),
    JSON.stringify({setupReport, setupPolicy, segments, capacity, elements, tables, flatBulk, tableBulk, tail}, null, 2) + "\n");
  console.log("PASS browser mounted batch: assertions, isolation, companions, browser-native compatibility, policy, selection, deadlines and cancellation");
})().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
