"use strict";

// Boundary observation: evaluate the actual frontend model on guest output.
const assert = require("node:assert/strict");
const vm = require("node:vm");

module.exports = function terminalScreen(source) {
  const context = vm.createContext({TextDecoder});
  vm.runInContext(source.toString(), context, {filename: "terminal/model.js"});
  const terminal = new context.WasteTerminalModel(80, 24);
  const checks = [];
  return {
    checks,
    write(text) { terminal.write(text); },
    check(expected) {
      const snapshot = terminal.snapshot();
      const rowText = row => snapshot.cells.slice(row * snapshot.columns,
        (row + 1) * snapshot.columns)
        .map(cell => String.fromCodePoint(cell.code)).join("").trimEnd();
      const line = rowText(snapshot.y);
      if (expected.line !== undefined)
        assert.equal(line, expected.line, "visible terminal text before the next key");
      if (expected.x !== undefined)
        assert.equal(snapshot.x, expected.x, "terminal cursor before the next key");
      if (expected.y !== undefined)
        assert.equal(snapshot.y, expected.y, "terminal row before the next key");
      const rows = {};
      for (const [row, text] of Object.entries(expected.rows || {})) {
        assert(Number.isInteger(Number(row)) && Number(row) >= 0 && Number(row) < snapshot.rows,
          "observed terminal row is inside the screen");
        rows[row] = rowText(Number(row));
        assert.equal(rows[row], text, `terminal row ${row} before the next key`);
      }
      for (const row of expected.noUnderlineRows || []) {
        assert(Number.isInteger(row) && row >= 0 && row < snapshot.rows,
          "observed underline row is inside the screen");
        assert(snapshot.cells.slice(row * snapshot.columns, (row + 1) * snapshot.columns)
          .every(cell => !cell.underline), `terminal row ${row} has no stray underlines`);
      }
      checks.push({line, x: snapshot.x, y: snapshot.y,
        ...(expected.rows ? {rows} : {}),
        ...(expected.noUnderlineRows ? {noUnderlineRows: expected.noUnderlineRows} : {})});
    },
  };
};
