"use strict";
/* Production tarballjs/loader boundary, including the ustar path-prefix field. */
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const zlib = require("node:zlib");
function member(name, content = Buffer.from("data"), prefix = "", type = "0") {
  const header = Buffer.alloc(512);
  header.write(name, 0, 100);
  for (const [offset, value] of [[100,"0000644"], [108,"0000000"], [116,"0000000"],
    [124,content.length.toString(8).padStart(11,"0")], [136,"00000000000"]]) header.write(value, offset);
  header.fill(32, 148, 156);
  header[156] = type.charCodeAt(0);
  header.write("ustar", 257); header.write("00", 263); header.write(prefix, 345, 155);
  header.write(header.reduce((sum,b)=>sum+b,0).toString(8).padStart(6,"0")+"\0 ",148);
  return Buffer.concat([header, content, Buffer.alloc((512-content.length%512)%512)]);
}
function context(archive) {
  const zlibaux = fs.readFileSync("submodules/zlib-wasm/zlibaux.wasm");
  const ctx = vm.createContext({Uint8Array, TextDecoder, TextEncoder, Blob,
    DecompressionStream, Response,
    MANIFEST_URLS:["package"], ZLIBAUX_URL:"zlib",
    fetch:async url=>({ok:true,arrayBuffer:async ()=>url === "zlib" ? zlibaux : zlib.gzipSync(archive)}),
    setTimeout, clearTimeout});
  vm.runInContext(fs.readFileSync("submodules/tarballjs/tarball.js","utf8"),ctx);
  vm.runInContext(fs.readFileSync("src/html-rt/src/loader.js","utf8"),ctx);
  ctx.g.is_staging = false;
  return ctx;
}
const archive = (...members) => Buffer.concat([...members, Buffer.alloc(1024)]);
(async () => {
  const fullName = "n".repeat(100), prefix = "directory/" + "p".repeat(100), leaf = "sample";
  const ctx = context(archive(member(fullName), member(leaf, Buffer.from("long"),prefix), member("__proto__")));
  await ctx.boot(()=>{});
  assert.equal(Buffer.from(await ctx.loadBinary(fullName)).toString(),"data");
  assert.equal(Buffer.from(await ctx.loadBinary(prefix+"/"+leaf)).toString(),"long");
  assert.equal(Buffer.from(await ctx.loadBinary("__proto__")).toString(),"data");
  assert.equal(Object.getPrototypeOf(ctx.g.tar_hash),null);
  const corrupt = member("sample"); corrupt[0] ^= 1;
  for (const bytes of [archive(corrupt), archive(member("../escape")),
    archive(member("/absolute")), archive(member("sample"),member("./sample")),
    archive(member("sample",Buffer.alloc(0),"","2")), member("sample").subarray(0,513)])
    await assert.rejects(context(bytes).boot(()=>{}));
  console.log("PASS tarballjs loader: full-width names, ustar prefixes, bytes, prototype-safe map, checksums, truncation, unsafe/duplicate paths and unsupported types");
})().catch(error=>{console.error(error);process.exitCode=1;});
