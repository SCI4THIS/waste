#!/usr/bin/env node
"use strict";

/* Verify first paint while the generated HTML response is still incomplete. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {pathToFileURL} = require("node:url");
const {spawn} = require("node:child_process");

const root = path.resolve(__dirname, "../../..");
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


const http = require("node:http");
const html = fs.readFileSync(path.join(root, "build/html-rt/bash.html"));
const firstScript = html.indexOf("<script>");
assert(firstScript > 0 && firstScript < 14000);
let response;
const server = http.createServer((req, res) => {
  if (req.url !== "/") { res.writeHead(404).end(); return; }
  response = res;
  res.writeHead(200, {"Content-Type": "text/html; charset=utf-8",
    "Content-Length": html.length});
  res.write(html.subarray(0, firstScript));
});
(async () => {
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  const {targetId} = await call("Target.createTarget", {url:"about:blank"});
  const {sessionId} = await call("Target.attachToTarget", {targetId, flatten:true});
  await call("Page.enable", {}, sessionId);
  await call("Page.navigate", {url:`http://127.0.0.1:${server.address().port}/`}, sessionId);
  await until(sessionId, `document.querySelector('#loading-status')?.textContent === 'Loading…' &&
    performance.getEntriesByName('first-contentful-paint').length > 0`);
  assert.equal(await evaluate(sessionId, "document.readyState"), "loading");
  console.log("PASS: first contentful paint before payload");
  const shot = await call("Page.captureScreenshot", {}, sessionId);
  fs.writeFileSync(path.join(artifacts, "streaming-loading.png"), Buffer.from(shot.data, "base64"));
  const chunkEnd = html.indexOf("</script>", html.indexOf("Loading application (1 of")) + 9;
  assert(chunkEnd > firstScript);
  response.write(html.subarray(firstScript, chunkEnd));
  await until(sessionId, `document.querySelector('#loading-status').textContent.startsWith('Loading application (1 of')`);
  assert.equal(await evaluate(sessionId, "document.readyState"), "loading");
  console.log("PASS: progress before EOF");
  response.end(html.subarray(chunkEnd));
  await until(sessionId, `!document.querySelector('#loading-overlay') && document.querySelector('#terminal-input')?.disabled === false`, 60000);
  console.log("PASS: loading painted before payload, chunk progress before EOF, Bash boot after EOF");
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
  response?.destroy();
  server.closeAllConnections();
  server.close();
  await call("Browser.close").catch(() => {});
  browser.kill();
});
