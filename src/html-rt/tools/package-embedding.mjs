// Package the current installed VFS. This command never builds or installs guests.
import {readFile, writeFile, mkdir, realpath, stat, rm} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";
import {createHash} from "node:crypto";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const arguments_ = Object.fromEntries(process.argv.slice(2).map(value => {
  const match = /^--(build|output|tools|with-shell|release)=(.+)$/.exec(value);
  if (!match) throw Error(`Unknown package argument: ${value}`);
  return [match[1], match[2]];
}));
const build = path.resolve(arguments_.build || path.join(root, "build/html-rt"));
const output = path.resolve(arguments_.output || path.join(build, "embedding"));
const releaseVersion = arguments_.release || "development";
if (!/^[A-Za-z0-9.+_-]{1,64}$/.test(releaseVersion)) throw Error("Invalid artifact release version");
await mkdir(output, {recursive: true});
if (!(await realpath(output)).startsWith(await realpath(path.join(root, "build")) + path.sep))
  throw Error("Embedding output must be under the repository build directory");
// This subtree is generated, including when a repeat invocation selects fewer
// tools. Do not accidentally ship resources left by the previous selection.
await rm(path.join(output, "resources"), {recursive: true, force: true});
await rm(path.join(output, "waste.worker.bundle.mjs"), {force: true});
const profiles = JSON.parse(await readFile(path.join(root, "src/html-rt/command/packages.json"), "utf8"));
const tools = [...new Set((arguments_.tools || "awk,m4,dot").split(","))];
if (arguments_["with-shell"] === "1" && !tools.includes("bash")) tools.push("bash");
if (tools.some(name => !Object.hasOwn(profiles.tools, name))) throw Error("Unknown command profile");
const selected = [...new Set([...profiles.common, ...tools.flatMap(name => profiles.tools[name].files)])].sort();
const resources = {}, records = [], artifacts = [];
const digest = bytes => createHash("sha256").update(bytes).digest("hex");
async function publish(relative, bytes, mode = 0o644) {
  const target = path.join(output, relative);
  await mkdir(path.dirname(target), {recursive: true});
  await writeFile(target, bytes, {mode});
  return {path: relative, bytes: bytes.length, sha256: digest(bytes)};
}
for (const guestPath of selected) {
  const source = path.join(root, "src/vfs", guestPath);
  // The profiles select ordinary files. Never silently follow a source symlink
  // out of the mounted tree or infer an arbitrary host dependency.
  const resolved = await realpath(source);
  if (!resolved.startsWith(path.join(root, "src/vfs") + path.sep)) throw Error(`External resource: ${guestPath}`);
  const bytes = await readFile(source), mode = (await stat(source)).mode & 0o777;
  const record = {...await publish(`resources${guestPath}`, bytes, mode), guestPath, mode};
  resources[guestPath] = {bytes: bytes.toString("base64"), mode};
  if (bytes.subarray(0, 8).equals(Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]))) {
    const module = new WebAssembly.Module(bytes);
    record.imports = WebAssembly.Module.imports(module);
    const providers = new Set(["env", "GOT.func", "GOT.mem", "waste_kernel", "libc"]);
    for (const entry of record.imports)
      if (!providers.has(entry.module)) throw Error(`Undeclared provider ${entry.module} in ${guestPath}`);
  }
  records.push(record);
}
const engine = await readFile(path.join(build, "wasm.wasm"));
const engineModule = new WebAssembly.Module(engine);
const {exports: abi} = new WebAssembly.Instance(engineModule, {
  waste_command_host: {monotonic_ns: () => 0n, realtime_ns: () => 0n, random: () => -38}
});
const host = await readFile(path.join(root, "src/html-rt/command/waste.mjs"), "utf8");
const worker = await readFile(path.join(root, "src/html-rt/command/waste.worker.mjs"), "utf8");
const workerImport = 'import {createWaste} from "./waste.mjs";';
if (!worker.startsWith(workerImport)) throw Error("Worker bundle import changed; update the bundler");
const hostBoundary = host.indexOf("// Worker jobs use separate engine instances");
if (hostBoundary < 0) throw Error("Worker host boundary changed; update the bundler");
const workerBundle = (host.slice(0, hostBoundary) + "\n" + worker.slice(workerImport.length)).replace(/^export /gm, "");
for (const [name, bytes] of [["wasm.wasm", engine], ["waste.mjs", Buffer.from(host)],
  ["waste.worker.mjs", Buffer.from(worker)], ["waste.worker.bundle.js", Buffer.from(workerBundle)],
  ["LICENSE", await readFile(path.join(root, "LICENSE"))],
  ["include/waste.h", await readFile(path.join(root, "src/engine/include/waste.h"))],
  ["examples/embedding.c", await readFile(path.join(root, "src/cli-rt/examples/embedding.c"))],
  ["embedding.md", await readFile(path.join(root, "docs/embedding.md"))]])
  artifacts.push(await publish(name, bytes));
const sdk = JSON.parse(await readFile(path.join(root, "src/vfs/usr/share/waste/sdk.json")));
const version = abi.waste_command_version();
const metadata = {
  format: 1, releaseVersion, profile: "binary-command", hostABI: {major: version >>> 16, minor: version & 0xffff,
    features: abi.waste_command_features(), optionsBytes: abi.waste_command_options_size(),
    resultBytes: abi.waste_command_result_size(), fileInfoBytes: abi.waste_command_file_info_size()},
  guestABI: {target: "wasm32", profile: "WASTE PIC/shared-libc", layouts: sdk.abi,
    providers: {libc: "/lib/libc.so.wasm", waste_kernel: "engine", env: "process startup"}},
  tools, resources: records, engineImports: WebAssembly.Module.imports(engineModule),
  sourcePackages: tools.map(name => ({tool: name, command: `make -C src/aux ${profiles.tools[name].sourceTarget}`,
    archive: `build/aux/${name}/${name}-corresponding-source.tar.gz`})), artifacts
};
const template = await readFile(path.join(root, "src/html-rt/command/example.html"), "utf8");
const safeJSON = value => JSON.stringify(value).replaceAll("<", "\\u003c");
const html = template.replace("/* WASTE_HOST */", host)
  .replace("/* WASTE_ASSETS */", `const embedded = ${safeJSON({engine: engine.toString("base64"), resources, tools, workerSource: workerBundle})};`);
artifacts.push(await publish("embedding.html", Buffer.from(html)));
artifacts.push(await publish("README.md", Buffer.from(`WASTE command package\n\nSelected commands: ${tools.join(", ")}.\nOpen embedding.html directly with file://. No server is required.\nSee embedding.md for the C, JS and wire contracts; manifest.json records current asset digests and providers.\n\nPreserve LICENSE and resources/usr/share/licenses when redistributing.\nFor binary releases, generate and distribute the corresponding-source archives listed in manifest.json.\nThese explicit source-package commands are separate from packaging and never run implicitly.\n\n`)));
await publish("manifest.json", Buffer.from(JSON.stringify(metadata, null, 2) + "\n"));
console.log(`Embedding package: ${output} (${tools.join(", ")}; ${records.length} guest resources)`);
