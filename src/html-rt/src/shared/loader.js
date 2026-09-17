"use strict";

var g = {
  is_staging: true,
  /* tar_hash serves as the virtual file system backing store.
   * After boot(), it maps flat paths (e.g. "worker.js", "wast/address.wast")
   * to Blobs.  In staging mode it stays empty — files are fetched directly.
   * This will be extended for the Bash VFS (writable overlay, directory
   * listing, path resolution) once execve and coreutils land. */
  tar_hash: {},
  manifest_chunks: [],
};

/* Load a binary file (wasm, etc) — returns ArrayBuffer */
async function loadBinary(filename) {
  if (g.is_staging) return (await fetch(filename)).arrayBuffer();
  return g.tar_hash[filename].arrayBuffer();
}

/* Load a text file (json, wast, js) — returns string */
async function loadText(filename) {
  if (g.is_staging) return (await fetch(filename)).text();
  return g.tar_hash[filename].text();
}

/* Load and parse a JSON file — returns object */
async function loadJSON(filename) {
  if (g.is_staging) return (await fetch(filename)).json();
  const text = await g.tar_hash[filename].text();
  return JSON.parse(text);
}

/* Create a Web Worker — file URL in staging, Blob URL from tar in production */
async function createWorker(filename) {
  if (g.is_staging) return new Worker(filename);
  var src = await g.tar_hash[filename].text();
  var url = URL.createObjectURL(new Blob([src], {type: "text/javascript"}));
  var w = new Worker(url);
  URL.revokeObjectURL(url);
  return w;
}

/* Decompress gzip blob using DecompressionStream (zlibaux.wasm fallback) */
async function decompressGzip(blob) {
  if (typeof DecompressionStream !== "undefined") {
    try {
      var ds = new DecompressionStream("gzip");
      var decompressedStream = blob.stream().pipeThrough(ds);
      return await new Response(decompressedStream).blob();
    } catch (e) { /* fall through to zlibaux */ }
  }
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
  var memory = new WebAssembly.Memory({initial: 256});

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
    memory: memory,
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

  var result = await WebAssembly.instantiate(zlibBytes, imports);
  result.instance.exports.zlibaux_wasm_main();
  return new Blob(outputChunks);
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

  progress("Fetching data\u2026");

  /* Fetch and concatenate base64 data URI chunks */
  var buffers = await Promise.all(
    MANIFEST_URLS.map(function(url) { return fetch(url).then(function(r) { return r.arrayBuffer(); }); })
  );
  var totalBytes = 0;
  for (var i = 0; i < buffers.length; i++) totalBytes += buffers[i].byteLength;
  var compressed = new Blob(buffers);
  progress("Decompressing\u2026", (totalBytes / 1024) | 0);

  /* Decompress gzip */
  var decompressed = await decompressGzip(compressed);
  var decompressedSize = decompressed.size;
  progress("Unpacking\u2026", (decompressedSize / 1024) | 0);

  /* Untar via tarballjs — populates the VFS backing store */
  var tar = new tarball.TarReader();
  var entries = await tar.readFile(decompressed);
  var fileCount = 0;
  for (var j = 0; j < entries.length; j++) {
    var name = entries[j].name.replace(/^\.\//, "");
    if (!name) continue;
    g.tar_hash[name] = tar.getFileBlob(entries[j].name);
    fileCount++;
  }
  progress("Loaded " + fileCount + " files", (decompressedSize / 1024) | 0);

  await callback();
}
