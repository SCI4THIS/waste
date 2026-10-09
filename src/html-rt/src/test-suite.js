"use strict";

/* Browser events and worker lifetimes belong here. Manifest decoding, mounted
 * source/companion reads, assertions and guest execution belong to C. */
var WasteTestSuite = class {
  constructor({wasmBytes, vfs, createWorker, expectedFailures = ""}) {
    this.wasmBytes = wasmBytes;
    this.vfs = vfs;
    this.createWorker = createWorker;
    this.expectedFailures = expectedFailures;
    this.browserNativeSpecs = new Map();
    const inventory = JSON.parse(new TextDecoder().decode(vfs.inventory));
    const manifestIndex = inventory.entries.findIndex(entry =>
      entry.path === "/root/test/manifest.json");
    const manifestFile = vfs.files.find(file => file.index === manifestIndex);
    if (manifestIndex >= 0 && manifestFile) {
      const manifest = JSON.parse(new TextDecoder().decode(manifestFile.bytes));
      for (const test of manifest.tests || []) {
        const spec = test.executionSpec;
        if (spec?.mode === "browser-native" && Array.isArray(spec.modules) &&
            spec.modules.length > 0 && Array.isArray(spec.steps))
          this.browserNativeSpecs.set(test.id, spec);
      }
    }
    this.catalogue = null;
    this.running = null;
    this.guestPending = null;
  }

  request(suite, timeoutMs, state) {
    return new Promise((resolve, reject) => {
      let worker = null, finished = false, output = "";
      const complete = (value, error) => {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        state?.active.delete(cancel);
        worker?.terminate();
        if (error) reject(error);
        else resolve({...value, outputPreview: output.slice(0, 4096)});
      };
      const cancel = () => complete({cancelled: true});
      const timer = setTimeout(() => complete({timedOut: true}), timeoutMs);
      state?.active.add(cancel);
      if (state?.cancelled) { cancel(); return; }
      Promise.resolve().then(() => this.createWorker()).then(created => {
        worker = created;
        if (finished) { worker.terminate(); return; }
        worker.onerror = event => complete({error: event.message || "worker crashed"});
        worker.onmessage = ({data}) => {
          if (data.type === "output") {
            output += data.text;
            if (output.length > WASTE_CONFIG.SUITE_OUTPUT_MAX_BYTES)
              complete({error: "test output exceeded configured limit"});
          } else if (data.type === "suite-list" || data.type === "done") complete(data);
          else if (data.type === "host-upload-request" || data.type === "host-download")
            complete({error: "batch test requested an interactive host transfer"});
        };
        if (suite.mode === "run" && suite.browserNativeSpec) {
          worker.postMessage({type: "browser-native-start",
            testSpec: suite.browserNativeSpec});
        } else {
          worker.postMessage({type: "start", wasmBytes: this.wasmBytes,
            vfs: this.vfs, suite,
            executionLimits: {timeoutMs, pumpQuantumMs: 10}});
        }
      }).catch(error => complete(null, error));
    });
  }

  async list(state) {
    if (this.catalogue) return this.catalogue;
    const response = await this.request({mode: "list"}, 30000, state);
    if (!Array.isArray(response.tests))
      throw new Error(response.error || "Mounted manifest enumeration timed out");
    const expected = new Set();
    for (const line of this.expectedFailures.split(/\r?\n/)) {
      const identity = line.split("#", 1)[0].trim();
      if (!identity) continue;
      if (expected.has(identity) || !response.tests.some(test => test.identity === identity))
        throw new Error(`Unknown or duplicate expected-failure identity: ${identity}`);
      expected.add(identity);
    }
    this.catalogue = response.tests.map(test => {
      const browserNative = this.browserNativeSpecs.has(test.identity) &&
        test.mode === "browser-native" && !test.unsupported;
      return {...test,
        skipReason: browserNative ? "" : test.skipReason,
        expectedFailure: test.expectedFailure || expected.has(test.identity)};
    });
    return this.catalogue;
  }

  select(tests, {groups = [], files = [], excludeGroups = [], excludeFiles = []} = {}) {
    const fileMatch = (test, name) => [test.identity, test.path, test.file,
      test.identity.split("/").pop()].includes(name);
    for (const [names, match] of [[groups, (test, name) => test.group === name],
      [excludeGroups, (test, name) => test.group === name], [files, fileMatch], [excludeFiles, fileMatch]]) {
      if (!Array.isArray(names) || names.some(name => typeof name !== "string" ||
          !tests.some(test => match(test, name)))) throw new Error("Unknown test selection");
    }
    const selected = tests.filter(test =>
      (!(groups.length || files.length) || groups.includes(test.group) || files.some(name => fileMatch(test, name))) &&
      !excludeGroups.includes(test.group) && !excludeFiles.some(name => fileMatch(test, name)));
    if (!selected.length) throw new Error("No requested browser tests found");
    return selected;
  }

  static guestReply(code, output, report = null) {
    const encoder = new TextEncoder();
    const out = encoder.encode(output), json = report ? encoder.encode(JSON.stringify(report) + "\n") : new Uint8Array();
    if (16 + out.length + json.length > WASTE_CONFIG.SUITE_GUEST_REPLY_MAX_BYTES) throw new Error("Batch response exceeds configured limit");
    const bytes = new Uint8Array(16 + out.length + json.length);
    const header = new DataView(bytes.buffer);
    [1, code, out.length, json.length].forEach((value, i) => header.setUint32(i*4, value, true));
    bytes.set(out, 16); bytes.set(json, 16 + out.length);
    return bytes;
  }

  async guestCommand(request) {
    let pending = null;
    try {
      if (this.running || this.guestPending) throw new Error("A batch is already running");
      pending = {cancelled: false, active: new Set()};
      this.guestPending = pending;
      const bytes = new Uint8Array(request);
      if (bytes.length < 4 || bytes.length > WASTE_CONFIG.SUITE_GUEST_REQUEST_MAX_BYTES || new DataView(bytes.buffer, bytes.byteOffset).getUint32(0, true) !== 1)
        throw new Error("Invalid batch request");
      const strings = new TextDecoder("utf-8", {fatal: true, ignoreBOM: true}).decode(bytes.subarray(4));
      if (strings && !strings.endsWith("\0")) throw new Error("Unterminated batch arguments");
      const args = strings ? strings.slice(0,-1).split("\0") : [];
      if (args.length > WASTE_CONFIG.SUITE_GUEST_MAX_ARGS || args.some(arg => !arg)) throw new Error("Invalid batch arguments");
      const options = {jobs: WASTE_CONFIG.SUITE_GUEST_DEFAULT_JOBS, groups: [], files: [], excludeFiles: [], excludeGroups: [], timeoutGroups: Object.create(null)};
      let list = false, json = false;
      const ms = text => {
        if (!/^[0-9]+$/.test(text) || Number(text) < 1 || Number(text) > WASTE_CONFIG.EXECUTION_MAX_TIMEOUT_MS)
          throw new Error("Invalid execution deadline");
        return Number(text);
      };
      for (const arg of args) {
        if (arg === "--list") list = true;
        else if (arg === "--json") json = true;
        else if (arg.startsWith("--group=")) options.groups.push(arg.slice(8));
        else if (arg.startsWith("--exclude=")) options.excludeFiles.push(arg.slice(10));
        else if (arg.startsWith("--exclude-group=")) options.excludeGroups.push(arg.slice(16));
        else if (arg.startsWith("--jobs=")) options.jobs = ms(arg.slice(7));
        else if (arg.startsWith("--timeout-ms=")) options.timeoutMs = ms(arg.slice(13));
        else if (arg.startsWith("--timeout-group=")) {
          const value = arg.slice(16), colon = value.lastIndexOf(":");
          if (colon <= 0) throw new Error("Invalid timeout group");
          options.timeoutGroups[value.slice(0, colon)] = ms(value.slice(colon+1));
        } else if (arg.startsWith("-")) throw new Error("Unknown batch option");
        else options.files.push(arg);
      }
      if (options.jobs > WASTE_CONFIG.SUITE_BROWSER_MAX_JOBS) throw new Error("Invalid jobs");
      const catalogue = await this.list(pending);
      if (pending.cancelled) throw new Error("Batch cancelled before enumeration");
      const selected = this.select(catalogue, options);
      if (Object.keys(options.timeoutGroups).some(group => !catalogue.some(test => test.group === group)))
        throw new Error("Unknown timeout group");
      let report, output;
      if (list) {
        report = {tests: selected, count: selected.length,
          expectedFailures: selected.filter(test => test.expectedFailure).length,
          skip: selected.filter(test => test.skipReason).length, exitCode: 0};
        output = selected.map(test => (test.skipReason ? "SKIP " : test.expectedFailure ? "XFAIL? " : "PASS? ") +
          test.identity + (test.skipReason ? ": " + test.skipReason : "") + "\n").join("");
      } else {
        report = await this.run(options);
        const s = report.summary;
        output = report.tests.map(test => test.status + " " + test.identity + "\n").join("") +
          `Suite: ${s.pass} PASS, ${s.fail} FAIL, ${s.xfail} XFAIL, ${s.xpass} XPASS, ${s.skip} SKIP\n`;
      }
      return WasteTestSuite.guestReply(report.exitCode, json ? JSON.stringify(report) + "\n" : output, report);
    } catch (error) {
      if (pending?.cancelled) return WasteTestSuite.guestReply(130, "waste-test: cancelled before enumeration\n", {
        summary: {pass: 0, fail: 0, xfail: 0, xpass: 0, skip: 0, failures: [], unexpectedPasses: []},
        tests: [], exitCode: 130, cancelledBeforeEnumeration: true});
      return WasteTestSuite.guestReply(2, "waste-test: " + (error.message || String(error)) + "\n");
    } finally {
      if (this.guestPending === pending) this.guestPending = null;
    }
  }

  cancel() {
    for (const state of [this.guestPending, this.running]) {
      if (!state) continue;
      state.cancelled = true;
      for (const cancel of [...state.active]) cancel();
    }
  }

  async run(options = {}, onResult = () => {}) {
    if (this.running) throw new Error("A batch is already running");
    const {jobs = WASTE_CONFIG.SUITE_GUEST_DEFAULT_JOBS, timeoutMs, timeoutGroups = {}} = options;
    const validMs = value => Number.isInteger(value) && value > 0 && value <= WASTE_CONFIG.EXECUTION_MAX_TIMEOUT_MS;
    if (!Number.isInteger(jobs) || jobs < 1 || jobs > WASTE_CONFIG.SUITE_BROWSER_MAX_JOBS ||
        (timeoutMs !== undefined && !validMs(timeoutMs)) ||
        !timeoutGroups || typeof timeoutGroups !== "object" || Array.isArray(timeoutGroups) ||
        Object.values(timeoutGroups).some(value => !validMs(value)))
      throw new Error("Invalid jobs or execution deadline");
    const state = {cancelled: false, active: new Set()};
    this.running = state;
    try {
      const tests = await this.list();
      if (Object.keys(timeoutGroups).some(group => !tests.some(test => test.group === group)))
        throw new Error("Unknown timeout group");
      const selected = this.select(tests, options);
      const records = new Array(selected.length);
      let next = 0;
      const consume = async () => {
        while (next < selected.length) {
          const index = next++, test = selected[index];
          const started = performance.now();
          let record = {...test, elapsedMs: 0};
          if (test.skipReason) record.status = "SKIP";
          else if (state.cancelled) record.status = "CANCELLED";
          else {
            const budget = timeoutGroups[test.group] ?? timeoutMs ??
              (test.group === "wasm-spec/core" ? WASTE_CONFIG.SUITE_TIMEOUT_CORE_MS :
                ["wasm-spec/core/simd", "wasm-spec/core/bulk-memory", "wasm-spec/core/memory64"].includes(test.group) ? WASTE_CONFIG.SUITE_TIMEOUT_HEAVY_MS : WASTE_CONFIG.SUITE_TIMEOUT_DEFAULT_MS);
            let response;
            try { response = await this.request({mode: "run", identity: test.identity,
              browserNativeSpec: this.browserNativeSpecs.get(test.identity)}, budget, state); }
            catch (error) { response = {error: error.message || String(error)}; }
            record.elapsedMs = performance.now() - started;
            if (response.cancelled) record.status = "CANCELLED";
            else if (response.timedOut) record.status = "TIMEOUT";
            else if (response.error || !Array.isArray(response.results) ||
                typeof response.ok !== "boolean" ||
                !Number.isInteger(response.total) || response.total !== response.results.length ||
                response.passed !== response.results.filter(result => result.pass).length ||
                response.setup?.complete !== true || !Number.isInteger(response.setup.total) ||
                !Number.isInteger(response.setup.passed) || response.setup.passed < 0 ||
                response.setup.passed > response.setup.total || !Array.isArray(response.setup.failures) ||
                response.setup.failures.length !== response.setup.total - response.setup.passed ||
                typeof response.completed !== "boolean" ||
                response.ok !== (response.passed === response.total && response.completed &&
                                 response.setup.passed === response.setup.total)) {
              record.status = "FAIL";
              record.error = response.error || "Incomplete browser assertion report";
            } else {
              record.status = response.ok ? (test.expectedFailure ? "XPASS" : "PASS") :
                (test.expectedFailure ? "XFAIL" : "FAIL");
              record.passed = response.passed;
              record.total = response.total;
              record.failureCount = response.total - response.passed;
              record.browserReport = response;
            }
            if (response.outputPreview) record.outputPreview = response.outputPreview;
          }
          records[index] = record;
          onResult(record, records.filter(Boolean).length, selected.length);
        }
      };
      await Promise.all(Array.from({length: Math.min(jobs, selected.length)}, consume));
      const summary = {pass: 0, fail: 0, xfail: 0, xpass: 0, skip: 0, failures: [], unexpectedPasses: []};
      for (const record of records) {
        const key = record.status.toLowerCase();
        if (["pass", "xfail", "xpass", "skip"].includes(key)) summary[key]++;
        else { summary.fail++; summary.failures.push(record.identity); }
        if (key === "xpass") summary.unexpectedPasses.push(record.identity);
      }
      return {summary, tests: records, exitCode: state.cancelled ? 130 : summary.fail || summary.xpass ? 1 : 0};
    } finally {
      state.cancelled = true;
      for (const cancel of [...state.active]) cancel();
      this.running = null;
    }
  }
};

/* The offline shell page calls this controller directly. Tests use this same
 * API and production worker; there is no dashboard payload execution path. */
var browserTestSuite = null;
var browserTestResults = null;
var browserTestSuiteLoading = null;
async function installedBrowserSuite() {
  if (browserTestSuite) return browserTestSuite;
  if (!browserTestSuiteLoading) browserTestSuiteLoading = (async () => {
    const [wasmBytes, vfs, expectedFailures] = await Promise.all([
      loadBinary("waste-wast.wasm"), loadInstalledVfs(),
      loadText("browser-corpus-expected-failures.txt")]);
    browserTestSuite = new WasteTestSuite({wasmBytes, vfs, expectedFailures,
      createWorker: () => createWorker("worker.js")});
    return browserTestSuite;
  })();
  try { return await browserTestSuiteLoading; }
  catch (error) { browserTestSuiteLoading = null; throw error; }
}

function browserSuiteOptions() {
  const group = document.querySelector("#suite-group").value;
  return {groups: group ? [group] : [],
    files: document.querySelector("#suite-files").value.trim().split(/\s+/).filter(Boolean),
    jobs: Number(document.querySelector("#suite-jobs").value),
    timeoutMs: Number(document.querySelector("#suite-timeout").value)};
}

async function showInstalledTests(execute) {
  const status = document.querySelector("#suite-status");
  const output = document.querySelector("#suite-results");
  const buttons = [...document.querySelectorAll("#suite-list, #suite-run")];
  buttons.forEach(button => { button.disabled = true; });
  output.textContent = "";
  browserTestResults = null;
  document.querySelector("#suite-download").disabled = true;
  status.textContent = "Reading installed tests…";
  try {
    const suite = await installedBrowserSuite();
    const tests = await suite.list();
    const group = document.querySelector("#suite-group");
    if (group.options.length === 1) {
      for (const name of [...new Set(tests.map(test => test.group))])
        group.add(new Option(name, name));
    }
    const options = browserSuiteOptions();
    if (!execute) {
      const selected = suite.select(tests, options);
      output.textContent = selected.map(test => `${test.skipReason ? "SKIP" :
        test.expectedFailure ? "XFAIL?" : "PASS?"} ${test.identity}${test.skipReason ? ": " + test.skipReason : ""}`).join("\n");
      status.textContent = `${selected.length} installed tests`;
      return;
    }
    document.querySelector("#suite-cancel").disabled = false;
    browserTestResults = await suite.run(options, (record, done, count) => {
      output.textContent += `${record.status} ${record.identity}` +
        (record.total === undefined ? "" : ` (${record.passed}/${record.total})`) + "\n";
      status.textContent = `${done}/${count} completed`;
    });
    const s = browserTestResults.summary;
    status.textContent = `${s.pass} PASS, ${s.fail} FAIL, ${s.xfail} XFAIL, ${s.xpass} XPASS, ${s.skip} SKIP`;
    document.querySelector("#suite-download").disabled = false;
  } catch (error) { status.textContent = error.message || String(error); }
  finally {
    buttons.forEach(button => { button.disabled = false; });
    document.querySelector("#suite-cancel").disabled = true;
  }
}

if (typeof document !== "undefined") {
  document.querySelector("#suite-jobs").max = WASTE_CONFIG.SUITE_BROWSER_MAX_JOBS;
  document.querySelector("#suite-jobs").value = WASTE_CONFIG.SUITE_GUEST_DEFAULT_JOBS;
  document.querySelector("#suite-timeout").max = WASTE_CONFIG.EXECUTION_MAX_TIMEOUT_MS;
  document.querySelector("#suite-timeout").value = WASTE_CONFIG.SUITE_BROWSER_UI_TIMEOUT_MS;
  document.querySelector("#suite-list").onclick = () => showInstalledTests(false);
  document.querySelector("#suite-run").onclick = () => showInstalledTests(true);
  document.querySelector("#suite-cancel").onclick = () => browserTestSuite?.cancel();
  document.querySelector("#suite-download").onclick = () => {
    if (!browserTestResults) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(browserTestResults, null, 2)],
      {type: "application/json"}));
    const link = document.createElement("a");
    link.href = url;
    link.download = "waste-browser-test-results.json";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
}
