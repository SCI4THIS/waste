"use strict";

/* WebGL2 glyph-grid renderer with a Canvas2D fallback.  The terminal model
 * remains authoritative; this class only turns its cells into pixels. */
(function (root) {
  class TerminalRenderer {
    constructor(canvas, model) {
      this.canvas = canvas;
      this.model = model;
      this.cellWidth = 10;
      this.cellHeight = 18;
      this.dirty = true;
      this.gl = null;
      this.ctx = null;
      this.program = null;
      this.atlas = null;
      this.buffers = null;
      this.useWebGL = false;
      this.resizeObserver = null;
      this.init();
    }

    init() {
      this.canvas.setAttribute("role", "log");
      this.canvas.setAttribute("aria-label", "WASTE Bash terminal");
      try { this.gl = this.canvas.getContext("webgl2", {antialias: false, alpha: false}); }
      catch (_) { this.gl = null; }
      if (this.gl) {
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
      const vertex = `#version 300 es
        in vec2 aCorner; in vec4 aCell; in vec4 aFg; in vec4 aBg;
        uniform vec2 uGrid;
        out vec2 vGlyph; out vec4 vFg; out vec4 vBg;
        void main() {
          vec2 p = (aCell.xy + aCorner) / uGrid;
          gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
          float glyph = aCell.z;
          vGlyph = (vec2(mod(glyph, 16.0), floor(glyph / 16.0)) + aCorner) / 16.0;
          vFg = aFg; vBg = aBg;
        }`;
      const fragment = `#version 300 es
        precision mediump float;
        uniform sampler2D uAtlas;
        in vec2 vGlyph; in vec4 vFg; in vec4 vBg;
        out vec4 outColor;
        void main() { float alpha = texture(uAtlas, vGlyph).r; outColor = mix(vBg, vFg, alpha); }`;
      this.program = this.link(vertex, fragment);
      this.buffers = {
        corner: gl.createBuffer(), cell: gl.createBuffer(), fg: gl.createBuffer(), bg: gl.createBuffer(),
      };
      gl.bindBuffer(gl.ARRAY_BUFFER, this.buffers.corner);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1]), gl.STATIC_DRAW);
      this.atlas = this.makeAtlas();
      gl.useProgram(this.program);
      gl.uniform1i(gl.getUniformLocation(this.program, "uAtlas"), 0);
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
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
      return program;
    }

    makeAtlas() {
      const gl = this.gl;
      const canvas = document.createElement("canvas");
      canvas.width = canvas.height = 256;
      const ctx = canvas.getContext("2d");
      ctx.fillStyle = "black"; ctx.fillRect(0, 0, 256, 256);
      ctx.fillStyle = "white"; ctx.font = "16px monospace"; ctx.textBaseline = "top";
      for (let code = 0; code < 256; code++) {
        const x = (code & 15) * 16, y = (code >> 4) * 16;
        ctx.fillText(String.fromCharCode(code >= 32 ? code : 32), x, y - 1);
      }
      const texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, gl.RED, gl.UNSIGNED_BYTE, canvas);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      return texture;
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
      const gl = this.gl, model = this.model;
      const cells = model.active.cells;
      const cellData = new Float32Array(cells.length * 4);
      const fgData = new Float32Array(cells.length * 4);
      const bgData = new Float32Array(cells.length * 4);
      for (let i = 0; i < cells.length; i++) {
        const cell = cells[i], x = i % model.columns, y = Math.floor(i / model.columns);
        const code = cell.code >= 32 && cell.code < 256 ? cell.code : 63;
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        cellData.set([x, y, code, 0], i * 4);
        fgData.set(cursor ? [0, 0, 0, 1] : color(cell.fg), i * 4);
        bgData.set(cursor ? color(cell.fg) : color(cell.bg), i * 4);
      }
      gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); gl.useProgram(this.program);
      gl.uniform2f(gl.getUniformLocation(this.program, "uGrid"), model.columns, model.rows);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.atlas);
      this.attribute(this.buffers.corner, "aCorner", 2, false, 0, 0);
      this.attribute(this.buffers.cell, "aCell", 4, true, 1, cellData);
      this.attribute(this.buffers.fg, "aFg", 4, true, 1, fgData);
      this.attribute(this.buffers.bg, "aBg", 4, true, 1, bgData);
      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, cells.length);
    }

    attribute(buffer, name, size, dynamic, divisor, data) {
      const gl = this.gl, location = gl.getAttribLocation(this.program, name);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      if (data) gl.bufferData(gl.ARRAY_BUFFER, data, dynamic ? gl.DYNAMIC_DRAW : gl.STATIC_DRAW);
      gl.enableVertexAttribArray(location); gl.vertexAttribPointer(location, size, gl.FLOAT, false, 0, 0);
      gl.vertexAttribDivisor(location, divisor);
    }

    renderCanvas() {
      if (!this.ctx) return;
      const ctx = this.ctx, model = this.model;
      const ratio = typeof devicePixelRatio === "number" ? devicePixelRatio : 1;
      ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
      ctx.font = `${this.cellHeight - 3}px monospace`; ctx.textBaseline = "top";
      for (let i = 0; i < model.active.cells.length; i++) {
        const cell = model.active.cells[i], x = i % model.columns, y = Math.floor(i / model.columns);
        const cursor = model.cursorVisible && x === model.x && y === model.y;
        ctx.fillStyle = cursor ? cell.fg : cell.bg;
        ctx.fillRect(x * this.cellWidth, y * this.cellHeight, this.cellWidth, this.cellHeight);
        ctx.fillStyle = cursor ? cell.bg : cell.fg;
        ctx.fillText(String.fromCodePoint(cell.code), x * this.cellWidth, y * this.cellHeight + 1);
      }
    }
  }

  function color(value) {
    const number = Number.parseInt(String(value).slice(1), 16);
    return [(number >> 16 & 255) / 255, (number >> 8 & 255) / 255, (number & 255) / 255, 1];
  }

  root.WasteTerminalRenderer = TerminalRenderer;
})(typeof globalThis !== "undefined" ? globalThis : self);
