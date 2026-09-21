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
  const FONT_XMIN = 0;
  const FONT_YMIN = -409;
  const FONT_SCALE = 0.0004885197850512946;

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
      this.cellWidth = 10;
      this.cellHeight = 18;
      this.dirty = true;
      this.gl = null;
      this.ctx = null;
      this.glf = root.WasteTerminalGLF || root.glf || null;
      this.glyphProgram = null;
      this.backgroundProgram = null;
      this.glyphVao = null;
      this.backgroundBuffers = null;
      this.useWebGL = false;
      this.resizeObserver = null;
      this.init();
    }

    init() {
      this.canvas.setAttribute("role", "log");
      this.canvas.setAttribute("aria-label", "WASTE Bash terminal");
      try {
        this.gl = this.canvas.getContext("webgl2", {antialias: false, alpha: false});
      } catch (_) { this.gl = null; }
      if (this.gl && this.glf) {
        try { this.initWebGL(); this.useWebGL = true; }
        catch (_) { this.gl = null; this.useWebGL = false; }
      }
      if (!this.useWebGL) this.ctx = this.canvas.getContext("2d");
      this.resizeObserver = typeof ResizeObserver === "function"
        ? new ResizeObserver(() => this.resize()) : null;
      this.resizeObserver?.observe(this.canvas);
      this.resize();
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
      const glyphFragment = `#version 300 es
        precision highp float;
        uniform vec4 uColor;
        in vec2 vCurve;
        out vec4 outColor;
        void main() {
          float flip = 0.0;
          float s = vCurve.x;
          float t = vCurve.y;
          if (t < 0.0) { flip = 1.0; t = -t; }
          float inside = (1.0 - (s * s) < t) ? flip : (1.0 - flip);
          if (inside == 1.0) outColor = uColor; else discard;
        }`;
      const backgroundVertex = `#version 300 es
        in vec2 aCorner; in vec4 aCell; in vec4 aColor;
        uniform vec2 uGrid;
        out vec4 vColor;
        void main() {
          vec2 p = (aCell.xy + aCorner) / uGrid;
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

    resize() {
      const ratio = typeof devicePixelRatio === "number" ? devicePixelRatio : 1;
      const width = Math.max(1, Math.round(this.model.columns * this.cellWidth * ratio));
      const height = Math.max(1, Math.round(this.model.rows * this.cellHeight * ratio));
      this.canvas.width = width; this.canvas.height = height;
      this.canvas.style.aspectRatio = `${this.model.columns * this.cellWidth} / ${this.model.rows * this.cellHeight}`;
      if (this.useWebGL) this.gl.viewport(0, 0, width, height);
      this.dirty = true; this.render();
    }

    markDirty() { this.dirty = true; this.render(); }

    render() {
      if (!this.dirty) return;
      this.dirty = false;
      if (this.useWebGL) this.renderWebGL(); else this.renderCanvas();
    }

    renderWebGL() {
      const gl = this.gl, model = this.model, cells = model.active.cells;
      const cellData = new Float32Array(cells.length * 4);
      const colorData = new Float32Array(cells.length * 4);
      for (let i = 0; i < cells.length; i++) {
        const cell = cells[i], x = i % model.columns, y = Math.floor(i / model.columns);
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        cellData.set([x, y, 0, 0], i * 4);
        colorData.set(color(cursor ? cell.fg : cell.bg), i * 4);
      }
      gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
      gl.useProgram(this.backgroundProgram);
      gl.uniform2f(gl.getUniformLocation(this.backgroundProgram, "uGrid"),
        model.columns, model.rows);
      this.attribute(this.backgroundBuffers.corner, this.backgroundProgram,
        "aCorner", 2, 0, null);
      this.attribute(this.backgroundBuffers.cell, this.backgroundProgram,
        "aCell", 4, 1, cellData);
      this.attribute(this.backgroundBuffers.color, this.backgroundProgram,
        "aColor", 4, 1, colorData);
      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, cells.length);

      gl.useProgram(this.glyphProgram);
      gl.bindVertexArray(this.glyphVao);
      const matrixLocation = gl.getUniformLocation(this.glyphProgram, "uCell");
      const colorLocation = gl.getUniformLocation(this.glyphProgram, "uColor");
      const scaleX = 2 / model.columns, scaleY = 2 / model.rows;
      for (let i = 0; i < cells.length; i++) {
        const cell = cells[i], code = cell.code, x = i % model.columns;
        const y = Math.floor(i / model.columns);
        if (code === 32) continue;
        const glyph = lookupCmap(this.glf, code);
        const range = glyph == null ? null : this.glf.lookup[glyph];
        if (!range || range.len === 0) continue;
        const matrix = new Float32Array([
          scaleX * FONT_SCALE, 0, 0, 0,
          0, scaleY * FONT_SCALE, 0, 0,
          0, 0, 1, 0,
          -1 + scaleX * (x - FONT_XMIN * FONT_SCALE),
          1 - scaleY * (y + 1) - scaleY * FONT_YMIN * FONT_SCALE,
          0, 1,
        ]);
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        gl.uniformMatrix4fv(matrixLocation, false, matrix);
        gl.uniform4fv(colorLocation, color(cursor ? cell.bg : cell.fg));
        gl.drawElements(gl.TRIANGLES, range.len, gl.UNSIGNED_INT, range.start * 4);
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
      const ratio = typeof devicePixelRatio === "number" ? devicePixelRatio : 1;
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.font = `${this.cellHeight - 3}px monospace`; ctx.textBaseline = "top";
      for (let i = 0; i < model.active.cells.length; i++) {
        const cell = model.active.cells[i], x = i % model.columns;
        const y = Math.floor(i / model.columns);
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        ctx.fillStyle = cursor ? cell.fg : cell.bg;
        ctx.fillRect(x * this.cellWidth, y * this.cellHeight,
          this.cellWidth, this.cellHeight);
        ctx.fillStyle = cursor ? cell.bg : cell.fg;
        ctx.fillText(String.fromCodePoint(cell.code), x * this.cellWidth,
          y * this.cellHeight + 1);
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
