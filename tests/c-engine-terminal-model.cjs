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

const rogueTerminal = new context.WasteTerminalModel(30, 8);
rogueTerminal.write("\x1b[2J\x1b[2d -\x1b[23b");
snapshot = rogueTerminal.snapshot();
check(snapshot.cells[1 * 30].code === 32 &&
      Array.from({length: 24}, (_, index) =>
        snapshot.cells[1 * 30 + index + 1].code).every(code => code === 45),
  "Rogue room border uses CSI REP");

rogueTerminal.write("\x1b[3;1H\x1b(BX");
snapshot = rogueTerminal.snapshot();
check(snapshot.cells[2 * 30].code === 88 &&
      snapshot.cells[2 * 30 + 1].code === 32,
  "character-set designation is consumed");

rogueTerminal.write("\x1b[?1h\x1b=");
check(rogueTerminal.applicationCursorKeys && rogueTerminal.applicationKeypad,
  "ncurses application key modes enabled");
check(rogueTerminal.keySequence("ArrowUp") === "\x1bOA" &&
      rogueTerminal.keySequence("PageDown") === "\x1b[6~",
  "application and navigation key sequences");
rogueTerminal.write("\x1b[?1l\x1b>");
check(!rogueTerminal.applicationCursorKeys && !rogueTerminal.applicationKeypad,
  "application key modes disabled");
check(rogueTerminal.keySequence("ArrowUp") === "\x1b[A",
  "normal cursor key sequence");
rogueTerminal.write("\x1b[?25l");
check(!rogueTerminal.cursorVisible,
  "curses can hide the hardware cursor during gameplay");
rogueTerminal.write("\x1b[?25h");
check(rogueTerminal.cursorVisible,
  "curses can restore the hardware cursor on teardown");

const eraseTerminal = new context.WasteTerminalModel(8, 3);
eraseTerminal.write("\x1b[2;3H@\x1b[2;3H\x1b[");
eraseTerminal.write("X");
snapshot = eraseTerminal.snapshot();
check(snapshot.cells[10].code === 32 && snapshot.x === 2 && snapshot.y === 1,
  "split CSI ECH clears the old player without moving the cursor");
eraseTerminal.write("\x1b[2;4H@");
check(eraseTerminal.snapshot().cells.filter(cell => cell.code === 64).length === 1,
  "moving after ECH leaves exactly one player glyph");
eraseTerminal.write("\x1b[2;4H\x1b[0X");
check(eraseTerminal.snapshot().cells[11].code === 32,
  "zero ECH count defaults to one character");

eraseTerminal.write("\x1b[1;1Habcd\x1b[1;2H\x1b[2X");
snapshot = eraseTerminal.snapshot();
check(snapshot.cells[0].code === 97 && snapshot.cells[1].code === 32 &&
      snapshot.cells[2].code === 32 && snapshot.cells[3].code === 100,
  "ECH erases only the requested characters");
eraseTerminal.write("\x1b[3;1HZ\x1b[2;7H@@\x1b[2;7H\x1b[44m\x1b[999X");
snapshot = eraseTerminal.snapshot();
check(snapshot.cells[14].code === 32 && snapshot.cells[15].code === 32 &&
      snapshot.cells[16].code === 90 && snapshot.x === 6 && snapshot.y === 1,
  "ECH stops at the right margin without wrapping or changing another row");
check(snapshot.cells[14].bg === "#0000aa",
  "ECH blanks use the current background color");
eraseTerminal.write("\x1b[b");
check(eraseTerminal.snapshot().cells[14].code === 64,
  "ECH does not replace the last graphic used by REP");

terminal.resize(10, 4);
snapshot = terminal.snapshot();
check(snapshot.columns === 10 && snapshot.rows === 4, "resize dimensions");

console.log("C-engine terminal model tests passed");
