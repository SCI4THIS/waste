#!/usr/bin/env node
"use strict";
// Reuse the production API session harness and guest WAST assertions. Supply a
// valid catalogue identity that collides with the former staging-only counter.
const assert = require("node:assert/strict");
const path = require("node:path");
const root = path.resolve(__dirname, "../../..");
const fixtures = require(path.join(root, "src/html-rt/test/vfs-package.cjs"));
const treeVfs = fixtures.treeVfs;
fixtures.treeVfs = directory => {
  const vfs = treeVfs(directory);
  const manifest = JSON.parse(vfs.inventory);
  const bash = manifest.entries.find(entry => entry.path === "/usr/bin/bash");
  assert(bash, "identity fixture requires the installed Bash");
  const previous = bash.inode;
  const displaced = manifest.entries.find(entry => entry !== bash && entry.inode === 1000);
  if (displaced) displaced.inode = previous;
  bash.inode = 1000;
  vfs.inventory = Buffer.from(JSON.stringify(manifest));
  return vfs;
};
process.argv[4] ??= path.join(root, "src/cli-rt/test/staged-identities.json");
require(path.join(root, "src/html-rt/test/guest-session/browser.cjs"));
