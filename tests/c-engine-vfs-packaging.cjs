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
  const stage = path.join(scratch, "package");
  fs.mkdirSync(stage);
  for (const name of page.directories) fs.mkdirSync(path.join(stage, name), {recursive:true});
  for (const [name, bytes] of page.files) {
    if (!name) continue;
    const dest = path.join(stage, name);
    fs.mkdirSync(path.dirname(dest), {recursive:true});
    fs.writeFileSync(dest, bytes);
  }
  for (const e of manifest.entries) {
    if (e.role !== "interpreter" && e.path !== "/") fs.chmodSync(path.join(stage, e.path.slice(1)), e.mode);
  }
  const archive = path.join(scratch, "package.tar");
  assert.equal(spawnSync("tar", ["-cf", archive, "-C", stage, "."]).status, 0);
  let result = run("archive-audit", "--archive", archive);
  assert.equal(result.status, 0, result.stderr);
  const nativeCli = spawnSync(path.join(root, "build/cli-rt/waste-cli"), ["--vfs-root", tree,
    path.join(root, "tests/vfs-mounted-paths.wast")], {encoding:"utf8"});
  assert.equal(nativeCli.status, 0, nativeCli.stderr);
  assert.equal(JSON.parse(nativeCli.stdout).passed, 6);
  console.log("PASS installed directory/archive bytes, metadata and native mounted paths");

  const copy = path.join(scratch, "vfs");
  fs.cpSync(tree, copy, {recursive:true, preserveTimestamps:true});
  // Host mtimes are deliberately not guest metadata (including a Git checkout).
  fs.rmdirSync(path.join(copy, "root"));
  fs.rmdirSync(path.join(copy, "tmp"));
  fs.utimesSync(path.join(copy, "usr/bin/date"), 0, 0);
  result = run("audit", "--root", copy);
  assert.equal(result.status, 0, result.stderr);
  const original = fs.readFileSync(path.join(copy, ".inventory.json"));
  for (const [change, message] of [
    [m => m.entries.push({...m.entries[0]}), /conflicting destination/],
    [m => m.entries[0].path = "/../escape", /unsafe guest path/],
    [m => m.entries[0].path = "//root", /unsafe guest path/],
    [m => m.entries.reverse(), /first VFS node/],
    [m => m.entries = m.entries.filter(e => e.path !== "/usr/bin/echo"), /missing mandatory/],
  ]) {
    const changed = JSON.parse(original);
    change(changed);
    fs.writeFileSync(path.join(copy, ".inventory.json"), JSON.stringify(changed));
    result = run("audit", "--root", copy);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, message);
  }
  // Preserve exact POSIX metadata with the same copy primitive as installation.
  fs.rmSync(copy, {recursive:true});
  assert.equal(spawnSync("cp", ["-a", tree, copy]).status, 0);
  const before = fs.readFileSync(path.join(copy, ".inventory.json"));
  const partial = path.join(scratch, "partial.wasm");
  fs.writeFileSync(partial, Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]));
  const unknown = path.join(scratch, "unknown-import.wasm");
  fs.writeFileSync(unknown, Buffer.from([0,97,115,109,1,0,0,0,
    1,4,1,96,0,0, 2,15,1,3,101,110,118,7,109,121,115,116,101,114,121,0,0,
    7,10,1,6,95,115,116,97,114,116,0,0]));
  for (const [args, message] of [
    [["--component", "echo", "--source", path.join(scratch, "missing")], /missing mandatory compiler result/],
    [["--component", "echo", "--source", partial], /missing required function exports/],
    [["--component", "echo", "--source", unknown], /new imports require an explicit ABI review/],
    [["--component", "echo", "--source", partial, "--component", "echo", "--source", partial], /conflicting component destinations/],
    [["--component", "upload", "--source", path.join(tree, "usr/bin/upload"),
      "--component", "download", "--source", partial], /missing required function exports/],
  ]) {
    result = run("install", "--root", copy, ...args);
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, message);
    assert.deepEqual(fs.readFileSync(path.join(copy, ".inventory.json")), before);
    assert.deepEqual(fs.readFileSync(path.join(copy, "usr/bin/upload")), fs.readFileSync(path.join(tree, "usr/bin/upload")));
  }
  // Check a successful atomic refresh as well as fail-before-publish paths.
  result = run("install", "--root", copy, "--component", "echo", "--source", path.join(tree, "usr/bin/echo"));
  assert.equal(result.status, 0, result.stderr);
  result = run("audit", "--root", copy);
  assert.equal(result.status, 0, result.stderr);
  fs.writeFileSync(path.join(copy, "usr/bin/echo"), "partial compiler output");
  result = run("install", "--root", copy, "--component", "echo", "--source", path.join(tree, "usr/bin/echo"));
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /modified or partial installed file/);
  console.log("PASS VFS guards: escaping/conflicting/omitted/partial inputs, atomic batch, edited snapshot protection");
} finally {
  fs.rmSync(scratch, {recursive:true, force:true});
}
