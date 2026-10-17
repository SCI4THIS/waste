#!/usr/bin/env python3
"""Extract the two actual staged LLVM argument-fit overloads, not Program.cpp."""
import hashlib
import json
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
unix = source / "llvm/lib/Support/Unix/Program.inc"
program = source / "llvm/lib/Support/Program.cpp"


def section(contents, begin, end):
    if contents.count(begin) != 1 or contents.count(end) != 1:
        raise SystemExit("LLVM argument-fit boundaries changed; re-audit fixture")
    start, stop = contents.index(begin), contents.index(end)
    if start >= stop:
        raise SystemExit("LLVM argument-fit boundary order changed; re-audit fixture")
    return contents[start:stop]


text = unix.read_text()
first = "bool llvm::sys::commandLineFitsWithinSystemLimits(StringRef Program,"
if text.count(first) != 1:
    raise SystemExit("LLVM argument-fit definition changed; re-audit fixture")
end = "  return true;\n#endif\n}\n"
if text.count(end) != 1 or not text.endswith(end):
    raise SystemExit("LLVM argument-fit trailing boundary changed; re-audit fixture")
method = text[text.index(first):]
overload = section(program.read_text(),
                   "bool sys::commandLineFitsWithinSystemLimits(StringRef Program,",
                   "void sys::printArg(")
generated = (text[:text.index("#include")] +
             '// Diagnostic fixture: actual staged argument-fit overloads.\n'
             '#include "llvm/Config/config.h"\n'
             '#include "llvm/ADT/SmallVector.h"\n'
             '#include "llvm/Support/WasteProgram.h"\n'
             'using namespace llvm;\n' + method + overload)
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix(".json").write_text(json.dumps({
    "scope": "Actual staged argument-fit overloads; not complete Program.cpp",
    "sources": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in (unix, program)},
    "generatedSha256": hashlib.sha256(generated.encode()).hexdigest(),
}, indent=2) + "\n")
