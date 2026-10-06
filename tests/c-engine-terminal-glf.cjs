#!/usr/bin/env node
"use strict";

/* Pure GLF/cmap and range gate. It does not create a DOM, WebGL context, Bash,
 * or the C engine. */
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const context = {};
vm.createContext(context);
vm.runInContext(fs.readFileSync(
  path.join(root, "src/html-rt/src/terminal/glf.js"), "utf8"), context,
  {filename: "terminal/glf.js"});
vm.runInContext(fs.readFileSync(
  path.join(root, "src/html-rt/src/terminal/renderer.js"), "utf8"), context,
  {filename: "terminal/renderer.js"});

if (!context.glf || !context.WasteTerminalLookupCmap) {
  throw new Error("GLF asset or cmap helper was not initialized");
}

const expected = new Map([
  [32, null], [33, [24, 507]], [48, [10692, 1041]],
  [49, [11733, 582]], [65, [24276, 1023]], [73, [31896, 657]],
  [79, [38220, 1110]], [91, [51240, 831]], [124, [77949, 702]],
  [126, [79587, 729]], [0x2603, null],
]);
for (const [code, range] of expected) {
  const glyph = context.WasteTerminalLookupCmap(context.glf, code);
  const actual = glyph == null || !context.glf.lookup[glyph]
    ? null : [context.glf.lookup[glyph].start, context.glf.lookup[glyph].len];
  if (JSON.stringify(actual) !== JSON.stringify(range)) {
    throw new Error(`GLF range mismatch U+${code.toString(16)}: ` +
      `${JSON.stringify(actual)} != ${JSON.stringify(range)}`);
  }
}

if (!Array.isArray(context.glf.pts) || context.glf.pts.length % 4 !== 0 ||
    !Array.isArray(context.glf.idx) || context.glf.idx.length % 3 !== 0) {
  throw new Error("GLF geometry does not contain vec4 points and triangles");
}
console.log(`GLF cmap/range fixture passed (${expected.size} cases, ` +
  `${context.glf.pts.length / 4} vertices, ${context.glf.idx.length / 3} triangles)`);
