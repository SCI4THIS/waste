// Private Wasm host boundary for the same C ownership probe as native.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
let exp;
const floating = (p, n) => Number(Buffer.from(new Uint8Array(exp.memory.buffer, p, n)).toString());
const {instance} = await WebAssembly.instantiate(
  fs.readFileSync(path.join(root, 'build/html-rt/direct-start-check.wasm')),
  {waste_host: {strtod: floating, strtof: floating}});
exp = instance.exports;
const allocate = bytes => {
  const p = exp.malloc(bytes.length);
  if (!p) throw Error('probe allocation failed');
  new Uint8Array(exp.memory.buffer, p, bytes.length).set(bytes);
  return p;
};
if (!exp.probe_init()) throw Error('probe initialization failed');
try {
  const stage = (guest, bytes) => {
    const p = allocate(Buffer.from(guest + '\0')), b = allocate(bytes);
    try {
      if (!exp.probe_stage(p, b, bytes.length)) throw Error(`cannot stage ${guest}`);
    } finally { exp.free(p); exp.free(b); }
  };
  for (const name of fs.readdirSync(path.join(root, 'src/system-tests/cli-runtime')).filter(n => n.endsWith('.wat'))) {
    stage('/tmp/' + name, fs.readFileSync(path.join(root, 'src/system-tests/cli-runtime', name)));
  }
  for (const guest of ['/usr/bin/bash', '/usr/bin/cat', '/usr/bin/echo', '/usr/lib/libc.so.wasm']) {
    stage(guest, fs.readFileSync(path.join(root, 'src/vfs', guest)));
  }
  const checks = exp.probe_run();
  if (!checks) {
    const bytes = new Uint8Array(exp.memory.buffer), p = exp.probe_error();
    let end = p;
    while (bytes[end]) end++;
    throw Error(new TextDecoder().decode(bytes.subarray(p, end)));
  }
  console.log(`browser C direct startup: ${checks} checks, PASS`);
} finally { exp.probe_destroy(); }
