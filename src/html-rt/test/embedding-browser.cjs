#!/usr/bin/env node
// Real file:// gate for the standalone embedding consumer and Blob worker.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {spawn} = require("node:child_process");
const {pathToFileURL} = require("node:url");
const root = path.resolve(__dirname, "../../..");
const page = path.resolve(process.argv[2] || path.join(root, "build/html-rt/embedding/embedding.html"));
const logs = path.join(root, "build/engine/logs");
fs.mkdirSync(logs, {recursive: true});
const profile = fs.mkdtempSync(path.join(logs, "embedding-chromium-"));
const browser = spawn(process.env.WASTE_CHROMIUM || "chromium", ["--headless=new", "--remote-debugging-pipe",
  "--no-first-run", "--no-default-browser-check", "--disable-background-networking", "--disable-component-update",
  "--disable-sync", `--user-data-dir=${profile}`], {stdio: ["ignore", "ignore", "pipe", "pipe", "pipe"]});
let sequence = 0, input = Buffer.alloc(0), diagnostics = "";
const pending = new Map(), events = [], workerErrors = [];
function rejectPending(error) { for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); } pending.clear(); }
browser.on("error", rejectPending);
browser.on("exit", code => rejectPending(Error(`Chromium exited ${code}: ${diagnostics}`)));
browser.stderr.on("data", bytes => { diagnostics = (diagnostics + bytes).slice(-8000); });
browser.stdio[3].on("error", rejectPending);
browser.stdio[4].on("data", bytes => {
  input = Buffer.concat([input, bytes]);
  for (let at; (at = input.indexOf(0)) >= 0;) {
    const message = JSON.parse(input.subarray(0, at)); input = input.subarray(at + 1);
    if (!message.id) {
      events.push(message);
      if (message.method === "Target.attachedToTarget") {
        const child = message.params.sessionId;
        (async () => {
          await call("Network.enable", {}, child);
          await call("Runtime.enable", {}, child);
          await call("Runtime.runIfWaitingForDebugger", {}, child);
        })().catch(error => { workerErrors.push(error.message); rejectPending(error); });
      }
      continue;
    }
    const request = pending.get(message.id);
    if (!request) continue;
    pending.delete(message.id); clearTimeout(request.timer);
    if (message.error) request.reject(Error(JSON.stringify(message.error))); else request.resolve(message.result);
  }
});
function call(method, params = {}, sessionId) {
  return new Promise((resolve, reject) => {
    const id = ++sequence, timer = setTimeout(() => { pending.delete(id); reject(Error(`Chromium timeout: ${method}`)); }, 60000);
    pending.set(id, {resolve, reject, timer});
    browser.stdio[3].write(JSON.stringify({id, method, params, sessionId}) + "\0");
  });
}
async function evaluate(session, expression) {
  const response = await call("Runtime.evaluate", {expression, awaitPromise: true, returnByValue: true}, session);
  if (response.exceptionDetails) throw Error(JSON.stringify(response.exceptionDetails));
  return response.result.value;
}
(async () => {
  const {targetId} = await call("Target.createTarget", {url: "about:blank"});
  const {sessionId} = await call("Target.attachToTarget", {targetId, flatten: true});
  await call("Network.enable", {}, sessionId); await call("Runtime.enable", {}, sessionId);
  await call("Target.setAutoAttach", {autoAttach: true, waitForDebuggerOnStart: true, flatten: true}, sessionId);
  await call("Page.enable", {}, sessionId);
  await call("Page.navigate", {url: pathToFileURL(page).href}, sessionId);
  const deadline = Date.now() + 30000;
  while (!(await evaluate(sessionId, "Boolean(window.embedding?.ready)"))) {
    const error = await evaluate(sessionId, "window.embedding?.error");
    if (error) throw Error(error);
    if (Date.now() > deadline) throw Error("Embedding page failed to become ready");
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  const result = await evaluate(sessionId, `(async () => {
    const text = bytes => new TextDecoder().decode(bytes);
    const awk = await embedding.run(embedding.examples.awk);
    const streamed = await embedding.run({...embedding.examples.awk, stdin:(async function* () {
      yield 'stream one\\n'; await new Promise(resolve => setTimeout(resolve, 10)); yield 'second two\\n';
    })()});
    const raw = await embedding.run({executable:'/usr/bin/awk',
      args:['BEGIN { printf "a%cB", 0; print "error" > "/dev/stderr"; exit 7 }']});
    const m4 = await embedding.run({...embedding.examples.m4, stdin:"include(\u0060/work/input')", files:{'/work/input':'included\\n'}});
    const dot = await embedding.run(embedding.examples.dot);
    const svg = text(dot.outputFiles['/work/graph.svg']);
    const downloadVisible = !document.querySelector('#download').hidden;
    const looping = embedding.run({executable:'/usr/bin/awk', args:['BEGIN { while (1) {} }'], limits:{quantumMs:1}});
    setTimeout(() => embedding.cancel(), 50);
    const cancelled = await looping;
    const after = await embedding.run({...embedding.examples.awk, stdin:'fresh run\\n'});
    return {awk:{status:awk.exitCode,stdout:text(awk.stdout)}, streamed:text(streamed.stdout), raw:{status:raw.exitCode,stdout:Array.from(raw.stdout),stderr:text(raw.stderr)},
      m4:{status:m4.exitCode,stdout:text(m4.stdout)}, dot:{status:dot.exitCode,svg},
      cancelled:cancelled.outcome, after:text(after.stdout), downloadVisible,
      isolated:crossOriginIsolated, status:document.querySelector('#status').textContent};
  })()`);
  assert.deepEqual(result.awk, {status: 0, stdout: "one\nthree\n"});
  assert.equal(result.streamed, "stream\nsecond\n");
  assert.deepEqual(result.raw, {status: 7, stdout: [97, 0, 66], stderr: "error\n"});
  assert.deepEqual(result.m4, {status: 0, stdout: "included\n"});
  assert.equal(result.dot.status, 0); assert.match(result.dot.svg, /<svg[\s>]/);
  assert.match(result.dot.svg, /<g id="edge1"/);
  const nativeSVG = path.join(logs, "embedding-native.svg");
  assert.equal(result.dot.svg, fs.readFileSync(nativeSVG, "utf8"), "native/browser file bytes");
  assert.equal(result.downloadVisible, true); assert.equal(result.cancelled, "cancelled");
  assert.equal(result.after, "fresh\n"); assert.equal(result.isolated, false);
  assert.equal(result.status, "Exited with status 0");
  const requests = events.filter(event => event.method === "Network.requestWillBeSent").map(event => event.params.request.url);
  assert(requests.includes(pathToFileURL(page).href));
  assert(requests.every(url => url === pathToFileURL(page).href || url.startsWith("blob:")), `External requests: ${requests}`);
  const exceptions = events.filter(event => event.method === "Runtime.exceptionThrown");
  assert.equal(exceptions.length, 0, JSON.stringify(exceptions));
  assert.deepEqual(workerErrors, []);
  assert(events.some(event => event.method === "Target.attachedToTarget" && event.params.targetInfo.type === "worker"));
  fs.writeFileSync(path.join(logs, "embedding-offline-results.json"), JSON.stringify({page, requests, ...result}, null, 2) + "\n");
  console.log("embedding offline browser: AWK/M4/DOT, exact SVG parity, Blob worker cancellation/reuse and no external requests passed");
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
  try { await call("Browser.close"); } catch {}
  browser.kill();
});
