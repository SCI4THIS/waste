#!/usr/bin/env python3
"""Extract unchanged LLVM mapping methods, not the complete Path implementation."""
import hashlib
import json
from pathlib import Path
import sys


def fragment(text, begin, end):
    if text.count(begin) != 1 or text.count(end) != 1:
        raise SystemExit("LLVM mapping boundaries changed; re-audit this fixture")
    start = text.index(begin)
    return text[start:text.index(end, start)]


unix, common, output = map(Path, sys.argv[1:])
text = unix.read_text()
license_header = text[:text.index("#include")]
methods = fragment(text, "std::error_code mapped_file_region::init(file_t FD, uint64_t Offset,",
                   "int mapped_file_region::alignment()")
methods += fragment(common.read_text(), "size_t mapped_file_region::size() const {",
                    "Error readNativeFileToEOF(")
generated = (license_header + "// Test-only unchanged mapping methods; not full Path support.\n"
             '#include "llvm/Config/config.h"\n'
             '#include "llvm/Support/FileSystem.h"\n'
             '#include "llvm/Support/Errno.h"\n'
             '#include "llvm/Support/Error.h"\n'
             '#include "llvm/Support/IOSandbox.h"\n'
             '#include <sys/mman.h>\n'
             "namespace llvm::sys::fs {\n" + methods + "}\n")
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix(".json").write_text(json.dumps(
    {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (unix, common)},
    indent=2) + "\n")
