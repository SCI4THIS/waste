#!/usr/bin/env python3
"""Extract actual staged executable discovery, not the full Path translation unit."""
import hashlib
import json
from pathlib import Path
import sys

source = Path(sys.argv[1])
output = Path(sys.argv[2])
text = source.read_text()
begin = "std::string getMainExecutable(const char *argv0, void *MainAddr) {"
end = "TimePoint<> basic_file_status::getLastAccessedTime() const {"
if text.count(begin) != 1 or text.count(end) != 1:
    raise SystemExit("LLVM executable discovery boundaries changed; re-audit the fixture")
start = text.index(begin)
function = text[start:text.index(end, start)]
generated = (text[:text.index("#include")]
             + "// Test-only actual executable discovery, not full Path support.\n"
             '#include "llvm/Config/config.h"\n'
             '#include "llvm/Support/FileSystem.h"\n'
             '#include "llvm/Support/IOSandbox.h"\n'
             '#include <limits.h>\n#include <waste/process.h>\n'
             + "namespace llvm::sys::fs {\n" + function + "}\n")
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix(".json").write_text(json.dumps(
    {str(source): hashlib.sha256(source.read_bytes()).hexdigest()}, indent=2) + "\n")
