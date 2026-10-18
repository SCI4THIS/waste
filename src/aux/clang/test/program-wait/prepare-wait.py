#!/usr/bin/env python3
"""Isolate actual staged LLVM Wait; not a full Program translation unit."""
import hashlib
import json
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
program = source / "llvm/lib/Support/Unix/Program.inc"
unix = source / "llvm/lib/Support/Unix/Unix.h"
text = program.read_text()


def section(contents, begin, end):
    if contents.count(begin) != 1 or contents.count(end) != 1:
        raise SystemExit("LLVM Wait boundaries changed; re-audit the fixture")
    start, stop = contents.index(begin), contents.index(end)
    if start >= stop:
        raise SystemExit("LLVM Wait boundary order changed; re-audit the fixture")
    return contents[start:stop]


constructor = section(text, "ProcessInfo::ProcessInfo()", "ErrorOr<std::string> sys::findProgramByName")
method = section(text, "ProcessInfo llvm::sys::Wait(", "std::error_code llvm::sys::ChangeStdinMode(")
message = section(unix.read_text(), "static inline bool MakeErrMsg(", "// Include StrError(errnum)")
generated = (text[:text.index("#include")] +
             '// Diagnostic fixture: actual staged Wait and error helper.\n'
             '#include "llvm/Config/config.h"\n'
             '#include "llvm/Support/Program.h"\n'
             '#include "llvm/Support/Errno.h"\n'
             '#include "llvm/Support/WasteProgram.h"\n'
             '#include <signal.h>\n#include <sys/wait.h>\n#include <unistd.h>\n'
             'using namespace llvm;\nusing namespace llvm::sys;\n' +
             constructor + message + method)
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix(".json").write_text(json.dumps({
    "scope": "Actual staged Wait, ProcessInfo constructor and MakeErrMsg; not full Program.cpp",
    "sources": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in (program, unix)},
    "generatedSha256": hashlib.sha256(generated.encode()).hexdigest(),
}, indent=2) + "\n")
