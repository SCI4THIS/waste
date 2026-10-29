import assert from "node:assert/strict";
import {mkdir, readFile, writeFile} from "node:fs/promises";
import {createWaste, createWasteWorker} from "../command/waste.mjs";
import {Worker} from "node:worker_threads";
import {gzipSync} from "node:zlib";

const engine = await readFile("build/html-rt/wasm.wasm");
await mkdir("build/engine/logs", {recursive: true});
const files = {
  "/lib/libc.so.wasm": await readFile("src/vfs/lib/libc.so.wasm"),
  "/usr/bin/awk": await readFile("src/vfs/usr/bin/awk"),
  "/usr/bin/m4": await readFile("src/vfs/usr/bin/m4"),
  "/usr/bin/dot": await readFile("src/vfs/usr/bin/dot")
};
const decode = value => new TextDecoder().decode(value);
// Exercise the wire boundary directly, independently of the friendly wrapper.
const {exports: abi} = await WebAssembly.instantiate(await WebAssembly.compile(engine), {
  waste_command_host: {monotonic_ns: () => BigInt(Math.floor(performance.now() * 1e6)),
    realtime_ns: () => 0n, random: () => -38}
});
const pointer = abi.waste_command_alloc(64);
const initialMemoryBytes = abi.memory.buffer.byteLength;
let options = new DataView(abi.memory.buffer, pointer, 64);
options.setUint32(0, 64, true); options.setUint32(4, 1024, true);
options.setUint32(8, 1024, true); options.setUint32(16, 1, true);
assert.equal(abi.waste_command_create(2, 0, 0, pointer, 64), 0);
assert.equal(abi.waste_command_create(1, 0, 16, pointer, 64), 0);
const first = abi.waste_command_create(1, 0, 7, pointer, 64);
assert(first > 0);
assert.equal(abi.waste_command_put_file(first, 0xfffffff0, 100, 0, 0, 0o644), -1);
assert.equal(abi.waste_command_inspect(first, pointer, 304), -1);
assert.equal(abi.waste_command_destroy(first), 0);
assert.equal(abi.waste_command_step(first), -5);
const second = abi.waste_command_create(1, 0, 7, pointer, 64);
assert(second > 0 && second !== first);
assert.equal(abi.waste_command_start(second, pointer, 64), -1);
assert.equal(abi.waste_command_destroy(second), 0);
assert.equal(abi.waste_command_free(pointer + 1), -5);
assert.equal(abi.waste_command_free(pointer), 0);
assert.equal(abi.waste_command_create(1, 0, 7, pointer, 64), 0);
assert(!WebAssembly.Module.exports(await WebAssembly.compile(engine)).some(e => e.name.startsWith("waste_wast_")));
const runtime = await createWaste({engine, files});
const result = await runtime.run({executable: "/usr/bin/awk", args: ["{print $1}"], stdin: "one two\nthree four\n"});
assert.equal(result.outcome, "exited");
assert.equal(result.exitCode, 0);
assert.equal(decode(result.stdout), "one\nthree\n");
assert.equal(result.stderr.length, 0);
const raw = await runtime.run({executable: "/usr/bin/awk",
  args: ['BEGIN { printf "a%cB", 0; print "error" > "/dev/stderr"; exit 7 }']});
assert.equal(raw.exitCode, 7);
assert.deepEqual([...raw.stdout], [97, 0, 66]);
assert.equal(decode(raw.stderr), "error\n");
const m4 = await runtime.run({executable: "/usr/bin/m4", stdin: "define(`name', `WASTE')name\n"});
assert.equal(m4.exitCode, 0);
assert.equal(decode(m4.stdout), "WASTE\n");
const dot = await runtime.run({executable: "/usr/bin/dot", args: ["-Tsvg", "-o", "/work/graph.svg"],
  stdin: "digraph { a -> b }", outputPaths: ["/work/graph.svg", "/work/missing"]});
assert.equal(dot.exitCode, 0, dot.message + decode(dot.stderr));
assert.match(decode(dot.outputFiles["/work/graph.svg"]), /<svg[\s>]/);
assert.match(decode(dot.outputFiles["/work/graph.svg"]), /a&#45;&gt;b|a-&gt;b/);
const nativeSVG = process.argv.find(arg => arg.startsWith("--native-svg="))?.slice("--native-svg=".length);
if (nativeSVG) assert.deepEqual(dot.outputFiles["/work/graph.svg"], new Uint8Array(await readFile(nativeSVG)));
assert.equal(dot.outputErrors["/work/missing"].code, 5);
const isolated = await runtime.run({executable: "/usr/bin/awk", args: ['BEGIN { print "fresh" }'], outputPaths: ["/work/graph.svg"]});
assert.equal(decode(isolated.stdout), "fresh\n");
assert(!isolated.outputFiles["/work/graph.svg"]);
const stream = await runtime.run({executable: "/usr/bin/awk", args: ["{print $1}"],
  stdin: (async function* () { yield "stream one\n"; await new Promise(r => setTimeout(r, 10)); yield "second two\n"; })()});
assert.equal(stream.exitCode, 0);
assert.equal(decode(stream.stdout), "stream\nsecond\n");
const throwingCleanup = await runtime.run({executable: "/usr/bin/awk", args: ['BEGIN {print "cleanup"}'], stdin: {
  [Symbol.asyncIterator]() { return this; }, async next() { return {done: true}; },
  return() { throw Error("cleanup callback failed"); }
}});
assert.equal(decode(throwingCleanup.stdout), "cleanup\n");
const inputStream = new ReadableStream({start(controller) { controller.enqueue(new TextEncoder().encode("readable one\n")); controller.close(); }});
const readable = await runtime.run({executable: "/usr/bin/awk", args: ["{print $1}"], stdin: inputStream});
assert.equal(decode(readable.stdout), "readable\n");
await new Promise(resolve => setTimeout(resolve, 0));
assert.equal(inputStream.locked, false);
const large = await runtime.run({executable: "/usr/bin/awk", args: ["{print $1}"], stdin: "x\n".repeat(5000)});
assert.equal(large.exitCode, 0);
assert.equal(decode(large.stdout), "x\n".repeat(5000));
const abort = new AbortController();
setTimeout(() => abort.abort(), 40);
const cancelled = await runtime.run({executable: "/usr/bin/awk", args: ["BEGIN { while (1) {} }"], signal: abort.signal, limits: {quantumMs: 1}});
assert.equal(cancelled.outcome, "cancelled");
assert.equal(cancelled.exitCode, null);
const timeout = await runtime.run({executable: "/usr/bin/awk", args: ["BEGIN { while (1) {} }"], limits: {timeoutMs: 10, quantumMs: 1}});
assert.equal(timeout.outcome, "timed_out");
const limited = await runtime.run({executable: "/usr/bin/awk", args: ['BEGIN { print "overflow" }'], limits: {maxOutputBytes: 2}});
assert.equal(limited.outcome, "limit");
const memoryLimited = await runtime.run({executable: "/usr/bin/awk", args: ['BEGIN {print "too much"}'], limits: {maxMemoryPages: 1}});
assert.equal(memoryLimited.outcome, "limit");
assert.match(memoryLimited.message, /memory/);
assert(memoryLimited.resources.memoryPagesPeak <= 1);
const instructionLimited = await runtime.run({executable: "/usr/bin/awk", args: ["BEGIN { while (1) {} }"], limits: {maxInstructions: 1000000n}});
assert.equal(instructionLimited.outcome, "limit");
assert.match(instructionLimited.message, /instruction/);
assert.equal(instructionLimited.resources.instructions, 1000000n);
const maxFilesystemBytes = Object.values(files).reduce((size, value) => size + value.length, 0) + 4096;
const fileLimited = await runtime.run({executable: "/usr/bin/awk",
  args: ['BEGIN { printf "%10000s", "x" > "/work/quota"; close("/work/quota") }'],
  limits: {maxFilesystemBytes}, outputPaths: ["/work/quota"]});
assert.equal(fileLimited.outcome, "limit");
assert.match(fileLimited.message, /filesystem/);
assert(fileLimited.resources.filesystemBytesPeak <= maxFilesystemBytes);
assert(fileLimited.outputFiles["/work/quota"].length <= 4096);
const bashBytes = await readFile("src/vfs/usr/bin/bash");
const signaled = await runtime.run({executable: "/usr/bin/bash", files: {"/usr/bin/bash": bashBytes},
  args: ["--norc", "-c", "kill -TERM $$"]});
assert.equal(signaled.outcome, "signaled");
assert.equal(signaled.signal, 15);
assert.equal(signaled.exitCode, null);
const processLimited = await runtime.run({executable: "/usr/bin/awk", args: ['BEGIN { system("printf child") }'],
  files: {"/usr/bin/bash": bashBytes}, limits: {maxProcesses: 1}});
assert.equal(processLimited.outcome, "limit");
assert.match(processLimited.message, /process/);
assert.equal(processLimited.resources.processesPeak, 1);
const parameters = await runtime.run({executable: "/usr/bin/awk", cwd: "/tmp",
  env: {VALUE: "with space"}, files: {"/tmp/input": "first second\n"},
  args: ['BEGIN {print ENVIRON["VALUE"]; print ENVIRON["HOME"]} {print $1}', "input"]});
assert.equal(parameters.exitCode, 0);
assert.equal(decode(parameters.stdout), "with space\n\nfirst\n");
for (const withShell of [false, true]) {
  const shellFiles = withShell ? {"/usr/bin/bash": bashBytes} : {};
  const system = await runtime.run({executable: "/usr/bin/awk", files: shellFiles,
    args: ['BEGIN { status=system("printf child"); print ":" status }']});
  assert.equal(system.exitCode, 0);
  assert.equal(decode(system.stdout), withShell ? "child:0\n" : ":127\n");
  const pipe = await runtime.run({executable: "/usr/bin/awk", files: shellFiles,
    args: ['BEGIN { "printf pipe" | getline value; close("printf pipe"); print value }']});
  assert.equal(pipe.exitCode, 0);
  assert.equal(decode(pipe.stdout), withShell ? "pipe\n" : "\n");
  const syscmd = await runtime.run({executable: "/usr/bin/m4", files: shellFiles,
    stdin: "syscmd(`printf child')sysval\n"});
  assert.equal(syscmd.exitCode, 0);
  assert.equal(decode(syscmd.stdout), withShell ? "child0\n" : "127\n");
  const esyscmd = await runtime.run({executable: "/usr/bin/m4", files: shellFiles,
    stdin: "esyscmd(`printf captured') sysval\n"});
  assert.equal(esyscmd.exitCode, 0);
  assert.equal(decode(esyscmd.stdout), withShell ? "captured 0\n" : " 127\n");
}
const withoutProvider = await createWaste({engine, files: {"/usr/bin/awk": files["/usr/bin/awk"]}});
const providerFailure = await withoutProvider.run({executable: "/usr/bin/awk", args: ['BEGIN {print "missing"}']});
assert.equal(providerFailure.outcome, "failed");
withoutProvider.dispose();
const missing = await runtime.run({executable: "/usr/bin/missing"});
assert.equal(missing.outcome, "failed");
const trappingModule = new Uint8Array([0,97,115,109,1,0,0,0, 1,4,1,96,0,0,
  3,2,1,0, 7,10,1,6,95,115,116,97,114,116,0,0, 10,5,1,3,0,0,11]);
const trapped = await runtime.run({executable: "/usr/bin/trap", files: {"/usr/bin/trap": trappingModule}});
assert.equal(trapped.outcome, "trapped");
assert.equal(trapped.exitCode, null);
const after = await runtime.run({executable: "/usr/bin/awk", args: ['BEGIN { print "after" }']});
assert.equal(decode(after.stdout), "after\n");
runtime.dispose();
await assert.rejects(runtime.run({executable: "/usr/bin/m4"}), /disposed/);
let nodeWorker;
const workerRuntime = await createWasteWorker({engine, files, workerFactory: () => {
  nodeWorker = new Worker(new URL("./embedding-worker-node.mjs", import.meta.url)); return nodeWorker;
}});
const workerAwk = await workerRuntime.run({executable: "/usr/bin/awk", args: ["{print $1}"], stdin: "one two\nthree four\n"});
assert.equal(workerAwk.exitCode, result.exitCode);
assert.deepEqual(workerAwk.stdout, result.stdout);
const workerDot = await workerRuntime.run({executable: "/usr/bin/dot", args: ["-Tsvg", "-o", "/work/graph.svg"],
  stdin: "digraph { a -> b }", outputPaths: ["/work/graph.svg"]});
assert.deepEqual(workerDot.outputFiles["/work/graph.svg"], dot.outputFiles["/work/graph.svg"]);
const workerStream = await workerRuntime.run({executable: "/usr/bin/awk", args: ["{print $1}"], stdin: (async function* () {
  yield "stream one\n"; await new Promise(r => setTimeout(r, 10)); yield "second two\n";
})()});
assert.deepEqual(workerStream.stdout, stream.stdout);
const workerAbort = new AbortController();
const working = workerRuntime.run({executable: "/usr/bin/awk", args: ["BEGIN { while (1) {} }"], signal: workerAbort.signal, limits: {quantumMs: 1}});
// Late input/cancellation for an old job must not affect the current session.
nodeWorker.postMessage({protocol: 1, type: "cancel", id: 1});
nodeWorker.postMessage({protocol: 1, type: "input", id: 1, sequence: 1, bytes: new Uint8Array([1])});
nodeWorker.emit("message", {protocol: 1, type: "result", id: 1, result: {outcome: "exited"}});
await assert.rejects(workerRuntime.run({executable: "/usr/bin/m4"}), /active run/);
setTimeout(() => workerAbort.abort(), 40);
assert.equal((await working).outcome, "cancelled");
await assert.rejects(workerRuntime.run({executable: "/usr/bin/awk", args: ["{print $1}"],
  limits: {maxInputBytes: 4}, stdin: (async function* () { yield "too long\n"; })()}), /input limit/);
await assert.rejects(workerRuntime.run({executable: "/usr/bin/awk", args: ["{print $1}"],
  stdin: (async function* () { throw Error("source failed"); })()}), /source failed/);
const workerAfter = await workerRuntime.run({executable: "/usr/bin/awk", args: ['BEGIN { print "after" }']});
assert.deepEqual(workerAfter.stdout, after.stdout);
const workerMemory = await workerRuntime.run({executable: "/usr/bin/awk", args: ['BEGIN {print "limit"}'], limits: {maxMemoryPages: 1}});
assert.equal(workerMemory.outcome, memoryLimited.outcome);
const workerInstructions = await workerRuntime.run({executable: "/usr/bin/awk", args: ["BEGIN {while (1) {}}"], limits: {maxInstructions: 1000000n}});
assert.equal(workerInstructions.resources.instructions, 1000000n);
const parallelRuntime = await createWasteWorker({engine, files, workerFactory: () =>
  new Worker(new URL("./embedding-worker-node.mjs", import.meta.url))});
const parallel = await Promise.all([
  workerRuntime.run({executable: "/usr/bin/awk", args: ['BEGIN { print "left" > "/work/shared" }'], outputPaths: ["/work/shared"]}),
  parallelRuntime.run({executable: "/usr/bin/awk", args: ['BEGIN { print "right" > "/work/shared" }'], outputPaths: ["/work/shared"]})
]);
assert.equal(decode(parallel[0].outputFiles["/work/shared"]), "left\n");
assert.equal(decode(parallel[1].outputFiles["/work/shared"]), "right\n");
await parallelRuntime.dispose();
await workerRuntime.dispose();
await workerRuntime.dispose();
const gzipBytes = gzipSync(engine).length;
assert(engine.length <= 1024 * 1024, "Command artifact exceeds initial measured 1 MiB budget");
assert(gzipBytes <= 256 * 1024, "Compressed command artifact exceeds initial measured 256 KiB budget");
const exampleMemoryBytes = {awk: result.engineMemoryBytes, m4: m4.engineMemoryBytes, dot: dot.engineMemoryBytes};
assert(Object.values(exampleMemoryBytes).every(size => size <= 128 * 1024 * 1024), "Examples exceed initial measured 128 MiB outer-memory budget");
const imports = WebAssembly.Module.imports(await WebAssembly.compile(engine));
assert.deepEqual(imports.map(value => `${value.module}.${value.name}`).sort(), [
  "waste_command_host.monotonic_ns", "waste_command_host.random", "waste_command_host.realtime_ns"]);
await writeFile("build/engine/logs/embedding-browser-results.json", JSON.stringify({
  passed: true, artifactBytes: engine.length, gzipBytes, initialMemoryBytes, exampleMemoryBytes,
  examples: ["awk", "m4", "dot"], exactNativeSVGParity: Boolean(nativeSVG),
  peakObservedEngineMemoryBytes: Math.max(result.engineMemoryBytes, m4.engineMemoryBytes, dot.engineMemoryBytes),
  imports
}, null, 2) + "\n");
console.log("PASS embedding Wasm and worker: AWK/M4/DOT, raw streams, files, isolation, streaming EOF, cancellation, timeout, limits and stale messages");
