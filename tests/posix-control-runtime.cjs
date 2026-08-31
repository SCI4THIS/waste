"use strict";

const fs = require("node:fs");
const path = require("node:path");
const {Worker} = require("node:worker_threads");

const root = path.resolve(__dirname, "..");
const loaderPath = path.join(
  root,
  process.argv[2] === "threaded" ? "build/ocaml-wasm/dist-threaded" : "build/ocaml-wasm/dist",
  "wasm_cli.bc.wasm.js"
);

const source = `(module
  (import "env" "sigaction"
    (func $sigaction (param i32 i32 i32) (result i32)))
  (memory 1)
  (table 4 funcref)
  (global $seen (mut i32) (i32.const 0))
  (func $handler (param $signal i32)
    local.get $signal
    global.set $seen)
  (elem (i32.const 2) $handler)
  (func $install
    i32.const 16
    i32.const 2
    i32.store
    i32.const 2
    i32.const 16
    i32.const 0
    call $sigaction
    drop)
  (start $install)
  (func (export "run") (result i32)
    loop $wait
      global.get $seen
      i32.eqz
      br_if $wait
    end
    global.get $seen))
(assert_return (invoke "run") (i32.const 2))`;

const page = new Int32Array(new SharedArrayBuffer((4 + 256) * 4));
page[3] = 1;
globalThis.waste_control_page = page;
globalThis.waste_exit_code = 0;

const controller = new Worker(`
  const {workerData} = require("node:worker_threads");
  const page = new Int32Array(workerData);
  const sleep = milliseconds => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, milliseconds);
  const send = (operation, argument = 0) => {
    const sequence = Atomics.load(page, 0) + 1;
    Atomics.store(page, 4 + (sequence & 255), (operation << 16) | argument);
    Atomics.store(page, 0, sequence);
    Atomics.notify(page, 0);
  };
  // Pause the first evaluator activity, then allow the module start function
  // to install SIGINT before the external signal is published.
  sleep(25);
  Atomics.store(page, 1, 1);
  send(1);
  sleep(100);
  Atomics.store(page, 1, 0);
  Atomics.notify(page, 1);
  sleep(50);
  send(2, 2);
`, {eval: true, workerData: page.buffer});

process.argv = [process.execPath, loaderPath, "--control-page", "-e", source];
require.main.filename = loaderPath;

(async () => {
  try {
    const completion = eval(fs.readFileSync(loaderPath, "utf8"));
    await completion;
    if (globalThis.waste_exit_code !== 0) {
      throw new Error(`interpreter exited with ${globalThis.waste_exit_code}`);
    }
    console.log("pause/resume and SIGINT delivery: pass");
  } finally {
    await controller.terminate();
  }
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
