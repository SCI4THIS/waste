#!/usr/bin/env node
"use strict";
const {config, withConfig} = require("./runtime-config.cjs");
const {packageVfs, stageVfs, treeVfs} = require("./vfs-package.cjs");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {execFileSync} = require("node:child_process");
const {readOfflinePackage} = require("./offline-html-package.cjs");
(async () => {
  // Prepare the installed-only C probe before starting the engine checks.
  const scratch = "build/engine/guest-sdk";
  fs.mkdirSync(scratch, {recursive:true});
  execFileSync("make", ["-C", "src/system-tests", "sysroot",
    "SYSROOT_DIR=" + path.resolve(scratch, "sysroot")]);
  execFileSync(scratch + "/sysroot/bin/waste-wasm-clang", ["-O2", "src/system-tests/guest-sdk/abi.c",
    "src/system-tests/guest-sdk/stat.c", "-Wl,--allow-undefined", "-Wl,--export=sdk_check",
    "-Wl,--export=sdk_stat_check", "-Wl,--export=sdk_signal_check",
    "-Wl,--export=sdk_assert_fail", "-o", scratch + "/abi.wasm"]);
  const text = execFileSync("wasm-dis", [scratch + "/abi.wasm"], {encoding:"utf8"});
  const probe = Buffer.from(text + '\n(assert_return (invoke "sdk_check") (i32.const 1))' +
    '\n(assert_return (invoke "sdk_stat_check") (i32.const 1))' +
    '\n(assert_return (invoke "sdk_signal_check") (i32.const 1))' +
    '\n(assert_trap (invoke "sdk_assert_fail") "unreachable")\n');
  fs.writeFileSync(scratch + "/abi.wast", probe);
  const page = readOfflinePackage(process.argv[2] || "build/html-rt/bash.html");
  let exp;
  function floating(ptr, length) {
    return Number(Buffer.from(new Uint8Array(exp.memory.buffer, ptr, length)).toString());
  }
  const {instance} = await WebAssembly.instantiate(page.read("waste-wast.wasm"), {waste_host:{
    strtod:floating, strtof:floating, posix_open:()=>-1, posix_close:()=>0,
    posix_read:()=>-1, posix_write:(_fd, _ptr, length)=>length, wall_clock_ms:()=>Date.now(),
  }});
  exp = instance.exports;
  function stage(bytes, invoke) {
    const p = exp.waste_wast_alloc(bytes.length);
    assert(p);
    new Uint8Array(exp.memory.buffer, p, bytes.length).set(bytes);
    const result = invoke(p, bytes.length);
    exp.waste_wast_free(p);
    return result;
  }
  // Current-tree discovery must reach the real browser engine, including edits
  // and deletions with no inventory or SDK refresh.
  const currentRoot = fs.mkdtempSync(path.join(scratch, "current-vfs-"));
  try {
    fs.mkdirSync(path.join(currentRoot, "data"));
    const sample = path.join(currentRoot, "data/sample");
    const currentProbe = `(module
      (import "waste_kernel" "path_access_v1" (func $access (param i32 i32 i32 i32) (result i32)))
      (import "waste_kernel" "path_stat_v1" (func $stat (param i32 i32 i32 i32) (result i32)))
      (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
      (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
      (import "env" "close" (func $close (param i32) (result i32)))
      (memory (export "__waste_memory") 1)
      (func (export "__errno_location") (result i32) (i32.const 0))
      (data (i32.const 32) "/data/sample\\00")
      (func (export "access") (result i32)
        (call $access (i32.const 32) (i32.const 12) (i32.const 0) (i32.const 0)))
      (func (export "size") (result i32)
        (drop (call $stat (i32.const 32) (i32.const 12) (i32.const 1) (i32.const 256)))
        (i32.wrap_i64 (i64.load (i32.const 272))))
      (func (export "first") (result i32) (local $fd i32)
        (local.set $fd (call $open (i32.const 32) (i32.const 0) (i32.const 0)))
        (if (i32.ne (call $read (local.get $fd) (i32.const 128) (i32.const 1)) (i32.const 1))
          (then (drop (call $close (local.get $fd))) (return (i32.const -1))))
        (drop (call $close (local.get $fd)))
        (i32.load8_u (i32.const 128))))`;
    for (const content of ["AB", "XYZ", null]) {
      if (content === null) fs.unlinkSync(sample);
      else fs.writeFileSync(sample, content);
      stageVfs(exp, treeVfs(currentRoot));
      const assertions = content === null
        ? '(assert_return (invoke "access") (i32.const -2))'
        : `(assert_return (invoke "access") (i32.const 0))
           (assert_return (invoke "size") (i32.const ${content.length}))
           (assert_return (invoke "first") (i32.const ${content.charCodeAt(0)}))`;
      assert.equal(stage(Buffer.from(currentProbe + assertions), exp.waste_wast_run_script), 0);
      const count = content === null ? 1 : 3;
      assert.equal(exp.waste_wast_results_total(), count);
      assert.equal(exp.waste_wast_results_passed(), count);
    }
    console.log("PASS browser current-tree mount: added file, changed size/bytes, deletion without metadata refresh");
  } finally { fs.rmSync(currentRoot, {recursive:true, force:true}); }
  const vfs = packageVfs(page);
  const malformed = JSON.parse(vfs.inventory);
  malformed.entries[0].path = "/../escape";
  assert.notEqual(stage(Buffer.from(JSON.stringify(malformed)), exp.waste_wast_stage_vfs_inventory), 0);
  assert.equal(stage(vfs.inventory, exp.waste_wast_stage_vfs_inventory), 0);
  assert.notEqual(exp.waste_wast_vfs_ready(), 0);
  const first = vfs.files[0];
  assert.notEqual(stage(Buffer.alloc(first.bytes.length), (p, n) =>
    exp.waste_wast_stage_vfs_file(first.index, p, n)), 0);
  for (const [fixture, count] of [["tests/vfs-mounted-paths.wast", 7], ["tests/guest-sdk-mounted.wast", 12]]) {
   const source = Buffer.from(fs.readFileSync(fixture));
   for (let i = 0; i < 2; i++) {
    stageVfs(exp, vfs);
    assert.equal(stage(source, exp.waste_wast_run_script), 0);
    assert.equal(exp.waste_wast_results_total(), count);
    assert.equal(exp.waste_wast_results_passed(), count);
   }
  }
  stageVfs(exp, vfs);
  assert.equal(stage(probe, exp.waste_wast_run_script), 0);
  const ptr = exp.waste_wast_results_ptr();
  let details = "";
  for (let i = 0; i < exp.waste_wast_results_total(); ++i) {
    const bytes = new Uint8Array(exp.memory.buffer, ptr + i * config.BROWSER_RESULT_BYTES + config.BROWSER_RESULT_ERROR_OFFSET, config.BROWSER_RESULT_ERROR_BYTES);
    const end = bytes.indexOf(0);
    details += Buffer.from(bytes.subarray(0, end)).toString() + "\n";
  }
  assert.equal(exp.waste_wast_results_total(), 4, details);
  assert.equal(exp.waste_wast_results_passed(), 4, details);
  console.log("PASS mounted-only C SDK: real compiled engine varargs, stat/signal canaries and assert trap");
  const corpus = JSON.parse(page.read("root/test/manifest.json"));
  let offset = 16384;
  const data = [], checks = [];
  for (const test of corpus.tests) {
    const path = Buffer.from(test.path + "\0");
    const bytes = page.read(test.path.slice(1));
    data.push(Array.from(path, b => "\\" + b.toString(16).padStart(2,"0")).join(""));
    checks.push(`(local.set $total (i32.add (local.get $total) (call $corpus_open` +
      ` (i32.const ${offset}) (i32.const ${bytes.length}) (i32.const ${bytes[0]}) (i32.const ${bytes[bytes.length-1]}))))`);
    offset += path.length;
  }
  assert(offset < 65536);
  const corpusProbe = Buffer.from(`(module
    (import "env" "open" (func $open (param i32 i32 i32) (result i32)))
    (import "env" "read" (func $read (param i32 i32 i32) (result i32)))
    (import "env" "close" (func $close (param i32) (result i32)))
    (memory (export "__waste_memory") 1)
    (func (export "__errno_location") (result i32) (i32.const 0))
    ;; Pack all path strings into one data segment.
    (data (i32.const 16384) "${data.join("")}")
    (func $corpus_open (param $path i32) (param $size i32) (param $first i32) (param $last i32) (result i32)
      (local $fd i32) (local $count i32) (local $total i32) (local $seen_first i32) (local $seen_last i32)
      (local.set $fd (call $open (local.get $path) (i32.const 0) (i32.const 0)))
      (if (i32.lt_s (local.get $fd) (i32.const 0))
        (then (return (i32.sub (i32.const -1000) (i32.load (i32.const 0))))))
      (block $done (loop $again
        (local.set $count (call $read (local.get $fd) (i32.const 4096) (i32.const 8192)))
        (if (i32.lt_s (local.get $count) (i32.const 0))
          (then (drop (call $close (local.get $fd))) (return (i32.const -102))))
        (br_if $done (i32.eqz (local.get $count)))
        (if (i32.eqz (local.get $total)) (then (local.set $seen_first (i32.load8_u (i32.const 4096)))))
        (local.set $seen_last (i32.load8_u (i32.add (i32.const 4095) (local.get $count))))
        (local.set $total (i32.add (local.get $total) (local.get $count)))
        (if (i32.gt_u (local.get $total) (local.get $size))
          (then (drop (call $close (local.get $fd))) (return (i32.const -103))))
        (br $again)))
      (if (i32.ne (call $close (local.get $fd)) (i32.const 0)) (then (return (i32.const -107))))
      (if (i32.ne (local.get $total) (local.get $size)) (then (return (i32.const -104))))
      (if (i32.ne (local.get $seen_first) (local.get $first)) (then (return (i32.const -105))))
      (if (i32.ne (local.get $seen_last) (local.get $last)) (then (return (i32.const -106))))
      (i32.const 1))
    (func (export "corpus-all") (result i32) (local $total i32)
      ${checks.join("\n")}
      (local.get $total)))
    (assert_return (invoke "corpus-all") (i32.const ${corpus.tests.length}))`);
  stageVfs(exp, vfs);
  assert.equal(stage(corpusProbe, exp.waste_wast_run_script), 0);
  assert.equal(exp.waste_wast_results_total(), 1);
  const resultPointer = exp.waste_wast_results_ptr();
  const failures = [];
  for (let i = 0; i < exp.waste_wast_results_total(); i++) {
    const record = new Uint8Array(exp.memory.buffer, resultPointer + i * config.BROWSER_RESULT_BYTES, config.BROWSER_RESULT_BYTES);
    if (record[0]) continue;
    const message = record.subarray(config.BROWSER_RESULT_ERROR_OFFSET);
    failures.push(Buffer.from(message.subarray(0, message.indexOf(0))).toString());
  }
  assert.equal(exp.waste_wast_results_passed(), 1, failures.join("\n"));
  console.log(`PASS compiled browser corpus: all ${corpus.tests.length} paths open/read through EOF with exact lengths and endpoint bytes`);
  console.log("PASS compiled browser inventory/files: native-identical mounted path assertions, repeated fresh stores, malformed rejection");
})().catch(e => { console.error(e); process.exitCode = 1; });
