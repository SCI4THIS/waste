// Consume a distribution exactly as a separate application would.
import assert from "node:assert/strict";
import {readFile, readdir} from "node:fs/promises";
import {createHash} from "node:crypto";
import path from "node:path";
import {pathToFileURL} from "node:url";
const directory = path.resolve(process.argv[2] || "build/html-rt/embedding");
const manifest = JSON.parse(await readFile(path.join(directory, "manifest.json")));
const {createWaste} = await import(pathToFileURL(path.join(directory, "waste.mjs")));
const files = {};
for (const record of [...manifest.resources, ...manifest.artifacts]) {
  const bytes = await readFile(path.join(directory, record.path));
  assert.equal(bytes.length, record.bytes);
  assert.equal(createHash("sha256").update(bytes).digest("hex"), record.sha256);
  if (record.guestPath) files[record.guestPath] = {bytes, mode: record.mode};
}
assert(files["/lib/libc.so.wasm"]);
assert(!Object.keys(files).some(name => name.startsWith("/root/test") || name.startsWith("/root/app")));
const listedFiles = [];
async function visit(directory_, prefix = "") {
  for (const entry of await readdir(directory_, {withFileTypes: true})) {
    const relative = prefix + entry.name;
    if (entry.isDirectory()) await visit(path.join(directory_, entry.name), relative + "/");
    else listedFiles.push(relative);
  }
}
await visit(path.join(directory, "resources"));
assert.deepEqual(listedFiles.sort(), manifest.resources.map(record => record.path.slice("resources/".length)).sort());
const runtime = await createWaste({engine: await readFile(path.join(directory, "wasm.wasm")), files});
const examples = {
  awk: {executable: "/usr/bin/awk", args: ["{print $1}"], stdin: "one two\nthree four\n"},
  m4: {executable: "/usr/bin/m4", stdin: "include(`/work/include')", files: {"/work/include": "included\n"}},
  dot: {executable: "/usr/bin/dot", args: ["-Tsvg", "-o", "/work/graph.svg"], stdin: "digraph { a -> b }", outputPaths: ["/work/graph.svg"]},
  bash: {executable: "/usr/bin/bash", args: ["-c", "printf shell"]}
};
const expected = {awk: "one\nthree\n", m4: "included\n", bash: "shell"};
for (const name of manifest.tools) {
  const result = await runtime.run(examples[name]);
  assert.equal(result.outcome, "exited", result.message);
  assert.equal(result.exitCode, 0, new TextDecoder().decode(result.stderr));
  if (name === "dot") assert.match(new TextDecoder().decode(result.outputFiles["/work/graph.svg"]), /<svg[\s>]/);
  else assert.equal(new TextDecoder().decode(result.stdout), expected[name]);
}
runtime.dispose();
console.log(`embedding package consumer: ${manifest.tools.join(", ")}, selected resource closure and artifact digests passed`);
