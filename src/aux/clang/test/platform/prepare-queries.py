#!/usr/bin/env python3
"""Extract actual staged optional queries; this does not compile all Path.cpp."""
import hashlib
import json
from pathlib import Path
import sys


def fragment(text, begin, end):
    if text.count(begin) != 1 or text.count(end) != 1:
        raise SystemExit("LLVM filesystem query boundaries changed; re-audit the fixture")
    start = text.index(begin)
    return text[start:text.index(end, start)]


source = Path(sys.argv[1])
output = Path(sys.argv[2])
text = source.read_text()
license_header = text[:text.index("#include")]
queries = fragment(text, "ErrorOr<space_info> disk_space(const Twine &Path) {",
                   "std::error_code current_path(")
queries += fragment(text, "std::error_code is_local(const Twine &Path, bool &Result) {",
                    "std::error_code rename(")
generated = (license_header + "// Test-only actual optional queries, not full Path support.\n"
             '#include "llvm/Config/config.h"\n'
             '#include "llvm/Support/FileSystem.h"\n'
             '#include "llvm/Support/Errc.h"\n'
             '#include "llvm/Support/IOSandbox.h"\n'
             "namespace llvm::sys::fs {\n" + queries + "}\n")
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix(".json").write_text(json.dumps(
    {str(source): hashlib.sha256(source.read_bytes()).hexdigest()}, indent=2) + "\n")
