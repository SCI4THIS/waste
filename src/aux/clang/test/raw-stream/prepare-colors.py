#!/usr/bin/env python3
"""Isolate unchanged upstream ANSI helpers; this does not build Process.cpp."""
import hashlib
import json
from pathlib import Path
import sys


def fragment(text, begin, end):
    if text.count(begin) != 1 or text.count(end) != 1:
        raise SystemExit("upstream ANSI helper boundaries changed; re-audit the fixture")
    start = text.index(begin)
    finish = text.index(end, start) + len(end)
    return text[start:finish]


source = Path(sys.argv[1])
output = Path(sys.argv[2])
common_path = source / "Process.cpp"
unix_path = source / "Unix/Process.inc"
common = common_path.read_text()
unix = unix_path.read_text()
constants = fragment(common, "// clang-format off\n#define COLOR", "// clang-format on\n")
helpers = fragment(unix, "void Process::UseANSIEscapeCodes(bool /*enable*/) {",
                   'const char *Process::ResetColor() { return "\\033[0m"; }')
license_header = common[:common.index('#include')]
text = (license_header + '// Test-only upstream ANSI fragments, not full Process support.\n'
        '#include "llvm/Support/Process.h"\nusing namespace llvm;\nusing namespace sys;\n'
        + constants + '\n' + helpers + '\n')
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != text:
    output.write_text(text)
provenance = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in (common_path, unix_path)}
output.with_suffix('.json').write_text(json.dumps(provenance, indent=2) + '\n')
