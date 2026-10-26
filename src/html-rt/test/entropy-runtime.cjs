#!/usr/bin/env node
"use strict";
// Private adapter boundary: callback admission, bounded buffers and failure.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const {webcrypto} = require("node:crypto");
const wasm = fs.readFileSync(process.argv[2] || "build/html-rt/waste-wast.wasm");
(async () => {
  let exp, calls = 0, unavailable = false;
  const {instance} = await WebAssembly.instantiate(wasm, {waste_host: {
    strtod: () => 0, strtof: () => 0, posix_open: () => -1, posix_close: () => 0,
    posix_read: () => -1, posix_write: (_fd, _ptr, n) => n, wall_clock_ms: () => Date.now(),
    random_bytes: (ptr, count) => {
      calls++;
      assert(count > 0 && count <= 256);
      if (unavailable) return -38;
      webcrypto.getRandomValues(new Uint8Array(exp.memory.buffer, ptr, count));
      return count;
    }
  }});
  exp = instance.exports;
  const source = expected => `(module
    (import "waste_kernel" "getrandom_v1" (func $random (param i32 i32 i32) (result i32)))
    (memory 1)
    (data (i32.const 65532) "keep")
    (func (export "random") (param i32 i32 i32) (result i32)
      local.get 0 local.get 1 local.get 2 call $random)
    (func (export "sentinel") (result i32) (i32.load (i32.const 65532))))
    (assert_return (invoke "random" (i32.const 0) (i32.const 0) (i32.const 0)) (i32.const 0))
    (assert_return (invoke "random" (i32.const 65532) (i32.const 8) (i32.const 0)) (i32.const -14))
    (assert_return (invoke "random" (i32.const 32) (i32.const 8) (i32.const 8)) (i32.const -22))
    (assert_return (invoke "sentinel") (i32.const 0x7065656b))
    (assert_return (invoke "random" (i32.const 32) (i32.const 512) (i32.const 0)) (i32.const ${expected}))`;
  for (const failure of [false, true]) {
    unavailable = failure; calls = 0;
    const bytes = Buffer.from(source(failure ? -38 : 512));
    const ptr = exp.waste_wast_alloc(bytes.length);
    assert(ptr);
    new Uint8Array(exp.memory.buffer, ptr, bytes.length).set(bytes);
    try {
      assert.equal(exp.waste_wast_run_script(ptr, bytes.length), 0);
      assert.equal(exp.waste_wast_results_total(), 5);
      assert.equal(exp.waste_wast_results_passed(), 5);
      assert.equal(calls, failure ? 1 : 2,
        "invalid/empty requests must not invoke entropy; failed entropy must not retry/fallback");
    } finally { exp.waste_wast_free(ptr); }
  }
  console.log("PASS entropy adapter: bounds, 256-byte transfers, secure source, fail-closed capability");
})().catch(error => { console.error(error); process.exitCode = 1; });
