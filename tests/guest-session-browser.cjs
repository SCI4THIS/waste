#!/usr/bin/env node
"use strict";
const {treeVfs, stageVfs} = require("./vfs-package.cjs");
// Boundary-only driver: fixture/inputs/expectations are shared with native.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const scenario = JSON.parse(fs.readFileSync(process.argv[4] || "tests/guest-session-io.json"));
(async () => {
  let exp, output = "";
  const floating = (p, n) => Number(Buffer.from(new Uint8Array(exp.memory.buffer, p, n)).toString());
  const {instance} = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {waste_host: {
    strtod: floating, strtof: floating, posix_open: () => -1,
    posix_close: () => 0, posix_read: () => -1, wall_clock_ms: () => Date.now(),
    posix_write: (_fd, p, n) => {
      output += Buffer.from(new Uint8Array(exp.memory.buffer, p, n)).toString();
      return n;
    },
  }});
  exp = instance.exports;
  function allocate(bytes) {
    const p = exp.waste_wast_alloc(bytes.length);
    assert(p);
    new Uint8Array(exp.memory.buffer, p, bytes.length).set(bytes);
    return p;
  }
  stageVfs(exp, treeVfs(process.argv[3]));
  for (const file of scenario.files || []) {
    const path = Buffer.from(file.path), bytes = fs.readFileSync(file.source);
    const p = allocate(path), b = allocate(bytes);
    assert.equal(exp.waste_wast_stage_file(p, path.length, b, bytes.length, file.mode), 0);
    exp.waste_wast_free(p); exp.waste_wast_free(b);
  }
  if (scenario.clockRealtimeNs !== undefined || scenario.clockMonotonicNs !== undefined) {
    const low = v => Number(BigInt(v) & 0xffffffffn) >>> 0;
    const high = v => Number(BigInt(v) >> 32n) >>> 0;
    const rt = scenario.clockRealtimeNs ?? 0, mt = scenario.clockMonotonicNs ?? 0;
    assert.equal(exp.waste_wast_set_clock_realtime_ns(low(rt), high(rt)), 0);
    assert.equal(exp.waste_wast_set_clock_monotonic_ns(low(mt), high(mt)), 0);
  }
  exp.waste_wast_enable_terminal();
  if (scenario.testSuiteCapability) assert.equal(exp.waste_wast_enable_test_suite(), 0);
  const cwd = Buffer.from("/root");
  const cwdPtr = allocate(cwd);
  assert.equal(exp.waste_wast_stage_cwd(cwdPtr, cwd.length), 0);
  exp.waste_wast_free(cwdPtr);
  const source = fs.readFileSync(scenario.fixture);
  const sourcePtr = allocate(source);
  if (scenario.events.some(event => event.monotonicNs !== undefined))
    assert.equal(exp.waste_wast_advance_clock_monotonic_ns(1, 0), -1,
      "clock advanced before an external wait");
  let status = exp.waste_wast_run_script(sourcePtr, source.length);
  let consumed = 0;
  const hostIoQueue = (scenario.hostIo || []).slice();
  const downloads = [];
  function drainHostIo() {
    while (status === 1 && exp.waste_wast_wait_kind() === 5) {
      const ioKind = exp.waste_wast_host_io_kind();
      const expected = hostIoQueue.shift();
      assert(expected, `unexpected host-io yield kind=${ioKind}`);
      if (ioKind === 2) {
        const dataPtr = exp.waste_wast_host_io_data_ptr();
        const dataLen = exp.waste_wast_host_io_data_len();
        downloads.push(Array.from(new Uint8Array(exp.memory.buffer, dataPtr, dataLen)));
        assert.equal(expected.kind, "download");
        if (expected.cancel) exp.waste_wast_host_io_cancel();
        else exp.waste_wast_host_io_complete();
      } else if (ioKind === 1) {
        assert.equal(expected.kind, "upload");
        if (expected.cancel) exp.waste_wast_host_io_cancel();
        else {
          const bytes = Buffer.from(expected.bytes);
          const ptr = exp.waste_wast_alloc(bytes.length);
          new Uint8Array(exp.memory.buffer, ptr, bytes.length).set(bytes);
          exp.waste_wast_host_io_provide_upload(ptr, bytes.length);
          exp.waste_wast_free(ptr);
        }
      }
      status = exp.waste_wast_resume();
    }
  }
  drainHostIo();
  for (const event of scenario.events) {
    assert.equal(status, 1, output);
    assert([1, 2].includes(exp.waste_wast_wait_kind()));
    const at = output.indexOf(event.after || "", consumed);
    const evidence = exp.waste_wast_transition_evidence_ptr ? Buffer.from(
      new Uint8Array(exp.memory.buffer, exp.waste_wast_transition_evidence_ptr(),
        exp.waste_wast_transition_evidence_len())).toString() : "";
    assert(at >= 0, `${JSON.stringify(event)}\n${output}\nresults: ${exp.waste_wast_results_passed()}/${exp.waste_wast_results_total()}\ntransitions: ${evidence}`);
    consumed = output.length;
    if (event.pid !== undefined) assert.equal(exp.waste_wast_process_id(), event.pid);
    if (event.waitKind !== undefined) assert.equal(exp.waste_wast_wait_kind(), event.waitKind);
    await new Promise(resolve => setTimeout(resolve, 30));
    for (const rejected of event.rejectMonotonicNs || []) {
      const value = BigInt(rejected);
      assert.equal(exp.waste_wast_advance_clock_monotonic_ns(
        Number(value & 0xffffffffn), Number(value >> 32n)), -1);
    }
    const beforeOutput = output;
    if (event.monotonicNs !== undefined) {
      const value = BigInt(event.monotonicNs);
      assert.equal(exp.waste_wast_advance_clock_monotonic_ns(
        Number(value & 0xffffffffn), Number(value >> 32n)), 0);
    }
    else if (event.resize) assert.equal(exp.waste_wast_resize_terminal(...event.resize), 0);
    else if (event.signal !== undefined) {
      if (event.signalPgid)
        assert.ok(exp.waste_wast_raise_signal_pgid(event.signal, event.signalPgid) > 0);
      else
        assert.equal(exp.waste_wast_raise_signal_pid(event.signal, event.signalPid || 0), 0);
    }
    else if (event.eof) assert.equal(exp.waste_wast_enqueue_eof(), 0);
    else {
      const bytes = event.text === undefined ? Buffer.from(event.bytes) : Buffer.from(event.text);
      // enqueue_input takes ownership, unlike VFS file staging.
      assert.equal(exp.waste_wast_enqueue_input(allocate(bytes), bytes.length), 0);
    }
    status = exp.waste_wast_resume();
    if (event.stillWaiting) {
      assert.equal(status, 1, "guest advanced before its wait was satisfied");
      assert.equal(output, beforeOutput);
      assert.equal(exp.waste_wast_wait_kind(), event.waitKind);
    }
    drainHostIo();
  }
  assert.equal(hostIoQueue.length, 0, `undrained host-io: ${JSON.stringify(hostIoQueue)}`);
  for (const expected of scenario.hostIo || []) {
    if (expected.kind === "download" && expected.expectedBytes && !expected.cancel) {
      assert.deepEqual(downloads.shift(), expected.expectedBytes);
    } else if (expected.kind === "download") downloads.shift();
  }
  assert.equal(status, 0);
  if (scenario.events.some(event => event.monotonicNs !== undefined))
    assert.equal(exp.waste_wast_advance_clock_monotonic_ns(1, 1), -1,
      "completed store accepted a clock event");
  exp.waste_wast_free(sourcePtr);
  const total = exp.waste_wast_results_total(), passed = exp.waste_wast_results_passed();
  const pointer = exp.waste_wast_results_ptr();
  const errors = [], results = [];
  for (let i = 0; i < total; i++) {
    const record = Buffer.from(new Uint8Array(exp.memory.buffer, pointer + i * 256, 256));
    const name = exp.waste_wast_result_name_ptr(i);
    results.push({func:Buffer.from(new Uint8Array(exp.memory.buffer, name,
      exp.waste_wast_result_name_len(i))).toString(), pass:Boolean(record[0])});
    if (!record[0]) errors.push(record.subarray(64).toString().split("\0")[0]);
  }
  assert.equal(total, scenario.assertions, errors.join("\n"));
  assert.equal(passed, scenario.expectedPassed ?? total, errors.join("\n"));
  if (scenario.expectedError) assert(errors.includes(scenario.expectedError), errors.join("\n"));
  if (scenario.output !== undefined) assert.equal(output, scenario.output);
  for (const marker of scenario.contains || []) assert(output.includes(marker), output);
  const exited = Boolean(exp.waste_wast_guest_exited());
  const exitStatus = exp.waste_wast_guest_exit_status();
  assert.equal(exited, scenario.exited ?? true);
  assert.equal(exitStatus, scenario.exitStatus);
  console.log(JSON.stringify({runtime: "browser-guest-session", passed, total, exited, exitStatus, output, results}));
})().catch(error => { console.error(error); process.exitCode = 1; });
