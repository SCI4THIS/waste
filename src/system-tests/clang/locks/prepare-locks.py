#!/usr/bin/env python3
"""Extract unchanged LLVM lock methods, not the complete Path implementation."""
import hashlib
import json
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text()
begin = "std::error_code tryLockFile(int FD, std::chrono::milliseconds Timeout,"
end = "std::error_code closeFile(file_t &F) {"
if text.count(begin) != 1 or text.count(end) != 1:
    raise SystemExit("LLVM lock method boundaries changed; re-audit the fixture")
methods = text[text.index(begin):text.index(end)]
generated = (text[:text.index("#include")] +
             '// Test-only unchanged timed/blocking lock methods.\n'
             '#include "llvm/Config/config.h"\n'
             '#include "llvm/Support/FileSystem.h"\n'
             '#include "llvm/Support/Errno.h"\n'
             '#include "llvm/Support/Errc.h"\n'
             '#include <fcntl.h>\n#include <cstring>\n#include <unistd.h>\n'
             'namespace llvm::sys::fs {\n' + methods + '}\n')
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix(".json").write_text(json.dumps(
    {str(source): hashlib.sha256(source.read_bytes()).hexdigest()}, indent=2) + "\n")
