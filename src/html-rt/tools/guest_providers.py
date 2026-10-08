"""Declaration/provider compatibility checks for the current guest profile.

Availability is distinct from semantic conformance. Shared guest-adapter
signatures and browser capabilities are described by the adapter policy;
compiled providers are inspected directly.
"""
import json
from pathlib import Path
import subprocess
import tempfile

import guest_sdk
import wasm_signatures


def inspect_sdk(repo, root, manifest, library=None):
    policy = guest_sdk.api_policy()
    review = policy["browser_adapter_review"]
    libc = root / "lib/libc.so.wasm"
    library = library or root / "lib/libncurses.so.wasm"
    if not libc.is_file():
        libc = repo / "src/vfs/lib/libc.so.wasm"
    if not library.is_file():
        library = repo / "src/vfs/lib/libncurses.so.wasm"
    providers = [("guest-libc", wasm_signatures.inspect(libc)["exports"]),
                 ("ncurses", wasm_signatures.inspect(library)["exports"]),
                 ("browser-env", review["signatures"])]
    headers = sorted(e["path"].removeprefix("/usr/include/") for e in manifest["headers"]
                     if e["role"] in ("sdk-public", "sdk-ncurses")
                     and e["path"] != "/usr/include/waste/ncurses/curses.h")
    command = ["clang", "--target=wasm32", "-std=c11", "-fno-builtin", "-Werror",
               "-Wno-deprecated-declarations",
               *guest_sdk.include_flags(root)]
    functions, variables = {}, {}
    for header in headers:
        ast = subprocess.run(command + ["-x", "c", "-Xclang", "-ast-dump=json", "-fsyntax-only", "-"],
                             input=f"#include <{header}>\n", text=True, capture_output=True, check=True)

        def walk(node):
            kind = node.get("kind")
            if kind in ("FunctionDecl", "VarDecl") and not node.get("isImplicit"):
                if kind == "FunctionDecl" or node.get("storageClass") == "extern":
                    name = node["name"]
                    unavailable = any(c.get("kind") == "UnavailableAttr" for c in node.get("inner", []))
                    inline = kind == "FunctionDecl" and node.get("storageClass") == "static" and any(
                        c.get("kind") == "CompoundStmt" for c in node.get("inner", []))
                    table = functions if kind == "FunctionDecl" else variables
                    entry = table.setdefault(name, dict(type=node["type"]["qualType"], headers=[],
                                                       unavailable=unavailable, inline=inline))
                    if entry["unavailable"] != unavailable:
                        raise ValueError(f"inconsistent availability: {name}")
                    if header not in entry["headers"]:
                        entry["headers"].append(header)
            for child in node.get("inner", []):
                walk(child)

        walk(json.loads(ast.stdout))
    unavailable = guest_sdk.unavailable_policy()
    stubs = {name: e["behavior"] for e in policy["compatibility_stubs"] for name in e["names"].split()}
    errors = []
    for name, entry in functions.items():
        category = next((category for header, category in (
            ("curses.h", "ncurses"), ("term.h", "ncurses"), ("unctrl.h", "ncurses"),
            ("locale.h", "locale"), ("langinfo.h", "locale"),
            ("wchar.h", "wide_characters"), ("wctype.h", "wide_characters"), ("uchar.h", "wide_characters"),
            ("stdio.h", "stdio"), ("stdio_ext.h", "stdio"), ("pthread.h", "threads"),
            ("netdb.h", "network"), ("sys/socket.h", "network"), ("arpa/inet.h", "network"),
            ("signal.h", "signals"),
            ("dlfcn.h", "dynamic_loading"), ("sys/mman.h", "mapping"),
            ("spawn.h", "processes"), ("sys/wait.h", "processes"),
            ("dirent.h", "filesystem"), ("sys/stat.h", "filesystem"), ("fcntl.h", "filesystem"))
            if header in entry["headers"]), "default")
        entry["capability_note"] = policy["capability_notes"][category]
        if entry["unavailable"]:
            if name not in unavailable:
                errors.append(f"unreviewed unavailable declaration: {name}")
            entry.update(provider="unavailable", reason=unavailable.get(name))
        elif entry["inline"]:
            entry["provider"] = "header-inline"
        else:
            matches = [(role, symbols[name]) for role, symbols in providers if name in symbols]
            if name in review["prefer_over_module"]:
                matches = [("browser-env", review["signatures"][name])]
            if not matches:
                errors.append(f"unprovided declaration: {name}")
            else:
                entry.update(provider=matches[0][0], wasm=matches[0][1])
        if name in stubs:
            entry["compatibility_stub"] = stubs[name]
    # Data addresses are linker relocations, not function imports. Only the
    # CRT/shared-libc stream pointers and libc's exported environment are
    # part of this profile; never silently accept wasm-ld's address-zero fixup.
    globals_ = set(wasm_signatures.inspect(libc)["globals"])
    ncurses_globals = set(wasm_signatures.inspect(library)["globals"])
    for name, entry in variables.items():
        if entry["unavailable"] and name in ("optarg", "optind", "opterr", "optopt",
                                             "error_message_count", "error_print_progname"):
            entry.update(provider="unavailable", reason="Package-owned gnulib state")
        elif name in ("stdin", "stdout", "stderr") and name in globals_:
            entry["provider"] = "guest-libc/CRT stream pointer (requires link relocation)"
        elif name == "environ" and name in globals_:
            entry["provider"] = "guest-libc data address (requires link relocation)"
        elif name in ncurses_globals:
            entry["provider"] = "ncurses data address (requires link relocation)"
        else:
            errors.append(f"unprovided variable: {name}")
    if errors:
        raise ValueError("SDK provider gate: " + "; ".join(errors))
    source = "".join(f"#include <{h}>\n" for h in headers)
    refs = sorted(name for name, entry in functions.items() if not entry["unavailable"])
    source += "void (*sdk_api_refs[])(void) = {\n" + ",\n".join(
        f"(void (*)(void))({name})" for name in refs) + "\n};\n"
    with tempfile.TemporaryDirectory(prefix="providers-", dir=repo / "build/engine/guest-sdk") as temporary:
        binary = Path(temporary) / "references.wasm"
        subprocess.run(command + ["-x", "c", "-O0", "-nostdlib", "-Wl,--no-entry", "-Wl,--allow-undefined",
                                  "-Wl,--export=sdk_api_refs", "-", "-o", str(binary)],
                       input=source, text=True, check=True)
        imports = wasm_signatures.inspect(binary)["imports"]
    for key, signature in imports.items():
        module, name = key.split(":", 1)
        entry = functions.get(name)
        if module != "env" or not entry or entry.get("wasm") != signature:
            errors.append(f"Wasm signature mismatch: {key}: declaration {signature}, provider {entry}")
    for name in refs:
        if functions[name]["provider"] != "header-inline" and "env:" + name not in imports:
            errors.append(f"missing declaration signature probe: {name}")
    if errors:
        raise ValueError("SDK signature gate: " + "; ".join(errors))
    # Check the configured DSO's actual dependencies as well as its exports.
    # Internal libc accessor functions have no public declarations but still
    # require an exact compiled provider, never a name-only allowance.
    libc_exports = wasm_signatures.inspect(libc)["exports"]
    for key, signature in wasm_signatures.inspect(library)["imports"].items():
        module, name = key.split(":", 1)
        matches = ([libc_exports[name]] if name in libc_exports else []) if module == "libc" else [
            symbols[name] for _, symbols in providers if name in symbols]
        if module == "env" and name in review["prefer_over_module"]:
            matches = [review["signatures"][name]]
        if module not in ("env", "libc") or not matches or matches[0] != signature:
            raise ValueError(f"ncurses dependency signature mismatch: {key}")
    return dict(format=1, status="verified partial guest ABI; not C/POSIX conformance",
                functions=functions, variables=variables,
                removed_package_apis=sorted(set(unavailable) - set(functions)),
                providers={"libc_sha256": guest_sdk.sha(libc), "ncurses_sha256": guest_sdk.sha(library)})
