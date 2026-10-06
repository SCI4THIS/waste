"use strict";

const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const wasmPath = path.join(root, "build/html-rt/waste-libc/waste-libc.wasm");
const bytes = fs.readFileSync(wasmPath);
const imports = Object.create(null);
let importCalls = 0;
// The merged artifact imports POSIX functions unrelated to allocation. Fail
// immediately if the allocator reaches one; this supplies no kernel behavior.
for (const entry of WebAssembly.Module.imports(new WebAssembly.Module(bytes))) {
  if (entry.kind !== "function" || !["env", "waste_kernel"].includes(entry.module))
    throw new Error(`unexpected allocator artifact import ${entry.module}:${entry.name} (${entry.kind})`);
  imports[entry.module] ??= Object.create(null);
  imports[entry.module][entry.name] = () => {
    importCalls++;
    throw new Error(`allocator stress called kernel import ${entry.module}:${entry.name}`);
  };
}

WebAssembly.instantiate(bytes, imports).then(({instance}) => {
  const api = instance.exports;
  if (api.waste_allocator_init(235120) !== 1) throw new Error("allocator initialization failed");
  let memory = new Uint8Array(api.memory.buffer);
  const live = [];

  for (let index = 1; index <= 5000; index++) {
    const size = (index * 37) % 399 + 2;
    const pointer = api.malloc(size);
    if (!pointer || (pointer & 15)) throw new Error(`invalid pointer ${pointer}`);
    if (memory.buffer !== api.memory.buffer) memory = new Uint8Array(api.memory.buffer);
    for (const allocation of live) {
      if (pointer < allocation.pointer + allocation.size && allocation.pointer < pointer + size)
        throw new Error(`allocation ${index} overlaps allocation ${allocation.index}`);
      if (memory[allocation.pointer] !== allocation.first ||
          memory[allocation.pointer + allocation.size - 1] !== allocation.last)
        throw new Error(`allocation ${allocation.index} was corrupted`);
    }
    memory[pointer] = index & 255;
    memory[pointer + size - 1] = (index * 3) & 255;
    live.push({
      index,
      pointer,
      size,
      first: index & 255,
      last: (index * 3) & 255
    });
    if (index % 3 === 0) api.free(live.shift().pointer);
  }

  for (const allocation of live) {
    if (memory[allocation.pointer] !== allocation.first ||
        memory[allocation.pointer + allocation.size - 1] !== allocation.last)
      throw new Error(`allocation ${allocation.index} was corrupted at completion`);
    api.free(allocation.pointer);
  }
  if (importCalls !== 0) throw new Error(`allocator made ${importCalls} kernel calls`);
  // A real wrapper must hit the guard, rather than accepting a constant stub.
  let guarded = false;
  try { api.isatty(0); }
  catch (error) { guarded = error.message === "allocator stress called kernel import waste_kernel:isatty_v1"; }
  if (!guarded || importCalls !== 1) throw new Error("allocator import guard did not reject a kernel call");
  console.log(
    `native allocator stress: pass (${api.waste_memory_pages()} pages, ` +
    `${api.waste_memory_grow_calls()} one-page grows)`
  );
}).catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
