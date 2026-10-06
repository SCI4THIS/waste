#!/usr/bin/env node
"use strict";

/* Real file:// acceptance gate for the single Bash/test page, using Chromium's
 * pipe protocol (no web server, external driver, or exposed debugging socket). */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {pathToFileURL} = require("node:url");
const {spawn} = require("node:child_process");

// Select one real page for a focused DOM rerun; default checks both.
const pageFlag = process.argv.find(arg => arg.startsWith("--page="));
const selectedPage = pageFlag ? pageFlag.slice("--page=".length) : null;
assert(selectedPage === null || selectedPage === "bash", "only the bash.html page remains");
const root = path.resolve(__dirname, "..");
const artifacts = path.join(root, "build/html-rt/offline-browser-check");
fs.mkdirSync(artifacts, {recursive: true});
const profile = fs.mkdtempSync(path.join(artifacts, "chromium-"));
const browser = spawn(process.env.WASTE_CHROMIUM || "chromium", [
  "--headless=new", "--remote-debugging-pipe", "--no-first-run",
  "--no-default-browser-check", "--disable-background-networking",
  "--disable-component-update", "--disable-sync", "--window-size=1280,800",
  "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader",
  "--user-data-dir=" + profile,
], {stdio: ["ignore", "ignore", "pipe", "pipe", "pipe"]});
let sequence = 0;
let input = Buffer.alloc(0);
let diagnostics = "";
const pending = new Map();
const events = [];
browser.stderr.on("data", chunk => { diagnostics = (diagnostics + chunk).slice(-8000); });
function rejectPending(error) {
  for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
  pending.clear();
}
browser.on("error", rejectPending);
browser.on("exit", code => rejectPending(new Error(`Chromium exited (${code}): ${diagnostics}`)));
browser.stdio[3].on("error", rejectPending);
browser.stdio[4].on("data", chunk => {
  input = Buffer.concat([input, chunk]);
  for (let boundary; (boundary = input.indexOf(0)) >= 0;) {
    const message = JSON.parse(input.subarray(0, boundary).toString("utf8"));
    input = input.subarray(boundary + 1);
    if (!message.id) { events.push(message); continue; }
    const request = pending.get(message.id);
    if (!request) continue;
    pending.delete(message.id);
    clearTimeout(request.timer);
    if (message.error) request.reject(new Error(JSON.stringify(message.error)));
    else request.resolve(message.result);
  }
});
function call(method, params = {}, sessionId) {
  return new Promise((resolve, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`Chromium protocol timed out: ${method}; ${diagnostics}`));
    }, 20000);
    pending.set(id, {resolve, reject, timer});
    browser.stdio[3].write(JSON.stringify({id, method, params, sessionId}) + "\0");
  });
}
async function evaluate(session, expression) {
  const response = await call("Runtime.evaluate", {
    expression, returnByValue: true, awaitPromise: true,
  }, session);
  if (response.exceptionDetails) throw new Error(JSON.stringify(response.exceptionDetails));
  return response.result.value;
}
async function until(session, expression, timeoutMs = 25000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await evaluate(session, expression)) return;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  const status = await evaluate(session, `JSON.stringify({
    body:document.body.innerText,
    status:document.querySelector('#status')?.textContent,
    transcript:document.querySelector('#terminal-transcript')?.textContent
  })`);
  throw new Error(`offline page timed out: ${expression}\n${status}`);
}

async function waitSession(session, name = "waits") {
  const contract = JSON.parse(fs.readFileSync(path.join(root, `tests/guest-session-${name}.json`)));
  const source = fs.readFileSync(path.join(root, contract.fixture), "utf8");
  await evaluate(session, `(async () => {
    window.waitProbe = {ready:0, output:'', errors:[], done:null};
    window.waitProbeWorker = await createWorker('worker.js');
    waitProbeWorker.onmessage = ({data}) => {
      if (data.type === 'output' && !data.text.startsWith('WASTE_')) waitProbe.output += data.text;
      if (data.type === 'control-error') waitProbe.errors.push(data);
      if (data.type === 'started' || data.type === 'io-ready') {
        waitProbe.ready++; waitProbe.pid = data.pid; waitProbe.kind = data.waitKind;
      }
      if (data.type === 'done') waitProbe.done = data;
    };
    waitProbeWorker.onerror = event => { waitProbe.error = event.message; };
    waitProbeWorker.postMessage({type:'set-clock', realtimeLow:0, realtimeHigh:0,
      monotonicLow:${contract.clockMonotonicNs ?? 0}, monotonicHigh:0});
    waitProbeWorker.postMessage({type:'start', wasmBytes:await loadBinary('waste-wast.wasm'),
      source:${JSON.stringify(source)}, vfs:await loadInstalledVfs()});
  })()`);
  try {
    let previous = 0, consumed = 0, rejected = 0;
    for (const event of contract.events) {
      await until(session, `waitProbe.ready > ${previous} &&
        waitProbe.output.indexOf(${JSON.stringify(event.after || "")}, ${consumed}) >= 0`);
      const before = await evaluate(session, "waitProbe");
      assert.equal(before.pid, event.pid);
      assert.equal(before.kind, event.waitKind);
      assert.equal(before.error, undefined);
      previous = before.ready; consumed = before.output.length;
      for (const value of event.rejectMonotonicNs || []) {
        const n = BigInt(value);
        await evaluate(session, `waitProbeWorker.postMessage({type:'advance-clock',
          low:${Number(n & 0xffffffffn)}, high:${Number(n >> 32n)}})`);
        await until(session, `waitProbe.errors.length === ${++rejected}`);
      }
      let message;
      if (event.monotonicNs !== undefined) {
        const n = BigInt(event.monotonicNs);
        message = {type:"advance-clock", low:Number(n & 0xffffffffn), high:Number(n >> 32n)};
      } else if (event.signal !== undefined) message = {type:"signal", signal:event.signal};
      else message = {type:"input", bytes:event.text === undefined ? event.bytes :
        Array.from(Buffer.from(event.text))};
      await evaluate(session, `waitProbeWorker.postMessage(${JSON.stringify(message)})`);
      if (event.stillWaiting) {
        await until(session, `waitProbe.ready > ${previous}`);
        assert.equal(await evaluate(session, "waitProbe.output"), before.output,
          "guest advanced before its wait was satisfied");
      }
    }
    await until(session, "waitProbe.done !== null");
    const result = await evaluate(session, "waitProbe");
    assert.equal(result.output, contract.output);
    assert.equal(result.errors.length, rejected);
    assert(result.errors.every(error => error.operation === "advance-clock"));
    assert(result.done.ok && result.done.exited);
    assert.equal(result.done.passed, contract.assertions);
    assert.equal(result.done.total, contract.assertions);
    assert.equal(result.done.exitStatus, contract.exitStatus);
    const filename = name === "waits" ? "wait-session" : `${name}-session`;
    fs.writeFileSync(path.join(artifacts, `${filename}-results.json`), JSON.stringify(result, null, 2) + "\n");
    console.log(name === "waits" ?
      "PASS offline wait session: 24 checks, input/signal wakeups, exact frozen deadline and clock rejection" :
      `PASS offline ${name} session: ${contract.assertions} checks, shared transcript and post-yield events`);
  } finally {
    await evaluate(session, "waitProbeWorker.terminate()");
  }
}

(async () => {
  await call("Browser.getVersion");
  for (const [target, filename] of [["bash", "bash.html"]]) {
    if (selectedPage && selectedPage !== target) continue;
    const pagePath = path.resolve(process.argv.slice(2).find(arg => arg.endsWith(".html")) ||
      path.join(root, "build/html-rt", filename));
    const url = pathToFileURL(pagePath).href;
    const {targetId} = await call("Target.createTarget", {url: "about:blank"});
    const {sessionId} = await call("Target.attachToTarget", {targetId, flatten: true});
    await call("Page.enable", {}, sessionId);
    await call("Runtime.enable", {}, sessionId);
    await call("Network.enable", {}, sessionId);
    await call("Page.navigate", {url}, sessionId);
    await until(sessionId, `location.href === ${JSON.stringify(url)} &&
      document.readyState === 'complete' && typeof g !== 'undefined' &&
      !document.querySelector('#loading-overlay')`);
    assert.equal(await evaluate(sessionId, "g.is_staging"), false);
    if (target === "bash") {
      await until(sessionId, "!document.querySelector('#terminal-input').disabled");
      assert.equal(await evaluate(sessionId, "renderer.useWebGL && !renderer.gl.isContextLost()"), true);
      await evaluate(sessionId, `document.querySelector('#terminal-input').value =
        "/bin/echo __OFFLINE_LAYOUT_OK__";
        document.querySelector('#terminal-form').dispatchEvent(new Event('submit', {cancelable: true}));`);
      await until(sessionId, `document.querySelector('#terminal-transcript').textContent
        .includes(String.fromCharCode(13) + '__OFFLINE_LAYOUT_OK__')`);
      await evaluate(sessionId, `document.querySelector('#terminal-input').value =
        '/bin/waste-test --group=core --results=/tmp/early-cancel.json >/tmp/early-cancel.txt; printf "__EARLY_CANCEL__%s\\n" "$?"';
        document.querySelector('#terminal-form').dispatchEvent(new Event('submit', {cancelable: true}));`);
      await until(sessionId, `browserTestSuite !== null && browserTestSuite.guestPending !== null`);
      await evaluate(sessionId, `worker.postMessage({type:'input', bytes:[3]})`);
      await until(sessionId, `document.querySelector('#terminal-transcript').textContent.includes('__EARLY_CANCEL__130')`);
      await evaluate(sessionId, `document.querySelector('.diagnostics').open = true;
        document.querySelector('#installed-tests').open = true;
        document.querySelector('#suite-list').click();`);
      await until(sessionId, `document.querySelector('#suite-status').textContent === '296 installed tests'`);
      assert.equal(await evaluate(sessionId, `browserTestSuite.catalogue.filter(test => test.skipReason).length`), 4);
      await evaluate(sessionId, `document.querySelector('#suite-files').value = 'address.wast path-runtime.wast';
        document.querySelector('#suite-run').click();`);
      await until(sessionId, `browserTestResults !== null && !document.querySelector('#suite-run').disabled`);
      assert.equal(await evaluate(sessionId, "browserTestResults.summary.pass"), 2);
      assert.equal(await evaluate(sessionId, "browserTestResults.exitCode"), 0);
      assert(await evaluate(sessionId, "browserTestResults.tests.every(record => record.browserReport.results.length > 0)"));
      // Migrated executor checks run through the installed production workers.
      await evaluate(sessionId, `document.querySelector('#suite-files').value = '';
        document.querySelector('#suite-group').value = 'engine-regressions';
        document.querySelector('#suite-run').click();`);
      await until(sessionId, `browserTestResults !== null && !document.querySelector('#suite-run').disabled`);
      const regressions = await evaluate(sessionId, "browserTestResults");
      assert.equal(regressions.exitCode, 0);
      assert.equal(regressions.summary.pass, 12);
      assert.deepEqual(regressions.tests.map(test => [test.identity, test.passed, test.total]), [
        ['engine-regressions/caller-memory.wast', 12, 12],
        ['engine-regressions/continuation-waits.wast', 23, 23],
        ['engine-regressions/descriptor-flags.wast', 93, 93],
        ['engine-regressions/directory-umask.wast', 219, 219],
        ['engine-regressions/extern-aliases.wast', 16, 16],
        ['engine-regressions/i32-smoke.wast', 32, 32],
        ['engine-regressions/instance-isolation.wast', 16, 16],
        ['engine-regressions/path-vfs.wast', 71, 71],
        ['engine-regressions/pipe-descriptors.wast', 136, 136],
        ['engine-regressions/select-polling.wast', 247, 247],
        ['engine-regressions/shared-memory.wast', 231, 231],
        ['engine-regressions/signal-masks.wast', 134, 134],
      ]);
      fs.writeFileSync(path.join(artifacts, "engine-regressions-results.json"), JSON.stringify(regressions, null, 2) + "\n");
      console.log("PASS production browser executor regressions: 12 installed WAST files, 1230 assertions");
      await waitSession(sessionId);
      await waitSession(sessionId, "terminal-readiness");
      await waitSession(sessionId, "terminal-timing");
      await waitSession(sessionId, "process-groups");
      await waitSession(sessionId, "terminal-descriptors");
      await waitSession(sessionId, "diy-control");
      await evaluate(sessionId, `document.querySelector('#suite-group').value = '';`);
      // Cancel an in-flight batch, then run again without restarting Bash.
      await evaluate(sessionId, `document.querySelector('#suite-files').value = '';
        document.querySelector('#suite-run').click();`);
      await until(sessionId, `!document.querySelector('#suite-cancel').disabled`);
      await evaluate(sessionId, `document.querySelector('#suite-cancel').click()`);
      await until(sessionId, `browserTestResults !== null && !document.querySelector('#suite-run').disabled`);
      assert.equal(await evaluate(sessionId, "browserTestResults.exitCode"), 130);
      await evaluate(sessionId, `document.querySelector('#terminal-input').value =
        '/bin/waste-test --json --results=/tmp/guest-suite.json path-runtime.wast >/tmp/guest-stdout.json; printf "__GUEST_BATCH__%s\\n" "$?"; /bin/cat /tmp/guest-suite.json';
        document.querySelector('#terminal-form').dispatchEvent(new Event('submit', {cancelable: true}));`);
      await until(sessionId, `document.querySelector('#terminal-transcript').textContent.includes('__GUEST_BATCH__0') &&
        document.querySelector('#terminal-transcript').textContent.includes('"identity":"libc-test/path-runtime.wast"')`);
      await evaluate(sessionId, `document.querySelector('#terminal-input').value =
        '/bin/waste-test --group=core --results=/tmp/guest-cancel.json >/tmp/guest-cancel.txt; printf "__GUEST_CANCEL__%s\\n" "$?"';
        document.querySelector('#terminal-form').dispatchEvent(new Event('submit', {cancelable: true}));`);
      await until(sessionId, `browserTestSuite.running !== null`);
      await evaluate(sessionId, `worker.postMessage({type:'input', bytes:[3]})`);
      await until(sessionId, `document.querySelector('#terminal-transcript').textContent.includes('__GUEST_CANCEL__130')`);
      console.log("PASS offline guest launcher: JSON/report redirection and Ctrl-C return to shell");
      if (process.argv.includes("--suite-full")) {
        await evaluate(sessionId, `document.querySelector('#suite-jobs').value = 4;
          document.querySelector('#suite-run').click();`);
        await until(sessionId, `browserTestResults !== null && !document.querySelector('#suite-run').disabled`, 300000);
        const report = await evaluate(sessionId, "browserTestResults");
        fs.writeFileSync(path.join(artifacts, "mounted-suite-results.json"), JSON.stringify(report, null, 2) + "\n");
        const identities = await evaluate(sessionId,
          "browserTestSuite.list().then(tests => tests.map(test => test.identity))");
        assert.deepEqual(report.tests.map(test => test.identity), identities);
        assert.equal(report.exitCode, 0);
        console.log("PASS production browser batch: full mounted corpus", report.summary);
      }
      await evaluate(sessionId, `document.querySelector('#terminal-input').value =
        '/bin/echo __BASH_AFTER_SUITE__';
        document.querySelector('#terminal-form').dispatchEvent(new Event('submit', {cancelable:true}));`);
      await until(sessionId, `document.querySelector('#terminal-transcript').textContent
        .includes(String.fromCharCode(13) + '__BASH_AFTER_SUITE__')`);
    }
    const pageEvents = events.filter(event => event.sessionId === sessionId);
    assert.deepEqual(pageEvents.filter(event => event.method === "Runtime.exceptionThrown"), []);
    const requests = pageEvents.filter(event => event.method === "Network.requestWillBeSent")
      .map(event => event.params.request.url);
    assert(requests.includes(url), "must navigate the actual file:// page");
    assert(requests.every(request => request === url || /^(data:|blob:)/.test(request)),
      "offline page requested an external runtime resource");
    const screenshot = await call("Page.captureScreenshot", {format: "png"}, sessionId);
    fs.writeFileSync(path.join(artifacts, target + ".png"), Buffer.from(screenshot.data, "base64"));
    console.log(`PASS ${target}: actual file:// archive boot, DOM, worker, and no external requests`);
    await call("Target.closeTarget", {targetId});
  }
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
}).finally(async () => {
  const exited = new Promise(resolve => browser.once("exit", resolve));
  if (browser.exitCode === null && browser.signalCode === null) {
    browser.kill();
    await exited;
  }
  fs.rmSync(profile, {recursive: true, force: true});
});
