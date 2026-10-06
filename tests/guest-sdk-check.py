#!/usr/bin/env python3
"""Hermetic preprocessing, include-order, dependency and ABI regression gate."""
import json
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "src/html-rt/tools"))
import guest_sdk
from guest_providers import inspect_sdk
import wasm_signatures


def main():
    root = REPO / "src/vfs"
    manifest = guest_sdk.audit(root)
    policy = guest_sdk.api_policy()
    policy["browser_adapter_review"]["sha256"] = "0" * 64
    with patch.object(guest_sdk, "api_policy", return_value=policy):
        try:
            inspect_sdk(REPO, root, manifest)
        except ValueError as error:
            assert "binding review is stale" in str(error)
        else:
            raise AssertionError("stale browser adapter review passed SDK gate")
    policy = guest_sdk.api_policy()
    secondary = next(iter(policy["browser_adapter_review"]["additional_sources"]))
    policy["browser_adapter_review"]["additional_sources"][secondary] = "0" * 64
    with patch.object(guest_sdk, "api_policy", return_value=policy):
        try:
            inspect_sdk(REPO, root, manifest)
        except ValueError as error:
            assert "binding review is stale" in str(error)
        else:
            raise AssertionError("stale secondary adapter source passed SDK gate")
    command = ["clang", "--target=wasm32", "-std=c11", "-Werror", "-x", "c",
               *guest_sdk.include_flags(root)]
    names = sorted(e["path"].removeprefix("/usr/include/") for e in manifest["headers"]
                   if e["role"] == "sdk-public" or e["role"] == "sdk-ncurses")
    # The raw upstream curses header is exposed only through its ABI wrapper.
    names.remove("waste/ncurses/curses.h")
    for name, entry in {**manifest["api"]["functions"], **manifest["api"]["variables"]}.items():
        if entry["unavailable"]:
            result = subprocess.run(command + ["-fsyntax-only", "-"],
                                    input=f'#include <{entry["headers"][0]}>\nvoid check(void) {{ (void)(&{name}); }}\n',
                                    text=True, capture_output=True)
            assert result.returncode != 0 and "unavailable" in result.stderr, f"unguarded SDK API: {name}"
    for name in names:
        subprocess.run(command + ["-fsyntax-only", "-"], input=f"#include <{name}>\n", text=True, check=True)
    groups = [
        ["stdint.h", "sys/types.h", "stddef.h", "stdio.h", "stdlib.h", "string.h", "time.h", "sys/time.h", "sys/stat.h"],
        ["stdbool.h", "string.h", "curses.h", "term.h", "unctrl.h"],
        ["signal.h", "sys/select.h", "termios.h", "sys/ioctl.h", "sys/mman.h", "dirent.h"],
    ]
    with tempfile.TemporaryDirectory(prefix="guest-sdk-", dir=REPO / "build/engine") as temporary:
        for index, group in enumerate(groups):
            for reverse in (False, True):
                order = list(reversed(group)) if reverse else group
                source = "".join(f"#include <{name}>\n" for name in order)
                if index == 1:
                    source += '_Static_assert(sizeof(bool)==1,"C11 bool survives curses");\n'
                    source += '_Static_assert(sizeof(NCURSES_BOOL)==4,"ncurses bool ABI");\n'
                    source += '_Static_assert(sizeof(((WINDOW *)0)->_clear)==4,"WINDOW bool ABI");\n'
                dependency = Path(temporary) / f"{index}-{reverse}.d"
                subprocess.run(command + ["-fsyntax-only", "-MD", "-MF", str(dependency), "-"],
                               input=source, text=True, check=True)
                for path in dependency.read_text().replace("\\\n", " ").split()[1:]:
                    if not Path(path).resolve().is_relative_to(root):
                        raise AssertionError(f"guest preprocessing escaped mounted SDK: {path}")
        probe = REPO / "tests/guest-sdk-abi.c"
        subprocess.run(command + ["-fsyntax-only", str(probe)], check=True)
        binary = Path(temporary) / "abi.wasm"
        subprocess.run(command + ["-O2", "-nostdlib", "-Wl,--no-entry", "-Wl,--export=sdk_check",
                                  "-Wl,--export=sdk_assert_fail",
                                  str(probe), "-o", str(binary)], check=True)
        subprocess.run(["node", "-e", 'const fs=require("fs"),assert=require("assert");'
                        'const m=new WebAssembly.Module(fs.readFileSync(process.argv[1]));'
                        'const i=new WebAssembly.Instance(m);assert.equal(i.exports.sdk_check(),1);'
                        'assert.throws(()=>i.exports.sdk_assert_fail(),WebAssembly.RuntimeError);',
                        str(binary)], check=True)
        for header in ("linux/unistd.h", "helper.h", "kernel.h", "syscall.h", "ncurses_cfg.h",
                       "waste-gnulib-compat.h"):
            result = subprocess.run(command + ["-fsyntax-only", "-"],
                                    input=f"#include <{header}>\n", text=True, capture_output=True)
            assert result.returncode != 0, f"unexpected host/private header: {header}"
        copy = Path(temporary) / "vfs"
        shutil.copytree(root, copy)
        support = copy / "usr/lib/waste/cc/include/stdarg.h"
        original = support.read_bytes()
        support.write_bytes(b"/* partial compiler header */\n")
        try:
            guest_sdk.audit(copy)
        except ValueError:
            pass
        else:
            raise AssertionError("modified compiler header passed SDK audit")
        support.write_bytes(original)
        library = copy / "lib/libncurses.so.wasm"
        library.write_bytes(b"incompatible DSO")
        try:
            guest_sdk.audit(copy)
        except ValueError:
            pass
        else:
            raise AssertionError("mismatched ncurses DSO passed SDK audit")
        library.write_bytes((root / "lib/libncurses.so.wasm").read_bytes())
        private = copy / "usr/include/helper.h"
        private.write_text("/* accidental private SDK header */\n")
        try:
            guest_sdk.audit(copy)
        except ValueError:
            pass
        else:
            raise AssertionError("unlisted/private header passed SDK audit")
        import vfs
        before = (copy / vfs.MANIFEST).read_bytes()
        try:
            vfs.install_sdk(copy, root, manifest)
        except ValueError:
            pass
        else:
            raise AssertionError("SDK install overwrote an unlisted/private header")
        assert (copy / vfs.MANIFEST).read_bytes() == before, "failed SDK install modified inventory"
        private.unlink()
        header = copy / "usr/include/stdlib.h"
        original = header.read_text()
        for mutation in (original + '\nint waste_unprovided_probe(void);\n',
                         original.replace("void *malloc(size_t);", "double malloc(size_t);")):
            header.write_text(mutation)
            try:
                inspect_sdk(REPO, copy, manifest)
            except ValueError as error:
                assert "unprovided" in str(error) or "signature mismatch" in str(error), str(error)
            else:
                raise AssertionError("unprovided/incompatible declaration passed provider gate")
        header.write_text(original)
        # Malformed bounded signature input is never accepted as a provider.
        for index, data in enumerate((b"", b"\0asm\x01\0\0\0\1\xff", b"\0asm\x01\0\0\0\1\4\1\x60\x7f\0")):
            malformed = Path(temporary) / f"bad-{index}.wasm"
            malformed.write_bytes(data)
            try:
                wasm_signatures.inspect(malformed)
            except ValueError:
                pass
            else:
                raise AssertionError("malformed provider signatures accepted")
    print(f"Guest SDK: {len(names)} headers, 6 include orders, ABI/dependency/availability/provider negatives passed")


if __name__ == "__main__":
    main()
