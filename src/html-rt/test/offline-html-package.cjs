"use strict";

/* Read the package actually embedded in a production page, never staging
 * fallbacks. Browser fetch/decompression/DOM checks remain separate. */
const fs = require("node:fs");
const zlib = require("node:zlib");

function readOfflinePackage(filename) {
  const html = fs.readFileSync(filename, "utf8");
  const match = html.match(/var MANIFEST_URLS = \[([\s\S]*?)\];/);
  if (!match) throw new Error(`no embedded archive in ${filename}`);
  const urls = JSON.parse("[" + match[1] + "]");
  const compressed = Buffer.concat(urls.map(url => {
    const data = /^data:application\/octet-stream;base64,([A-Za-z0-9+/=]+)$/.exec(url);
    if (!data) throw new Error("offline archive has an external/invalid URL");
    return Buffer.from(data[1], "base64");
  }));
  const tar = zlib.gunzipSync(compressed);
  const files = new Map();
  const directories = new Set();
  const text = (offset, length) => tar.subarray(offset, offset + length)
    .toString("utf8").replace(/\0.*$/s, "");
  for (let offset = 0; offset + 512 <= tar.length;) {
    if (tar.subarray(offset, offset + 512).every(byte => byte === 0)) break;
    const prefix = text(offset + 345, 155);
    const name = ((prefix ? prefix + "/" : "") + text(offset, 100)).replace(/^\.\//, "");
    const size = parseInt(text(offset + 124, 12).trim(), 8);
    let checksum = 0;
    for (let i = 0; i < 512; i++) checksum += i >= 148 && i < 156 ? 32 : tar[offset + i];
    if (checksum !== parseInt(text(offset + 148, 8).trim(), 8) ||
        !Number.isSafeInteger(size) || size < 0 || offset + 512 + size > tar.length)
      throw new Error("invalid/truncated tar member");
    if (name.startsWith("/") || name.split("/").includes(".."))
      throw new Error(`unsafe tar path: ${name}`);
    const type = tar[offset + 156];
    if (type === 0 || type === 48) {
      if (files.has(name)) throw new Error(`duplicate tar file: ${name}`);
      files.set(name, tar.subarray(offset + 512, offset + 512 + size));
    } else if (type === 53) {
      if (name) directories.add(name.replace(/\/$/, ""));
    } else {
      throw new Error(`unsupported tar member type for ${name}`);
    }
    offset += 512 + Math.ceil(size / 512) * 512;
  }
  return {html, files, directories, read(name) {
    const bytes = files.get(name);
    if (!bytes) throw new Error(`offline package missing ${name}`);
    return bytes;
  }};
}

module.exports = {readOfflinePackage};
