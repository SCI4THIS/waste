#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const context = {TextDecoder, console};
vm.createContext(context);
vm.runInContext(fs.readFileSync(
  path.join(root, "src/html-rt/src/bash/terminal/model.js"), "utf8"), context,
  {filename: "terminal/model.js"});

function check(condition, message) {
  if (!condition) throw new Error(`terminal model check failed: ${message}`);
}

const terminal = new context.WasteTerminalModel(8, 3);
terminal.write("abc");
check(terminal.snapshot().cells[0].code === 97, "printable text");

terminal.write("\x1b[31mR\x1b[0m");
check(terminal.snapshot().cells[3].fg === "#aa0000", "SGR foreground");

terminal.write("\x1b[2J\x1b[HOK");
let snapshot = terminal.snapshot();
check(snapshot.cells[0].code === 79 && snapshot.cells[1].code === 75,
  "erase and home");

terminal.write("\x1b[2;3H@");
snapshot = terminal.snapshot();
check(snapshot.cells[1 * 8 + 2].code === 64, "absolute cursor position");

terminal.write("\x1b[?1049hALT\x1b[?1049l");
check(terminal.snapshot().cells[0].code === 79, "alternate screen restore");

terminal.resize(10, 4);
snapshot = terminal.snapshot();
check(snapshot.columns === 10 && snapshot.rows === 4, "resize dimensions");

console.log("C-engine terminal model tests passed");
