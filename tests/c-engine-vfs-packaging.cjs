#!/usr/bin/env node
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawnSync} = require("node:child_process");
const {readOfflinePackage} = require("./offline-html-package.cjs");
const root = path.resolve(__dirname, "..");
const tree = path.join(root, "src/vfs");
const tool = path.join(root, "src/html-rt/tools/vfs.py");
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "waste-vfs-check-"));
const run = (...args) => spawnSync("python3", [tool, ...args], {cwd:root, encoding:"utf8"});
try {
  const page = readOfflinePackage(process.argv[2] || path.join(root, "build/html-rt/bash.html"));
  const manifest = JSON.parse(page.read("vfs-manifest.json"));
  assert(!page.files.has("vfs-image.bin"));
  for (const e of manifest.entries) {
    if (e.role === "directory" || e.role === "interpreter") continue;
    assert.deepEqual(page.read(e.path.slice(1)), fs.readFileSync(path.join(tree, e.path.slice(1))));
  }
  const stage = path.join(scratch, "archive");
  for (const name of page.directories) fs.mkdirSync(path.join(stage, name), {recursive:true});
  for (const [name, bytes] of page.files) {
    if (!name) continue;
    const dest = path.join(stage, name);
    fs.mkdirSync(path.dirname(dest), {recursive:true});
    fs.writeFileSync(dest, bytes);
  }
  for (const e of manifest.entries)
    if (e.role !== "interpreter" && e.path !== "/") fs.chmodSync(path.join(stage, e.path.slice(1)), e.mode);
  const archive = path.join(scratch, "package.tar");
  const archiveCheck = () => {
    assert.equal(spawnSync("tar", ["-cf", archive, "-C", stage, "."]).status, 0);
    return run("archive-audit", "--archive", archive);
  };
  let result = archiveCheck();
  assert.equal(result.status, 0, result.stderr);
  fs.writeFileSync(path.join(stage, "usr/bin/echo"), "incomplete extracted bytes");
  result = archiveCheck();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /incomplete or changed package file/);

  const copy = path.join(scratch, "vfs");
  fs.cpSync(tree, copy, {recursive:true, preserveTimestamps:true});
  const added = path.join(copy, "tmp/experiment.txt");
  fs.mkdirSync(path.dirname(added), {recursive:true});
  fs.writeFileSync(added, "experimental content");
  fs.writeFileSync(path.join(copy, "usr/include/experiment.h"), "typedef int experiment_type;\n");
  fs.writeFileSync(path.join(copy, "usr/bin/echo"), "local replacement bytes");
  fs.writeFileSync(path.join(copy, ".inventory.json"), "ignored legacy ledger");
  result = run("audit", "--root", copy);
  assert.equal(result.status, 0, result.stderr);
  result = run("package", "--root", copy, "--output", path.join(scratch, "current"));
  assert.equal(result.status, 0, result.stderr);
  const current = JSON.parse(fs.readFileSync(path.join(scratch, "current/vfs-manifest.json")));
  assert(current.entries.some(e => e.path === "/tmp/experiment.txt"));
  assert(current.entries.some(e => e.path === "/usr/include/experiment.h"));
  assert(!current.entries.some(e => e.path === "/.inventory.json"));
  assert.equal(fs.readFileSync(path.join(scratch, "current/tmp/experiment.txt"), "utf8"), "experimental content");
  console.log("PASS current-tree packaging: added files/headers, replaced bytes, generated metadata and archive integrity");

  const partial = path.join(scratch, "partial.wasm");
  fs.writeFileSync(partial, Buffer.from([0,97,115,109,1,0,0,0]));
  for (const args of [
    ["--component", "echo", "--source", path.join(scratch, "missing")],
    ["--component", "echo", "--source", partial],
    ["--component", "echo", "--source", partial, "--component", "echo", "--source", partial],
    ["--component", "upload", "--source", path.join(tree, "usr/bin/upload"), "--component", "download", "--source", partial],
  ]) {
    const before = fs.readFileSync(path.join(copy, "usr/bin/upload"));
    result = run("install", "--root", copy, ...args);
    assert.notEqual(result.status, 0, result.stderr);
    assert.deepEqual(fs.readFileSync(path.join(copy, "usr/bin/upload")), before);
    assert.equal(fs.readFileSync(added, "utf8"), "experimental content");
  }
  // A valid replacement can overwrite experimental/partial existing bytes.
  result = run("install", "--root", copy, "--component", "echo", "--source", path.join(tree, "usr/bin/echo"));
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(fs.readFileSync(path.join(copy, "usr/bin/echo")), fs.readFileSync(path.join(tree, "usr/bin/echo")));
  assert.equal(fs.readFileSync(added, "utf8"), "experimental content");
  assert(!fs.existsSync(path.join(copy, ".inventory.json")));
  fs.symlinkSync(path.join(scratch, "outside"), path.join(copy, "escape"));
  result = run("audit", "--root", copy);
  assert.notEqual(result.status, 0);
  console.log("PASS explicit install: complete candidates, atomic batch failures, replacement of local edits and root confinement");
} finally {
  fs.rmSync(scratch, {recursive:true, force:true});
}
