"use strict";

const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "../..");
const wasmPath = path.join(root, "build/waste-libc/waste-libc.wasm");

WebAssembly.instantiate(fs.readFileSync(wasmPath)).then(({instance}) => {
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
  console.log(
    `native allocator stress: pass (${api.waste_memory_pages()} pages, ` +
    `${api.waste_memory_grow_calls()} one-page grows)`
  );
}).catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
