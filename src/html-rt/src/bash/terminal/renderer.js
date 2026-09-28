"use strict";

/*
 * Direct GLF/Bézier terminal renderer.
 *
 * The GLF data and curve coverage shader are derived from the renderer in
 * SCI4THIS/rogue-wasm at revision 28a574d9fe602165e77c52f2b629ffee4477a429.
 * The terminal model remains authoritative; this class only turns its cells
 * into pixels. This path deliberately does not create or sample a glyph
 * texture atlas.
 */
(function (root) {
  /* VT323 v1.0 metrics (the font the vendored GLF data was generated from):
   * unitsPerEm 2048, hhea ascent 1638, descent -409, lineGap 0, and every
   * glyph advance is 851. The GLF export carries outlines only, so these are
   * the font's own hhea/hmtx values recorded here by name. A monospace cell
   * is therefore FONT_ADVANCE x FONT_LINE font units (aspect ~0.416). */
  const FONT_ASCENT = 1638;
  const FONT_DESCENT = -409;
  const FONT_ADVANCE = 851;
  const FONT_LINE = FONT_ASCENT - FONT_DESCENT;
  /* Underline position/thickness in font units (VT323 post table). */
  const FONT_UNDERLINE_Y = -255;
  const FONT_UNDERLINE_THICKNESS = 102;

  function lookupCmapSubtable(cmap, code) {
    if (!cmap) return null;
    if (cmap.format === 4) {
      const count = cmap.startCode.length;
      for (let i = 0; i < count; i++) {
        if (code < cmap.startCode[i] || code > cmap.endCode[i]) continue;
        if (cmap.idRangeOffset[i] === 0) {
          return (cmap.idDelta[i] + code) & 0xffff;
        }
        const index = i + (cmap.idRangeOffset[i] >> 1) +
          (code - cmap.startCode[i]);
        if (index < cmap.idRangeOffset.length) return cmap.idRangeOffset[index];
        const glyphIndex = index - count;
        if (glyphIndex < cmap.glyphIdArray.length) {
          return cmap.glyphIdArray[glyphIndex];
        }
        return null;
      }
      return null;
    }
    if (cmap.format === 6 && code >= cmap.firstCode &&
        code < cmap.firstCode + cmap.entryCount) {
      return cmap.glyphIdArray[code - cmap.firstCode];
    }
    return null;
  }

  function lookupCmap(glyphFont, code) {
    const subtables = glyphFont?.cmap?.subtables || [];
    for (const cmap of subtables) {
      const glyph = lookupCmapSubtable(cmap, code);
      if (glyph !== null && glyph !== undefined) return glyph;
    }
    return null;
  }

  class TerminalRenderer {
    constructor(canvas, model) {
      this.canvas = canvas;
      this.model = model;
      /* Device-pixel cell size, derived from the canvas' CSS width in resize(). */
      this.cellWidth = 8;
      this.cellHeight = 19;
      this.ratio = 1;
      this.dirty = true;
      this.frame = 0;
      this.gl = null;
      this.ctx = null;
      this.glf = root.WasteTerminalGLF || root.glf || null;
      this.glyphProgram = null;
      this.backgroundProgram = null;
      this.glyphVao = null;
      this.backgroundBuffers = null;
      this.glyphRanges = new Map();
      this.matrix = new Float32Array(16);
      this.useWebGL = false;
      this.resizeObserver = null;
      this.init();
    }

    init() {
      this.canvas.setAttribute("role", "log");
      this.canvas.setAttribute("aria-label", "WASTE Bash terminal");
      try {
        /* MSAA smooths the straight outline edges (triangle edges); the
         * fragment shader smooths the curved edges analytically. */
        this.gl = this.canvas.getContext("webgl2", {antialias: true, alpha: false});
      } catch (_) { this.gl = null; }
      if (this.gl && this.glf) {
        try { this.initWebGL(); this.useWebGL = true; }
        catch (_) { this.gl = null; this.useWebGL = false; }
      }
      if (!this.useWebGL) this.ctx = this.canvas.getContext("2d");
      /* Observe the container, not the canvas: resize() sets the canvas' own
       * CSS size, so observing it would feed back into itself. */
      const target = this.canvas.parentElement || this.canvas;
      this.resizeObserver = typeof ResizeObserver === "function"
        ? new ResizeObserver(() => this.resize()) : null;
      this.resizeObserver?.observe(target);
      if (typeof matchMedia === "function") this.watchPixelRatio();
      this.resize();
    }

    /* devicePixelRatio changes (zoom, moving between monitors) do not
     * necessarily resize the container, so watch them explicitly. */
    watchPixelRatio() {
      const query = matchMedia(`(resolution: ${devicePixelRatio || 1}dppx)`);
      query.addEventListener?.("change", () => { this.resize(); this.watchPixelRatio(); },
        {once: true});
    }

    initWebGL() {
      const gl = this.gl;
      const glyphVertex = `#version 300 es
        in vec2 aCoord; in vec2 aCurve;
        uniform mat4 uCell;
        out vec2 vCurve;
        void main() {
          vCurve = aCurve;
          gl_Position = uCell * vec4(aCoord, 0.0, 1.0);
        }`;
      /* Same region test as rogue-wasm (inside when 1 - s^2 >= t, inverted
       * for t < 0), expressed as a signed function f so its screen-space
       * gradient gives an approximate pixel distance to the curve. Solid
       * interior triangles have (s,t) = (0,1): f == 0 with zero gradient,
       * which is treated as fully covered. */
      const glyphFragment = `#version 300 es
        precision highp float;
        uniform vec4 uColor;
        in vec2 vCurve;
        out vec4 outColor;
        void main() {
          float s = vCurve.x;
          float t = vCurve.y;
          float f = (t < 0.0 ? -1.0 : 1.0) * (1.0 - s * s - abs(t));
          float g = length(vec2(dFdx(f), dFdy(f)));
          float coverage = g > 0.0 ? clamp(0.5 + f / g, 0.0, 1.0) : 1.0;
          if (coverage <= 0.0) discard;
          outColor = vec4(uColor.rgb, uColor.a * coverage);
        }`;
      /* aCell = (column, row, top offset within the cell, height) in cell
       * units, so the same program draws cell backgrounds and underlines. */
      const backgroundVertex = `#version 300 es
        in vec2 aCorner; in vec4 aCell; in vec4 aColor;
        uniform vec2 uGrid;
        out vec4 vColor;
        void main() {
          vec2 p = vec2(aCell.x + aCorner.x, aCell.y + aCell.z + aCorner.y * aCell.w) / uGrid;
          gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
          vColor = aColor;
        }`;
      const backgroundFragment = `#version 300 es
        precision mediump float;
        in vec4 vColor;
        out vec4 outColor;
        void main() { outColor = vColor; }`;

      this.glyphProgram = this.link(glyphVertex, glyphFragment);
      this.backgroundProgram = this.link(backgroundVertex, backgroundFragment);
      this.uniforms = {
        cell: gl.getUniformLocation(this.glyphProgram, "uCell"),
        color: gl.getUniformLocation(this.glyphProgram, "uColor"),
        grid: gl.getUniformLocation(this.backgroundProgram, "uGrid"),
      };
      this.glyphVao = gl.createVertexArray();
      gl.bindVertexArray(this.glyphVao);
      const pointBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, pointBuffer);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(this.glf.pts), gl.STATIC_DRAW);
      const coord = gl.getAttribLocation(this.glyphProgram, "aCoord");
      const curve = gl.getAttribLocation(this.glyphProgram, "aCurve");
      gl.enableVertexAttribArray(coord);
      gl.vertexAttribPointer(coord, 2, gl.FLOAT, false, 16, 0);
      gl.enableVertexAttribArray(curve);
      gl.vertexAttribPointer(curve, 2, gl.FLOAT, false, 16, 8);
      const indexBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(this.glf.idx), gl.STATIC_DRAW);
      gl.bindVertexArray(null);

      const corner = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, corner);
      gl.bufferData(gl.ARRAY_BUFFER,
        new Float32Array([0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1]), gl.STATIC_DRAW);
      this.backgroundBuffers = {corner, cell: gl.createBuffer(), color: gl.createBuffer()};
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    }

    link(vertexSource, fragmentSource) {
      const gl = this.gl;
      const compile = (type, source) => {
        const shader = gl.createShader(type);
        gl.shaderSource(shader, source); gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
          const message = gl.getShaderInfoLog(shader); gl.deleteShader(shader);
          throw new Error(message || "terminal shader compilation failed");
        }
        return shader;
      };
      const program = gl.createProgram();
      gl.attachShader(program, compile(gl.VERTEX_SHADER, vertexSource));
      gl.attachShader(program, compile(gl.FRAGMENT_SHADER, fragmentSource));
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        throw new Error(gl.getProgramInfoLog(program) || "terminal program link failed");
      }
      return program;
    }

    /* Size the backing store in whole device pixels per cell so that every
     * cell starts on a pixel boundary and the canvas is shown 1:1 (no CSS
     * resampling). The cell height follows from the font's own aspect. */
    resize() {
      const ratio = typeof devicePixelRatio === "number" ? devicePixelRatio : 1;
      const container = this.canvas.parentElement;
      const available = container ? container.clientWidth -
        parseFloat(getComputedStyle(container).paddingLeft || 0) -
        parseFloat(getComputedStyle(container).paddingRight || 0) : 0;
      const cssWidth = available > 0 ? available : this.model.columns * 8;
      const cellWidth = Math.max(4, Math.floor(cssWidth * ratio / this.model.columns));
      const cellHeight = Math.max(8, Math.round(cellWidth * FONT_LINE / FONT_ADVANCE));
      const width = cellWidth * this.model.columns;
      const height = cellHeight * this.model.rows;
      this.ratio = ratio;
      this.cellWidth = cellWidth;
      this.cellHeight = cellHeight;
      if (this.canvas.width !== width || this.canvas.height !== height) {
        this.canvas.width = width; this.canvas.height = height;
      }
      this.canvas.style.width = `${width / ratio}px`;
      this.canvas.style.height = `${height / ratio}px`;
      this.canvas.style.minHeight = "0";
      this.canvas.style.aspectRatio = "";
      if (this.useWebGL) this.gl.viewport(0, 0, width, height);
      this.dirty = true; this.render();
    }

    /* Coalesce bursts of output into one frame. */
    markDirty() {
      this.dirty = true;
      if (typeof requestAnimationFrame !== "function") { this.render(); return; }
      if (this.frame) return;
      this.frame = requestAnimationFrame(() => { this.frame = 0; this.render(); });
    }

    render() {
      if (!this.dirty) return;
      this.dirty = false;
      if (this.useWebGL) this.renderWebGL(); else this.renderCanvas();
    }

    glyphRange(code) {
      let range = this.glyphRanges.get(code);
      if (range === undefined) {
        const glyph = lookupCmap(this.glf, code);
        /* Unmapped code points use glyph 0 (.notdef), as the plan requires. */
        range = this.glf.lookup[glyph == null ? 0 : glyph] || null;
        if (range && range.len === 0) range = null;
        this.glyphRanges.set(code, range);
      }
      return range;
    }

    renderWebGL() {
      const gl = this.gl, model = this.model, cells = model.active.cells;
      const columns = model.columns, rows = model.rows;
      const underlineTop = (FONT_ASCENT - FONT_UNDERLINE_Y) / FONT_LINE;
      const underlineHeight = Math.max(1 / this.cellHeight,
        Math.round(FONT_UNDERLINE_THICKNESS / FONT_LINE * this.cellHeight) / this.cellHeight);
      let underlines = 0;
      for (const cell of cells) if (cell.underline) underlines++;
      const instances = cells.length + underlines;
      const cellData = new Float32Array(instances * 4);
      const colorData = new Float32Array(instances * 4);
      let u = cells.length;
      for (let i = 0; i < cells.length; i++) {
        const cell = cells[i], x = i % columns, y = (i / columns) | 0;
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        cellData.set([x, y, 0, 1], i * 4);
        colorData.set(color(cursor ? cell.fg : cell.bg), i * 4);
        if (cell.underline) {
          cellData.set([x, y, underlineTop, underlineHeight], u * 4);
          colorData.set(color(cursor ? cell.bg : cell.fg), u * 4);
          u++;
        }
      }
      gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
      gl.useProgram(this.backgroundProgram);
      gl.uniform2f(this.uniforms.grid, columns, rows);
      this.attribute(this.backgroundBuffers.corner, this.backgroundProgram,
        "aCorner", 2, 0, null);
      this.attribute(this.backgroundBuffers.cell, this.backgroundProgram,
        "aCell", 4, 1, cellData);
      this.attribute(this.backgroundBuffers.color, this.backgroundProgram,
        "aColor", 4, 1, colorData);
      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, instances);

      gl.useProgram(this.glyphProgram);
      gl.bindVertexArray(this.glyphVao);
      const width = this.canvas.width, height = this.canvas.height;
      const cellWidth = this.cellWidth, cellHeight = this.cellHeight;
      /* One uniform scale for both axes keeps the glyph's true proportions;
       * the rounded cell height can differ from the ideal by < 1 px, which
       * is absorbed by centring horizontally. */
      const scale = cellHeight / FONT_LINE;
      const offsetX = Math.round((cellWidth - FONT_ADVANCE * scale) / 2);
      const baseline = Math.round(FONT_ASCENT * scale);
      const m = this.matrix;
      m.fill(0);
      m[0] = 2 * scale / width;
      m[5] = 2 * scale / height;
      m[10] = 1; m[15] = 1;
      /* Emboldening: redraw 1 device pixel to the right (classic terminal
       * double-strike), scaled to stay visible on high-DPI displays. */
      const boldShift = Math.max(1, Math.round(this.ratio));
      for (let i = 0; i < cells.length; i++) {
        const cell = cells[i], code = cell.code;
        if (code === 32) continue;
        const range = this.glyphRange(code);
        if (!range) continue;
        const x = i % columns, y = (i / columns) | 0;
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        gl.uniform4fv(this.uniforms.color, color(cursor ? cell.bg : cell.fg));
        const passes = cell.bold ? 2 : 1;
        for (let pass = 0; pass < passes; pass++) {
          const px = x * cellWidth + offsetX + pass * boldShift;
          const py = y * cellHeight + baseline;
          m[12] = -1 + 2 * px / width;
          m[13] = 1 - 2 * py / height;
          gl.uniformMatrix4fv(this.uniforms.cell, false, m);
          gl.drawElements(gl.TRIANGLES, range.len, gl.UNSIGNED_INT, range.start * 4);
        }
      }
      gl.bindVertexArray(null);
    }

    attribute(buffer, program, name, size, divisor, data) {
      const gl = this.gl, location = gl.getAttribLocation(program, name);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      if (data) gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(location); gl.vertexAttribPointer(location, size,
        gl.FLOAT, false, 0, 0); gl.vertexAttribDivisor(location, divisor);
    }

    renderCanvas() {
      if (!this.ctx) return;
      const ctx = this.ctx, model = this.model;
      const w = this.cellWidth, h = this.cellHeight;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.textBaseline = "alphabetic";
      for (let i = 0; i < model.active.cells.length; i++) {
        const cell = model.active.cells[i], x = i % model.columns;
        const y = Math.floor(i / model.columns);
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        ctx.fillStyle = cursor ? cell.fg : cell.bg;
        ctx.fillRect(x * w, y * h, w, h);
        ctx.fillStyle = cursor ? cell.bg : cell.fg;
        ctx.font = `${cell.bold ? "bold " : ""}${Math.round(h * 0.8)}px monospace`;
        ctx.fillText(String.fromCodePoint(cell.code), x * w,
          y * h + Math.round(h * FONT_ASCENT / FONT_LINE), w);
      }
    }
  }

  function color(value) {
    const number = Number.parseInt(String(value).slice(1), 16);
    return [(number >> 16 & 255) / 255, (number >> 8 & 255) / 255,
      (number & 255) / 255, 1];
  }

  root.WasteTerminalLookupCmap = lookupCmap;
  root.WasteTerminalGLF = root.glf;
  root.WasteTerminalRenderer = TerminalRenderer;
})(typeof globalThis !== "undefined" ? globalThis : self);
