"use strict";

/* Browser adapter for /root/waste/tests/render/terminal.wast.
 * The guest writes the returned report through its own VFS descriptors. */
globalThis.WasteTerminalRenderTest = function (liveRenderer) {
  const checks = [];
  const report = {schema: 1, date: new Date().toISOString(),
    userAgent: navigator.userAgent, devicePixelRatio,
    checks};
  const check = (name, pass) => { checks.push({name, pass: Boolean(pass)}); };
  let textures = 0;
  let prototype, createTexture;
  const ratioDescriptor = Object.getOwnPropertyDescriptor(window, "devicePixelRatio");
  const originalRatio = devicePixelRatio;
  let renderer;
  const screen = document.createElement("div");
  screen.style.cssText = "position:fixed;left:-10000px;top:0;width:640px;height:600px;padding:0";
  const canvas = document.createElement("canvas");
  canvas.style.display = "block";
  screen.appendChild(canvas);
  document.body.appendChild(screen);
  try {
    check("Production terminal uses WebGL", liveRenderer.useWebGL);
    prototype = WebGL2RenderingContext.prototype;
    createTexture = prototype.createTexture;
    prototype.createTexture = function (...args) {
      textures++;
      return createTexture.apply(this, args);
    };
    const model = new WasteTerminalModel(80, 24);
    model.write("Il1| O0 []{}() /\\\r\n");
    const ascii = Array.from({length: 94}, (_, i) => String.fromCharCode(33 + i));
    model.write(ascii.slice(0, 64).join("") + "\r\n" + ascii.slice(64).join(""));
    model.write("\x1b[5;1H\x1b[38;2;0;255;0mGREEN\x1b[0m");
    model.write("\x1b[6;1H\x1b[48;2;255;0;0m \x1b[48;2;0;0;255m \x1b[0m");
    model.write("\x1b[7;1H\x1b[38;2;255;255;255m");
    // Cursor colors belong to its cell, not the current SGR state.
    model.active.cells[6 * 80].fg = "#ffffff";
    const before = JSON.stringify(model.snapshot());
    renderer = new WasteTerminalRenderer(canvas, model, {observe: false});
    renderer.resizeObserver?.disconnect();
    if (!renderer.useWebGL) throw new Error("WebGL2 renderer unavailable (fallback is not a pass)");
    const gl = renderer.gl;
    report.webgl = {version: gl.getParameter(gl.VERSION), renderer: gl.getParameter(gl.RENDERER)};
    for (const [width, ratio] of [[640, 1], [800, 1], [640, 2]]) {
      const label = `${width}px / DPR ${ratio}`;
      Object.defineProperty(window, "devicePixelRatio", {configurable: true, value: ratio});
      screen.style.width = `${width}px`;
      renderer.resize();
      // Read in the same task as drawing; the default framebuffer may be
      // discarded after compositing on contexts without preserveDrawingBuffer.
      const pixels = new Uint8Array(canvas.width * canvas.height * 4);
      gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      const cell = (x, y) => {
        const values = [];
        for (let dy = 0; dy < renderer.cellHeight; dy++) {
          for (let dx = 0; dx < renderer.cellWidth; dx++) {
            const offset = ((canvas.height - 1 - y * renderer.cellHeight - dy) *
              canvas.width + x * renderer.cellWidth + dx) * 4;
            values.push(Array.from(pixels.subarray(offset, offset + 3)));
          }
        }
        return values;
      };
      const lit = rgb => Math.max(...rgb) > 32;
      const mask = (x, y) => cell(x, y).map(rgb => lit(rgb) ? "1" : "0").join("");
      check(`${label}: WebGL has no error`, gl.getError() === gl.NO_ERROR && !gl.isContextLost());
      check(`${label}: blank cells remain black`, [0, 20, 79].every(x => cell(x, 10).every(rgb => !lit(rgb))));
      const glyphs = [0, 1, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 15, 16];
      check(`${label}: visible glyphs`, glyphs.every(x => cell(x, 0).some(lit)));
      check(`${label}: printable ASCII`, ascii.every((_, i) => cell(i < 64 ? i : i - 64, i < 64 ? 1 : 2).some(lit)));
      check(`${label}: I/l/1/bar have different shapes`, new Set([0, 1, 2, 3].map(x => mask(x, 0))).size === 4);
      check(`${label}: O/zero have different shapes`, mask(5, 0) !== mask(6, 0));
      check(`${label}: colored foreground`, cell(0, 4).some(([r, g, b]) => g > 128 && r < 8 && b < 8));
      check(`${label}: red/blue backgrounds`, cell(0, 5).every(([r, g, b]) => r > 247 && g < 8 && b < 8) &&
        cell(1, 5).every(([r, g, b]) => b > 247 && r < 8 && g < 8));
      check(`${label}: cursor`, cell(0, 6).every(rgb => rgb.every(v => v > 247)));
      check(`${label}: grid and contents unchanged`, model.columns === 80 && model.rows === 24 &&
        JSON.stringify(model.snapshot()) === before);
      check(`${label}: backing dimensions`, canvas.width === renderer.cellWidth * 80 &&
        canvas.height === renderer.cellHeight * 24 && renderer.ratio === ratio &&
        canvas.width === width * ratio);
    }
    check("No glyph textures created", textures === 0);

  } catch (error) {
    check(String(error.stack || error), false);
  } finally {
    if (prototype && createTexture) prototype.createTexture = createTexture;
    if (ratioDescriptor) Object.defineProperty(window, "devicePixelRatio", ratioDescriptor);
    else delete window.devicePixelRatio;
    renderer?.gl?.getExtension("WEBGL_lose_context")?.loseContext();
    screen.remove();
  }
  report.pixelChecksPassed = checks.length > 0 && checks.every(c => c.pass);
  report.restoredDevicePixelRatio = devicePixelRatio === originalRatio;
  report.ok = report.pixelChecksPassed && report.restoredDevicePixelRatio;
  return report;
};
