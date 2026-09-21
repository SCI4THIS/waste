"use strict";

/* Small VT/ANSI screen model.  It deliberately has no DOM or WebGL
 * dependency so transcripts can be tested in Node and rendered by more than
 * one frontend. */
(function (root) {
  const PALETTE = [
    "#000000", "#aa0000", "#00aa00", "#aa5500",
    "#0000aa", "#aa00aa", "#00aaaa", "#aaaaaa",
    "#555555", "#ff5555", "#55ff55", "#ffff55",
    "#5555ff", "#ff55ff", "#55ffff", "#ffffff",
  ];

  function blankCell() {
    return {code: 32, fg: PALETTE[7], bg: PALETTE[0], bold: false,
      underline: false, inverse: false};
  }

  function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
  }

  class TerminalModel {
    constructor(columns = 80, rows = 24) {
      this.columns = Math.max(1, columns | 0);
      this.rows = Math.max(1, rows | 0);
      this.primary = this.makeScreen();
      this.alternate = this.makeScreen();
      this.active = this.primary;
      this.saved = {x: 0, y: 0, fg: PALETTE[7], bg: PALETTE[0], bold: false,
        underline: false, inverse: false};
      this.cursorVisible = true;
      this.state = "normal";
      this.csi = "";
      this.osc = "";
      this.oscEscaped = false;
      this.decoder = new TextDecoder("utf-8", {fatal: false});
      this.current = {fg: PALETTE[7], bg: PALETTE[0], bold: false,
        underline: false, inverse: false};
    }

    makeScreen() {
      return {columns: this.columns, rows: this.rows, x: 0, y: 0, cells: Array.from(
        {length: this.columns * this.rows}, blankCell)};
    }

    resetScreen(screen) {
      screen.x = 0;
      screen.y = 0;
      screen.cells.fill(null);
      for (let i = 0; i < screen.cells.length; i++) screen.cells[i] = blankCell();
    }

    get x() { return this.active.x; }
    set x(value) { this.active.x = clamp(value | 0, 0, this.columns - 1); }
    get y() { return this.active.y; }
    set y(value) { this.active.y = clamp(value | 0, 0, this.rows - 1); }

    index(x, y) { return y * this.columns + x; }
    cell(x, y) { return this.active.cells[this.index(x, y)]; }

    snapshot() {
      return {
        columns: this.columns, rows: this.rows, x: this.x, y: this.y,
        cursorVisible: this.cursorVisible,
        cells: this.active.cells.map(cell => ({...cell})),
      };
    }

    resize(columns, rows) {
      columns = Math.max(1, columns | 0);
      rows = Math.max(1, rows | 0);
      if (columns === this.columns && rows === this.rows) return;
      const old = this.active;
      this.columns = columns;
      this.rows = rows;
      /* Preserve the visible upper-left area of both buffers. */
      const oldPrimary = this.primary;
      const oldAlternate = this.alternate;
      this.primary = this.resizeScreen(oldPrimary);
      this.alternate = this.resizeScreen(oldAlternate);
      this.active = old === oldAlternate ? this.alternate : this.primary;
      this.x = old.x;
      this.y = old.y;
    }

    resizeScreen(old) {
      const next = this.makeScreen();
      const oldColumns = old.columns;
      for (let y = 0; y < Math.min(this.rows, old.rows); y++) {
        for (let x = 0; x < Math.min(this.columns, oldColumns); x++) {
          next.cells[this.index(x, y)] = {...old.cells[y * oldColumns + x]};
        }
      }
      next.x = clamp(old.x, 0, this.columns - 1);
      next.y = clamp(old.y, 0, this.rows - 1);
      return next;
    }

    scroll() {
      this.active.cells.splice(0, this.columns);
      for (let i = 0; i < this.columns; i++) this.active.cells.push(blankCell());
      this.active.y = this.rows - 1;
    }

    lineFeed() {
      if (this.y === this.rows - 1) this.scroll();
      else this.y++;
    }

    put(code) {
      if (code < 0x20 || code === 0x7f) return;
      const cell = {...this.current, code};
      if (cell.inverse) [cell.fg, cell.bg] = [cell.bg, cell.fg];
      this.active.cells[this.index(this.x, this.y)] = cell;
      if (this.x === this.columns - 1) {
        this.active.x = 0;
        this.lineFeed();
      } else this.x++;
    }

    write(bytes) {
      const text = typeof bytes === "string" ? bytes : this.decoder.decode(bytes, {stream: true});
      for (const character of text) this.consume(character.codePointAt(0));
    }

    consume(code) {
      if (this.state === "osc") {
        if (code === 7) { this.state = "normal"; this.osc = ""; return; }
        if (code === 27) { this.oscEscaped = true; return; }
        if (this.oscEscaped && code === 92) {
          this.state = "normal"; this.osc = ""; this.oscEscaped = false; return;
        }
        this.oscEscaped = false;
        if (this.osc.length < 4096) this.osc += String.fromCodePoint(code);
        return;
      }
      if (this.state === "csi") {
        if (code >= 0x40 && code <= 0x7e) {
          this.executeCsi(String.fromCodePoint(code));
          this.state = "normal";
        } else if (this.csi.length < 128) this.csi += String.fromCodePoint(code);
        else this.state = "normal";
        return;
      }
      if (this.state === "esc") {
        if (code === 91) { this.state = "csi"; this.csi = ""; return; }
        if (code === 93) { this.state = "osc"; this.osc = ""; return; }
        if (code === 55) this.saveCursor();
        else if (code === 56) this.restoreCursor();
        else if (code === 99) this.fullReset();
        else if (code === 7) this.state = "normal";
        else this.state = "normal";
        return;
      }
      if (code === 27) { this.state = "esc"; return; }
      if (code === 10 || code === 11 || code === 12) { this.lineFeed(); return; }
      if (code === 13) { this.x = 0; return; }
      if (code === 8) { this.x--; return; }
      if (code === 9) { this.x = Math.min(this.columns - 1, (this.x + 8) & ~7); return; }
      if (code < 0x20 || code === 0x7f) return;
      this.put(code);
    }

    params() {
      let privateMode = false;
      let source = this.csi;
      if (source[0] === "?") { privateMode = true; source = source.slice(1); }
      const values = source.split(";").map(value => value === "" ? 0 : Number(value));
      return {privateMode, values: values.map(value => Number.isFinite(value) ? value : 0)};
    }

    executeCsi(final) {
      const {privateMode, values} = this.params();
      const first = values[0] || 0;
      switch (final) {
        case "A": this.y -= first || 1; break;
        case "B": case "e": this.y += first || 1; break;
        case "C": case "a": this.x += first || 1; break;
        case "D": this.x -= first || 1; break;
        case "E": this.y += first || 1; this.x = 0; break;
        case "F": this.y -= first || 1; this.x = 0; break;
        case "G": case "`": this.x = (first || 1) - 1; break;
        case "d": this.y = (first || 1) - 1; break;
        case "H": case "f":
          this.y = (values[0] || 1) - 1; this.x = (values[1] || 1) - 1; break;
        case "J": this.eraseDisplay(first); break;
        case "K": this.eraseLine(first); break;
        case "m": this.sgr(values); break;
        case "s": this.saveCursor(); break;
        case "u": this.restoreCursor(); break;
        case "h": case "l":
          if (privateMode && (values.includes(1049) || values.includes(47))) {
            if (final === "h") this.enterAlternate(); else this.leaveAlternate();
          }
          if (privateMode && values.includes(25)) this.cursorVisible = final === "h";
          break;
        case "r":
          /* Scroll regions are intentionally treated as the full screen. */
          this.y = 0; this.x = 0; break;
        default: break;
      }
    }

    eraseLine(mode) {
      const from = mode === 1 ? 0 : this.x;
      const to = mode === 1 ? this.x : (mode === 2 ? this.columns - 1 : this.columns - 1);
      for (let x = from; x <= to; x++) this.active.cells[this.index(x, this.y)] = blankCell();
    }

    eraseDisplay(mode) {
      if (mode === 2 || mode === 3) {
        for (let i = 0; i < this.active.cells.length; i++) this.active.cells[i] = blankCell();
        return;
      }
      if (mode === 1) {
        for (let y = 0; y < this.y; y++) for (let x = 0; x < this.columns; x++)
          this.active.cells[this.index(x, y)] = blankCell();
        this.eraseLine(1);
      } else {
        this.eraseLine(0);
        for (let y = this.y + 1; y < this.rows; y++) for (let x = 0; x < this.columns; x++)
          this.active.cells[this.index(x, y)] = blankCell();
      }
    }

    sgr(values) {
      if (!values.length) values = [0];
      for (let i = 0; i < values.length; i++) {
        const value = values[i];
        if (value === 0) this.current = {fg: PALETTE[7], bg: PALETTE[0], bold: false, underline: false, inverse: false};
        else if (value === 1) this.current.bold = true;
        else if (value === 22) this.current.bold = false;
        else if (value === 4) this.current.underline = true;
        else if (value === 24) this.current.underline = false;
        else if (value === 7) this.current.inverse = true;
        else if (value === 27) this.current.inverse = false;
        else if (value === 39) this.current.fg = PALETTE[7];
        else if (value === 49) this.current.bg = PALETTE[0];
        else if (value >= 30 && value <= 37) this.current.fg = PALETTE[value - 30];
        else if (value >= 40 && value <= 47) this.current.bg = PALETTE[value - 40];
        else if (value >= 90 && value <= 97) this.current.fg = PALETTE[value - 90 + 8];
        else if (value >= 100 && value <= 107) this.current.bg = PALETTE[value - 100 + 8];
        else if (value === 38 || value === 48) {
          const target = value === 38 ? "fg" : "bg";
          if (values[i + 1] === 5 && values[i + 2] !== undefined) {
            this.current[target] = xtermColor(values[i + 2]); i += 2;
          } else if (values[i + 1] === 2 && values[i + 4] !== undefined) {
            this.current[target] = rgb(values[i + 2], values[i + 3], values[i + 4]); i += 4;
          }
        }
      }
    }

    saveCursor() {
      this.saved = {x: this.x, y: this.y, ...this.current};
    }

    restoreCursor() {
      this.x = this.saved.x; this.y = this.saved.y;
      this.current = {...this.saved};
    }

    enterAlternate() {
      this.saveCursor();
      this.resetScreen(this.alternate);
      this.active = this.alternate;
    }

    leaveAlternate() { this.active = this.primary; this.restoreCursor(); }

    fullReset() {
      this.resetScreen(this.primary); this.resetScreen(this.alternate);
      this.active = this.primary; this.cursorVisible = true; this.state = "normal";
      this.current = {fg: PALETTE[7], bg: PALETTE[0], bold: false, underline: false, inverse: false};
    }
  }

  function rgb(red, green, blue) {
    return "#" + [red, green, blue].map(value => clamp(value | 0, 0, 255).toString(16).padStart(2, "0")).join("");
  }

  function xtermColor(value) {
    value = clamp(value | 0, 0, 255);
    if (value < 16) return PALETTE[value];
    if (value >= 232) { const shade = 8 + (value - 232) * 10; return rgb(shade, shade, shade); }
    const n = value - 16;
    const channel = [0, 95, 135, 175, 215, 255];
    return rgb(channel[Math.floor(n / 36)], channel[Math.floor(n / 6) % 6], channel[n % 6]);
  }

  root.WasteTerminalModel = TerminalModel;
  root.WasteTerminalPalette = PALETTE;
})(typeof globalThis !== "undefined" ? globalThis : self);
