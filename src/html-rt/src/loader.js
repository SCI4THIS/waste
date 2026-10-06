"use strict";

var g = {
  is_staging: true,
  /* Authored frontend and generated data have distinct staging roots.
   * Archive member names stay unchanged in the offline page. */
  staging_data_root: "../../../build/html-rt/",
  /* Extracted package files are boot inputs; writable guest state belongs
   * to each C engine instance. */
  tar_hash: Object.create(null),
  tar_meta: Object.create(null),
  manifest_chunks: [],
  zlibauxModule: null,
};

/* Load a binary file (wasm, etc) — returns ArrayBuffer */
async function loadBinary(filename) {
  if (g.is_staging) return (await fetch(stagingDataPath(filename))).arrayBuffer();
  return g.tar_hash[filename].arrayBuffer();
}

/* Load a text file (json, wast, js) — returns string */
async function loadText(filename) {
  if (g.is_staging) return (await fetch(stagingDataPath(filename))).text();
  return g.tar_hash[filename].text();
}

/* Load and parse a JSON file — returns object */
async function loadJSON(filename) {
  if (g.is_staging) return (await fetch(stagingDataPath(filename))).json();
  const text = await g.tar_hash[filename].text();
  return JSON.parse(text);
}

/* Reuse the tarballjs file map and the installed inventory. No second copy
 * of the guest tree is embedded in a custom filesystem bundle. */
var installedVfsLoading = null;
function loadInstalledVfs() {
  if (!installedVfsLoading) installedVfsLoading = (async () => {
    const inventory = await loadBinary("vfs-manifest.json");
    const manifest = JSON.parse(new TextDecoder().decode(inventory));
    const files = [];
    for (const [index, entry] of manifest.entries.entries()) {
      if (entry.role === "directory" || entry.role === "interpreter") continue;
      if (!entry.path.startsWith("/") || entry.path.split("/").includes(".."))
        throw new Error("Unsafe installed VFS path");
      const name = entry.path.slice(1);
      const bytes = g.is_staging ? await (await fetch("../../vfs/" + name)).arrayBuffer() :
        await loadBinary(name);
      files.push({index, bytes});
    }
    return {inventory, files};
  })().catch(error => { installedVfsLoading = null; throw error; });
  return installedVfsLoading;
}

/* Create a Web Worker — file URL in staging, Blob URL from tar in production */
async function createWorker(filename) {
  if (g.is_staging) {
    const source = await loadText(filename);
    const config = "globalThis.WASTE_CONFIG = Object.freeze(" + JSON.stringify(WASTE_CONFIG) + ");\n";
    const url = URL.createObjectURL(new Blob([config, source], {type: "text/javascript"}));
    const worker = new Worker(url);
    URL.revokeObjectURL(url);
    return worker;
  }
  if (filename === "worker.js") filename = "root/waste/app/worker.js";
  var src = await g.tar_hash[filename].text();
  var url = URL.createObjectURL(new Blob([src], {type: "text/javascript"}));
  var w = new Worker(url);
  URL.revokeObjectURL(url);
  return w;
}

/* Production frontend files live in the compressed installed VFS. */
async function loadWebappFiles() {
  const style = document.createElement("style");
  style.textContent = await loadText("root/waste/app/style.css");
  document.head.appendChild(style);
  for (const filename of [
    "root/waste/app/terminal/model.js",
    "root/waste/app/terminal/glf.js",
    "root/waste/app/terminal/renderer.js",
    "root/waste/app/terminal/render-test.js",
    "root/waste/app/test-suite.js",
    "root/waste/app/app.js",
  ]) {
    const source = await loadText(filename);
    const url = URL.createObjectURL(new Blob([source], {type: "text/javascript"}));
    try {
      await new Promise((resolve, reject) => {
        const script = document.createElement("script");
        script.src = url;
        script.onload = resolve;
        script.onerror = () => reject(new Error("Unable to load " + filename));
        document.head.appendChild(script);
      });
    } finally {
      URL.revokeObjectURL(url);
    }
  }
}

function stagingDataPath(filename) {
  if (filename === "launch.wast") return g.staging_data_root + "bash-runtime.wast";
  if (filename === "payload.json") return g.staging_data_root + "tests/payload.json";
  if (filename === "waste-wast.wasm") return g.staging_data_root + filename;
  if (filename === "vfs-manifest.json") return "../../vfs/.inventory.json";
  if (filename === "browser-corpus-expected-failures.txt") return "../../../tests/" + filename;
  return filename;
}

/* Always use the bundled zlib-wasm implementation for the offline package. */
async function decompressGzip(blob) {
  return zlibaux_decompress(blob);
}

/* zlibaux.wasm-based gzip decompression (fallback) */
async function zlibaux_decompress(blob) {
  var compressed = new Uint8Array(await blob.arrayBuffer());
  var zlibUrl = (typeof ZLIBAUX_URL !== "undefined") ? ZLIBAUX_URL : null;
  if (!zlibUrl) throw new Error("no decompressor available");
  var zlibBytes = await fetch(zlibUrl).then(function(r) { return r.arrayBuffer(); });

  var memoryHead = 0;
  var inputOffset = 0;
  var outputChunks = [];
  var memory = null;

  function wasmalloc(size) {
    size = (size + 15) & ~15;
    var pages = memory.buffer.byteLength >>> 16;
    var needed = ((memoryHead + size + 65535) >>> 16);
    if (needed > pages) memory.grow(needed - pages);
    var ptr = memoryHead;
    memoryHead += size;
    return ptr;
  }

  var imports = {env: {
    input_chunk: function(buf, buflen) {
      var remaining = compressed.length - inputOffset;
      if (remaining <= 0) return 0;
      var n = Math.min(remaining, buflen);
      new Uint8Array(memory.buffer, buf, n).set(
        compressed.subarray(inputOffset, inputOffset + n));
      inputOffset += n;
      return n;
    },
    output_chunk: function(buf, buflen) {
      outputChunks.push(new Uint8Array(memory.buffer, buf, buflen).slice());
    },
    wasmalloc: wasmalloc,
  }};

  var result = await WebAssembly.instantiate(g.zlibauxModule || zlibBytes, imports);
  var instance = result.instance || result;
  memory = instance.exports.memory;
  memoryHead = instance.exports.__heap_base.value;
  instance.exports.zlibaux_wasm_main();
  return new Blob(outputChunks);
}

/* Keep tarballjs extraction, supplying ustar prefix handling and bounded
 * header checks that its small upstream reader does not expose. */
function packageTarReader() {
  return new (class extends tarball.TarReader {
    _readString(offset, size) {
      const bytes = new Uint8Array(this.buffer, offset, size);
      const end = bytes.indexOf(0);
      return new TextDecoder("utf-8", {fatal: true}).decode(bytes.subarray(0, end < 0 ? size : end));
    }
    _readFileName(offset) {
      const name = this._readString(offset, 100);
      const prefix = this._readString(offset + 345, 155);
      return prefix ? prefix + "/" + name : name;
    }
    _readFileSize(offset) {
      const number = (at, n) => {
        const value = this._readString(at, n).trim();
        if (!/^[0-7]+$/.test(value)) throw new Error("Invalid tar number");
        return parseInt(value, 8);
      };
      const size = number(offset + 124, 12);
      if (!Number.isSafeInteger(size) || offset + 512 + Math.ceil(size / 512) * 512 > this.buffer.byteLength)
        throw new Error("Truncated tar file");
      const header = new Uint8Array(this.buffer, offset, 512);
      let checksum = 0;
      for (let i = 0; i < 512; i++) checksum += i >= 148 && i < 156 ? 32 : header[i];
      if (checksum !== number(offset + 148, 8)) throw new Error("Invalid tar checksum");
      return size;
    }
    _readFileType(offset) {
      const type = new Uint8Array(this.buffer, offset + 156, 1)[0];
      if (type === 0 || type === 48) return "file";
      if (type === 53) return "directory";
      throw new Error("Unsupported tar entry type");
    }
  })();
}

/* Boot: inflate the compressed tar archive, populate tar_hash (the VFS),
 * then call the application callback.  In staging mode, skip straight to
 * the callback — files are served directly by the dev server.
 *
 * onProgress(message, detail) is called at each stage so the page can
 * update a loading screen.  detail is optional (e.g. byte counts). */
async function boot(callback, onProgress) {
  function progress(message, detail) {
    if (onProgress) onProgress(message, detail);
  }

  if (g.is_staging) {
    progress("Loading\u2026");
    await callback();
    return;
  }

  progress("Loading zlib-wasm\u2026");
  var zlibUrl = (typeof ZLIBAUX_URL !== "undefined") ? ZLIBAUX_URL : null;
  if (!zlibUrl) throw new Error("no decompressor available");
  var zlibBytes = await fetch(zlibUrl).then(function(r) {
    if (!r.ok) throw new Error("unable to load zlib-wasm");
    return r.arrayBuffer();
  });
  g.zlibauxModule = await WebAssembly.compile(zlibBytes);

  progress("Loading compressed app\u2026");

  /* Fetch and concatenate base64 data URI chunks */
  var buffers = await Promise.all(
    MANIFEST_URLS.map(function(url) { return fetch(url).then(function(r) { return r.arrayBuffer(); }); })
  );
  var totalBytes = 0;
  for (var i = 0; i < buffers.length; i++) totalBytes += buffers[i].byteLength;
  var compressed = new Blob(buffers);
  progress("Decompressing app\u2026", (totalBytes / 1024) | 0);

  /* Decompress gzip */
  var decompressed = await decompressGzip(compressed);
  var decompressedSize = decompressed.size;
  progress("Unpacking\u2026", (decompressedSize / 1024) | 0);

  /* Untar via tarballjs — populates the VFS backing store */
  var tar = packageTarReader();
  var entries = tar.readArrayBuffer(await decompressed.arrayBuffer());
  var fileCount = 0;
  for (var j = 0; j < entries.length; j++) {
    var name = entries[j].name.replace(/^\.\//, "");
    if (entries[j].type === "directory") name = name.replace(/\/$/, "");
    if (!name || name === ".") continue;
    if (name.startsWith("/") || name.split("/").some(part => !part || part === "." || part === "..") ||
        /[\x00-\x1f\\]/.test(name) || Object.prototype.hasOwnProperty.call(g.tar_hash, name))
      throw new Error("Unsafe or duplicate tar path");
    g.tar_hash[name] = tar.getFileBlob(entries[j].name);
    g.tar_meta[name] = {type: entries[j].type, size: entries[j].size};
    fileCount++;
  }
  progress("Loaded " + fileCount + " files", (decompressedSize / 1024) | 0);

  progress("Starting Bash\u2026");
  await callback();
}
