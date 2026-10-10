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
      this.applicationCursorKeys = false;
      this.applicationKeypad = false;
      this.insertMode = false;
      this.state = "normal";
      this.csi = "";
      this.osc = "";
      this.oscEscaped = false;
      this.lastGraphic = null;
      this.decoder = new TextDecoder("utf-8", {fatal: false});
      this.current = {fg: PALETTE[7], bg: PALETTE[0], bold: false,
        underline: false, inverse: false};
    }

    makeScreen() {
      return {columns: this.columns, rows: this.rows, x: 0, y: 0,
        scrollTop: 0, scrollBottom: this.rows - 1, cells: Array.from(
        {length: this.columns * this.rows}, blankCell)};
    }

    resetScreen(screen) {
      screen.x = 0;
      screen.y = 0;
      screen.scrollTop = 0;
      screen.scrollBottom = this.rows - 1;
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

    eraseCell() {
      /* Scrolling creates erased cells, not printed spaces. Preserve the
       * effective colors, but do not turn active text decorations (e.g.
       * Vim's underlined status row) into lines across the blank area. */
      return {code: 32,
        fg: this.current.inverse ? this.current.bg : this.current.fg,
        bg: this.current.inverse ? this.current.fg : this.current.bg,
        bold: false, underline: false, inverse: false};
    }

    scrollLines(top, bottom, count, down = false) {
      count = clamp(count, 1, bottom - top + 1);
      const start = top * this.columns, end = (bottom + 1) * this.columns;
      const offset = count * this.columns;
      const cells = this.active.cells;
      if (down) cells.copyWithin(start + offset, start, end - offset);
      else cells.copyWithin(start, start + offset, end);
      const blankStart = down ? start : end - offset;
      for (let i = blankStart; i < blankStart + offset; i++)
        cells[i] = this.eraseCell();
    }

    lineFeed() {
      if (this.y === this.active.scrollBottom)
        this.scrollLines(this.active.scrollTop, this.active.scrollBottom, 1);
      else this.y++;
    }

    reverseIndex() {
      if (this.y === this.active.scrollTop)
        this.scrollLines(this.active.scrollTop, this.active.scrollBottom, 1, true);
      else this.y--;
    }

    changeLines(count, insert) {
      if (this.y < this.active.scrollTop || this.y > this.active.scrollBottom) return;
      this.scrollLines(this.y, this.active.scrollBottom, count, insert);
      this.x = 0;
    }

    setScrollRegion(top, bottom) {
      top = (top || 1) - 1;
      bottom = (bottom || this.rows) - 1;
      if (top < 0 || bottom >= this.rows || top >= bottom) return;
      this.active.scrollTop = top;
      this.active.scrollBottom = bottom;
      this.x = 0; this.y = 0;
    }

    put(code, cellOverride = null) {
      if (code < 0x20 || code === 0x7f) return;
      const cell = cellOverride ? {...cellOverride} : {...this.current, code};
      if (!cellOverride && cell.inverse) [cell.fg, cell.bg] = [cell.bg, cell.fg];
      if (this.insertMode) {
        /* IRM (CSI 4 h): shift cells right from the cursor so readline's
         * mid-line insertions do not overwrite the trailing characters. */
        const row = this.y;
        for (let column = this.columns - 1; column > this.x; column--)
          this.active.cells[this.index(column, row)] =
            this.active.cells[this.index(column - 1, row)];
      }
      this.active.cells[this.index(this.x, this.y)] = cell;
      this.lastGraphic = {...cell};
      if (this.x === this.columns - 1) {
        this.active.x = 0;
        this.lineFeed();
      } else this.x++;
    }

    repeatLast(count) {
      if (!this.lastGraphic) return;
      count = clamp(count || 1, 1, this.columns * this.rows);
      for (let i = 0; i < count; i++)
        this.put(this.lastGraphic.code, this.lastGraphic);
    }

    keySequence(key) {
      const cursorPrefix = this.applicationCursorKeys ? "\x1bO" : "\x1b[";
      const sequences = {
        ArrowUp: cursorPrefix + "A", ArrowDown: cursorPrefix + "B",
        ArrowRight: cursorPrefix + "C", ArrowLeft: cursorPrefix + "D",
        Home: cursorPrefix + "H", End: cursorPrefix + "F",
        Insert: "\x1b[2~", Delete: "\x1b[3~",
        PageUp: "\x1b[5~", PageDown: "\x1b[6~",
        Escape: "\x1b", F1: "\x1bOP", F2: "\x1bOQ",
        F3: "\x1bOR", F4: "\x1bOS", F5: "\x1b[15~",
        F6: "\x1b[17~", F7: "\x1b[18~", F8: "\x1b[19~",
        F9: "\x1b[20~", F10: "\x1b[21~", F11: "\x1b[23~",
        F12: "\x1b[24~",
      };
      return sequences[key] || null;
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
      if (this.state === "charset") {
        /* ESC ( F and ESC ) F select a G0/G1 character set.  WASTE renders
         * Unicode directly, so designation changes no glyph mapping yet,
         * but the final designator byte must be consumed rather than drawn. */
        this.state = "normal";
        return;
      }
      if (this.state === "esc") {
        if (code === 91) { this.state = "csi"; this.csi = ""; return; }
        if (code === 93) { this.state = "osc"; this.osc = ""; return; }
        if (code === 40 || code === 41) { this.state = "charset"; return; }
        if (code === 55) this.saveCursor();
        else if (code === 56) this.restoreCursor();
        else if (code === 99) this.fullReset();
        else if (code === 61) this.applicationKeypad = true;
        else if (code === 62) this.applicationKeypad = false;
        else if (code === 68) this.lineFeed();
        else if (code === 69) { this.x = 0; this.lineFeed(); }
        else if (code === 77) this.reverseIndex();
        this.state = "normal";
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
      let prefix = "";
      let source = this.csi;
      if (/^[<=>?]/.test(source)) { prefix = source[0]; source = source.slice(1); }
      const values = source.split(";").map(value => value === "" ? 0 : Number(value));
      return {privateMode: prefix === "?", prefix,
        values: values.map(value => Number.isFinite(value) ? value : 0)};
    }

    executeCsi(final) {
      const {privateMode, prefix, values} = this.params();
      /* Private queries are not ordinary CSI commands. In particular,
       * Vim's CSI ? 4 m (modifyOtherKeys query) must not enable underline. */
      if (prefix && !(privateMode && (final === "h" || final === "l"))) return;
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
        case "X": this.eraseCharacters(first || 1); break;
        case "@": this.insertCharacters(first || 1); break;
        case "P": this.deleteCharacters(first || 1); break;
        case "L": this.changeLines(first || 1, true); break;
        case "M": this.changeLines(first || 1, false); break;
        case "S": this.scrollLines(this.active.scrollTop,
          this.active.scrollBottom, first || 1); break;
        case "T": this.scrollLines(this.active.scrollTop,
          this.active.scrollBottom, first || 1, true); break;
        case "b": this.repeatLast(first || 1); break;
        case "m": this.sgr(values); break;
        case "s": this.saveCursor(); break;
        case "u": this.restoreCursor(); break;
        case "h": case "l":
          if (!privateMode && values.includes(4)) this.insertMode = final === "h";
          if (privateMode && values.includes(1))
            this.applicationCursorKeys = final === "h";
          if (privateMode && (values.includes(1049) || values.includes(47))) {
            if (final === "h") this.enterAlternate(); else this.leaveAlternate();
          }
          if (privateMode && values.includes(25)) this.cursorVisible = final === "h";
          break;
        case "r":
          if (!privateMode) this.setScrollRegion(values[0], values[1]);
          break;
        default: break;
      }
    }

    eraseCharacters(count) {
      const end = this.x + clamp(count, 1, this.columns - this.x);
      for (let x = this.x; x < end; x++) {
        const cell = {...this.current, code: 32};
        if (cell.inverse) [cell.fg, cell.bg] = [cell.bg, cell.fg];
        this.active.cells[this.index(x, this.y)] = cell;
      }
    }

    insertCharacters(count) {
      count = clamp(count, 1, this.columns - this.x);
      const row = this.y;
      for (let column = this.columns - 1; column >= this.x + count; column--)
        this.active.cells[this.index(column, row)] =
          this.active.cells[this.index(column - count, row)];
      for (let column = this.x; column < this.x + count; column++) {
        const cell = {...this.current, code: 32};
        if (cell.inverse) [cell.fg, cell.bg] = [cell.bg, cell.fg];
        this.active.cells[this.index(column, row)] = cell;
      }
    }

    deleteCharacters(count) {
      count = clamp(count, 1, this.columns - this.x);
      const row = this.y;
      for (let column = this.x; column < this.columns - count; column++)
        this.active.cells[this.index(column, row)] =
          this.active.cells[this.index(column + count, row)];
      for (let column = this.columns - count; column < this.columns; column++) {
        const cell = {...this.current, code: 32};
        if (cell.inverse) [cell.fg, cell.bg] = [cell.bg, cell.fg];
        this.active.cells[this.index(column, row)] = cell;
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
      this.applicationCursorKeys = false; this.applicationKeypad = false;
      this.insertMode = false;
      this.lastGraphic = null;
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
