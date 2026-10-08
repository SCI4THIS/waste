"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const {execFileSync} = require("node:child_process");

/* Transport extracted files as separate buffers. This helper authors no
 * filesystem serialization; production uses loader.js and tarballjs. */
function installedVfs(manifest, read) {
  const files = [];
  for (const [index, e] of manifest.entries.entries()) {
    if (e.role === "directory" || e.role === "interpreter") continue;
    const bytes = read(e.path);
    assert.equal(bytes.length, e.size);
    const hash = crypto.createHash("sha256").update(bytes).digest("hex");
    if (e.sha256) assert.equal(hash, e.sha256);
    else e.sha256 = hash; // Synthetic host-boundary fixtures.
    files.push({index, bytes});
  }
  return {inventory:Buffer.from(JSON.stringify(manifest)), files};
}
function treeManifest(root) {
  return JSON.parse(execFileSync("python3", [path.join(__dirname, "../src/html-rt/tools/vfs.py"),
    "manifest", "--root", path.resolve(root)], {encoding:"utf8"}));
}
function treeVfs(root) {
  return installedVfs(treeManifest(root), name => fs.readFileSync(path.join(root, name.slice(1))));
}

function packageVfs(page) {
  const vfs = installedVfs(JSON.parse(page.read("vfs-manifest.json")), name => page.read(name.slice(1)));
  vfs.inventory = page.read("vfs-manifest.json");
  return vfs;
}
function stageVfs(exp, vfs) {
  const submit = (bytes, call) => {
    const ptr = exp.waste_wast_alloc(bytes.length || 1);
    assert(ptr);
    try {
      new Uint8Array(exp.memory.buffer, ptr, bytes.length).set(bytes);
      assert.equal(call(ptr, bytes.length), 0);
    } finally { exp.waste_wast_free(ptr); }
  };
  submit(vfs.inventory, exp.waste_wast_stage_vfs_inventory);
  for (const file of vfs.files)
    submit(file.bytes, (ptr, n) => exp.waste_wast_stage_vfs_file(file.index, ptr, n));
  assert.equal(exp.waste_wast_vfs_ready(), 0);
}
module.exports = {installedVfs, treeManifest, treeVfs, packageVfs, stageVfs};
