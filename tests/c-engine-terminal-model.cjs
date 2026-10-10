#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const root = path.resolve(__dirname, "..");
const context = {TextDecoder, console};
vm.createContext(context);
vm.runInContext(fs.readFileSync(
  path.join(root, "src/html-rt/src/terminal/model.js"), "utf8"), context,
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

function scrollingScreen() {
  const screen = new context.WasteTerminalModel(8, 5);
  ["HEADER", "ONE", "TWO", "THREE", "STATUS"].forEach((text, row) =>
    screen.write(`\x1b[${row + 1};1H${text}`));
  screen.write("\x1b[2;4r");
  return screen;
}

function checkRows(screen, expected, message) {
  const state = screen.snapshot();
  const actual = Array.from({length: state.rows}, (_, row) =>
    state.cells.slice(row * state.columns, (row + 1) * state.columns)
      .map(cell => String.fromCodePoint(cell.code)).join("").trimEnd());
  check(JSON.stringify(actual) === JSON.stringify(expected),
    `${message}: ${JSON.stringify(actual)}`);
}

let scrollTerminal = scrollingScreen();
scrollTerminal.write("\x1b[2;3H\x1b[");
scrollTerminal.write("M");
checkRows(scrollTerminal, ["HEADER", "TWO", "THREE", "", "STATUS"],
  "split delete-line scrolls the text and preserves rows outside the region");
check(scrollTerminal.x === 0 && scrollTerminal.y === 1,
  "delete-line returns to the left margin without moving rows");
scrollTerminal.write("\x1b[3;4H\x1b[L");
checkRows(scrollTerminal, ["HEADER", "TWO", "", "THREE", "STATUS"],
  "insert-line shifts only rows at and below the cursor");
scrollTerminal.write("\x1b[5;1H\x1b[M\x1b[L");
checkRows(scrollTerminal, ["HEADER", "TWO", "", "THREE", "STATUS"],
  "line insertion and deletion outside the region are ignored");

scrollTerminal = scrollingScreen();
scrollTerminal.write("\x1b[4;3H\x1bD");
checkRows(scrollTerminal, ["HEADER", "TWO", "THREE", "", "STATUS"],
  "index scrolls at the bottom margin");
check(scrollTerminal.x === 2 && scrollTerminal.y === 3,
  "index preserves cursor column and bottom-margin row");
scrollTerminal.write("\x1b[2;3H\x1bM");
checkRows(scrollTerminal, ["HEADER", "", "TWO", "THREE", "STATUS"],
  "reverse index scrolls down at the top margin");
scrollTerminal.write("\x1b[4;3H\x1bE");
check(scrollTerminal.x === 0 && scrollTerminal.y === 3,
  "next-line indexes and returns to the left margin");
scrollTerminal.write("\x1b[5;3H\n");
checkRows(scrollTerminal, ["HEADER", "TWO", "THREE", "", "STATUS"],
  "newline outside the bottom margin does not scroll the region");

scrollTerminal = scrollingScreen();
scrollTerminal.write("\x1b[3;3H\x1b[2S");
checkRows(scrollTerminal, ["HEADER", "THREE", "", "", "STATUS"],
  "scroll-up count applies to the complete region");
check(scrollTerminal.x === 2 && scrollTerminal.y === 2,
  "explicit scroll does not move the cursor");
scrollTerminal.write("\x1b[0T");
checkRows(scrollTerminal, ["HEADER", "", "THREE", "", "STATUS"],
  "zero scroll-down count defaults to one");
scrollTerminal.write("\x1b[44m\x1b[999L");
checkRows(scrollTerminal, ["HEADER", "", "", "", "STATUS"],
  "large insert-line count stops at the bottom margin");
check(scrollTerminal.cell(0, 2).bg === "#0000aa" &&
      scrollTerminal.cell(0, 4).bg === "#000000",
  "scroll blanks use the current background without recoloring the status row");

scrollTerminal = scrollingScreen();
scrollTerminal.write("\x1b[2;1H\x1b[2M");
checkRows(scrollTerminal, ["HEADER", "THREE", "", "", "STATUS"],
  "delete multiple lines");
scrollTerminal.write("\x1b[3;3H\x1b[4;2r");
check(scrollTerminal.x === 2 && scrollTerminal.y === 2,
  "invalid reversed margins leave the cursor and region unchanged");
scrollTerminal.write("\x1b[999S");
checkRows(scrollTerminal, ["HEADER", "", "", "", "STATUS"],
  "large scroll count stops at the region boundaries");
scrollTerminal.write("\x1b[r\x1b[5;1H\n");
checkRows(scrollTerminal, ["", "", "", "STATUS", ""],
  "default margins restore full-screen scrolling");

scrollTerminal = scrollingScreen();
scrollTerminal.write("\x1b[?1049h\x1b[2;3r\x1b[?1049l\x1b[4;1H\n");
checkRows(scrollTerminal, ["HEADER", "TWO", "THREE", "", "STATUS"],
  "alternate-screen margins do not change primary-screen margins");
scrollTerminal.resize(8, 6);
scrollTerminal.write("\x1b[6;1HLAST\n");
checkRows(scrollTerminal, ["TWO", "THREE", "", "STATUS", "LAST", ""],
  "resize resets margins to the new screen bounds");

scrollTerminal = scrollingScreen();
scrollTerminal.write("\x1b[3;1H\x1b[4mTWO\x1b[0m\x1b[5;1H\x1b[4mSTATUS");
scrollTerminal.write("\x1b[1;31;44m\x1b[4;1H\n");
checkRows(scrollTerminal, ["HEADER", "TWO", "THREE", "", "STATUS"],
  "scrolling with underlined status attributes preserves the text");
check(scrollTerminal.cell(0, 1).underline && scrollTerminal.cell(0, 4).underline,
  "scrolling preserves decorations on existing text and the status row");
check(Array.from({length: 8}, (_, x) => scrollTerminal.cell(x, 3))
  .every(cell => !cell.underline && !cell.bold && !cell.inverse &&
    cell.fg === "#aa0000" && cell.bg === "#0000aa"),
  "new scroll blanks preserve colors without carrying text decorations");
scrollTerminal.write("\n");
check(Array.from({length: 16}, (_, i) => scrollTerminal.cell(i % 8, 2 + (i >> 3)))
  .every(cell => !cell.underline), "repeated scrolling does not accumulate underlines");
scrollTerminal.write(" ");
check(scrollTerminal.cell(0, 3).underline && scrollTerminal.cell(0, 3).bold,
  "scrolling does not reset the rendition used for subsequently printed text");
scrollTerminal.write("\x1b[2;1H\x1b[L");
check(Array.from({length: 8}, (_, x) => scrollTerminal.cell(x, 1))
  .every(cell => !cell.underline && !cell.bold),
  "inserted lines also introduce undecorated blank cells");

const queryTerminal = new context.WasteTerminalModel(8, 3);
queryTerminal.write("\x1b[?4");
queryTerminal.write("mA");
check(!queryTerminal.cell(0, 0).underline,
  "Vim's split modifyOtherKeys query does not enable underline");
queryTerminal.write("\x1b[1;31m\x1b[>4;2mB");
check(queryTerminal.cell(1, 0).bold && queryTerminal.cell(1, 0).fg === "#aa0000",
  "private keyboard-mode requests do not reset ordinary text rendition");
queryTerminal.write("\x1b[4m\x1b[?0m\x1b[>0mC");
check(queryTerminal.cell(2, 0).underline && queryTerminal.cell(2, 0).bold,
  "ignored private queries preserve intentionally selected decorations");
queryTerminal.write("\x1b[24mD");
check(!queryTerminal.cell(3, 0).underline,
  "ordinary SGR still resets underline after private requests");

console.log("C-engine terminal model tests passed");
